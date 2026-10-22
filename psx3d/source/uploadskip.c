// Uploads that change nothing (docs/dev/upload-skip.md).
//
// Some games send the same pixels to PS1 VRAM again and again: Worms World
// Party rewrites its text images ~35 times a frame with identical data. For
// the GPU renderer every upload means "these pixels changed": it re-decodes
// the textures over them and copies the rectangle into its render target.
// When software VRAM already held exactly these pixels, and nothing the GPU
// drew there is still missing from software VRAM, the render target and the
// decoded textures already hold them too, so the upload can stop at
// software VRAM.
//
// The core keeps its code: gpulib's calls to pica_settle_rect (start of a
// VRAM transfer) and renderer_update_caches (its end) come here through
// --wrap in the core's partial link (the frontend calls the originals by
// their own names). Start: copy the destination; end: compare. Only without the GPU thread (the Old 3DS): the tile tables read
// here belong to the renderer's thread otherwise.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "../../core/plugins/gpulib/gpu.h"

void pica_settle_rect(int x, int y, int w, int h);
extern int (*pica_thread_call)(void (*call)(void *), void *data);
extern int pica_debug;
// gpu_pica.c: GPU drawing software VRAM hasn't seen yet
extern const uint16_t pica_drawn_blocks[32];     // 64x16 blocks (RTT old)
extern const uint32_t pica_drawn_tiles[64][4];   // 8x8 tiles (RTT new)
extern const bool pica_rtt_detected;

#define RTT_OLD 262144
#define RTT_FORCE_NEW 1048576
#define SKIP_OFF 268435456   // gldbg: every upload goes through (A/B)
#define SNAP_PIXELS 4096

unsigned psx3d_uploads_kept;   // uploads that stopped at software VRAM
static uint16_t snap[SNAP_PIXELS];
static int snap_x, snap_y, snap_w, snap_h;   // snap_h 0: no copy taken
// A big rectangle (1024+ pixels) whose last uploads all changed (a movie)
// isn't copied for the next 15 uploads to it; small copies cost next to
// nothing and some games alternate between changed and unchanged ones.
static struct { int16_t x, y, w, h; uint8_t misses, wait; } recent[64];
#ifdef UPLOADSKIP_STATS   // simulator: why uploads went through
unsigned psx3d_upload_why[6];   // other, size/edge, waiting, GPU drawing, changed, kept
#define WHY(n) (psx3d_upload_why[n]++)
#else
#define WHY(n) ((void)0)
#endif

static bool gpu_newer(int x, int y, int w, int h)
{
	if ((pica_debug & RTT_OLD) || (!pica_rtt_detected && !(pica_debug & RTT_FORCE_NEW))) {
		int bx0 = x >> 6, bx1 = (x + w - 1) >> 6;
		uint16_t m = (uint16_t)(((2u << bx1) - 1) & ~((1u << bx0) - 1));
		for (int by = y >> 4; by <= (y + h - 1) >> 4; by++)
			if (pica_drawn_blocks[by] & m)
				return true;
		return false;
	}
	int bx0 = x >> 3, bx1 = (x + w - 1) >> 3;
	for (int by = y >> 3; by <= (y + h - 1) >> 3; by++)
		for (int word = bx0 >> 5; word <= (bx1 >> 5); word++) {
			int lo = word == (bx0 >> 5) ? (bx0 & 31) : 0;
			int hi = word == (bx1 >> 5) ? (bx1 & 31) : 31;
			if (pica_drawn_tiles[by][word] & (~0u << lo) & (~0u >> (31 - hi)))
				return true;
		}
	return false;
}

static inline unsigned slot(int x, int y)
{
	return ((x >> 4) ^ (y >> 3) * 7) & 63;
}

void __wrap_pica_settle_rect(int x, int y, int w, int h)
{
	pica_settle_rect(x, y, w, h);   // pending fills are in software VRAM now
	snap_h = 0;
	if (gpu.dma.is_read || gpu.async || pica_thread_call || (pica_debug & SKIP_OFF))
		return (void)WHY(0);
	if (w * h > SNAP_PIXELS || x + w > 1024 || y + h > 512)
		return (void)WHY(1);
	unsigned i = slot(x, y);
	if (recent[i].x == x && recent[i].y == y && recent[i].w == w && recent[i].h == h) {
		if (recent[i].wait) {
			recent[i].wait--;
			return (void)WHY(2);
		}
	} else {
		recent[i].x = x; recent[i].y = y; recent[i].w = w; recent[i].h = h;
		recent[i].misses = recent[i].wait = 0;
	}
	if (gpu_newer(x, y, w, h))
		return (void)WHY(3);
	for (int r = 0; r < h; r++)
		memcpy(snap + r * w, gpu.vram + (y + r) * 1024 + x, w * 2);
	snap_x = x; snap_y = y; snap_w = w; snap_h = h;
}

void __wrap_renderer_update_caches(int x, int y, int w, int h, int state_changed)
{
	if (snap_h && !state_changed && x == snap_x && y == snap_y && w == snap_w && h == snap_h) {
		snap_h = 0;
		int r = 0;
		while (r < h && !memcmp(snap + r * w, gpu.vram + (y + r) * 1024 + x, w * 2))
			r++;
		unsigned i = slot(x, y);
		if (r == h && !gpu_newer(x, y, w, h)) {   // still nothing GPU-drawn there
			recent[i].misses = 0;
			psx3d_uploads_kept++;
			WHY(5);
			return;
		}
		WHY(4);
		if (w * h >= 1024 && ++recent[i].misses >= 2)
			recent[i].wait = 15;
	}
	snap_h = 0;
	renderer_update_caches(x, y, w, h, state_changed);
}
