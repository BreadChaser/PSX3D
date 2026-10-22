// Real 3D for the GPU renderer, New 3DS only: see gpustereo.h.
//
// Threads: gpu_pica draws on the GPU thread (New 3DS). The main thread
// captures depths as it queues polygons and, once per frame after
// pica_thread_sync, decides whether 3D is wanted (gpustereo_frame). The GPU
// thread switches mirroring on/off between frames (gpustereo_after_present)
// and does all mirroring in the wrappers below.
#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <string.h>
#include "../vendor/citro3d/internal.h"
#include "psx3d.h"
#include "gpustereo.h"
#include "../../core/plugins/gpulib/gpu.h"
#include "../../core/libpcsxcore/psx3d_depth.h"

#define VRAM_W 1024
#define VRAM_H 512
#define VRAM_BYTES (VRAM_W * VRAM_H * 2)
#define RING_HALF (24 * 1024)   // vertices per frame (gpu_pica: 16K at most between syncs)

bool video_stereo_targets(bool native);   // video.c: native top targets free VRAM
bool video_stereo_on(void);               // video.c: the top screen shows 3D
bool video_top_aa(void);                  // video.c: supersampled top targets

// mirroring state; written by the GPU thread between frames
static volatile bool mirror;
static bool need_seed, seeded, hold_valid, failed;
static C3D_Tex right_tex, right_hold;
static gs_vtx *ring;
static unsigned ring_serial, ring_half, ring_pos;
static uintptr_t dirty_lo, dirty_hi;
static void *canonical, *hold;   // gpu_pica's VRAM target and held picture
static float want;               // main thread -> GPU thread
static bool tracking;
volatile bool gpustereo_copy_bypass;
extern unsigned video_frame_serial;   // renderqueue.c: C3D_FrameBegin count

int gpustereo_mode(void)
{
	if (!g_new3ds)
		return 0;
	return sw_stereo >= 0 ? sw_stereo : g_cfg.stereo;
}

// --- main thread -----------------------------------------------------------

// gpu_async calls psx3d_depth_cmd for every command it queues while depth
// recording is on; the core is linked with --wrap so it lands here.
static inline int sext11(uint32_t v) { return ((int32_t)(v << 21)) >> 21; }
// The empty originals stay linked and are still called: dropping them
// would move the renderer's code (Old 3DS speed depends on its layout).
void psx3d_depth_cmd(const uint32_t *list, uint32_t cmd);   // the core's originals
void psx3d_stereo_set(int on);
void __wrap_psx3d_depth_cmd(const uint32_t *list, uint32_t cmd)
{
	psx3d_depth_cmd(list, cmd);
	if (cmd < 0x20 || cmd >= 0x40)
		return;
	uint16_t z[4] = { 0 };
	int n = (cmd & 8) ? 4 : 3, i = 1;
	for (int k = 0; k < n; k++) {
		if (k && (cmd & 0x10))
			i++;
		uint32_t xy = list[i++];
		if (cmd & 4)
			i++;
		uint32_t key = psx3d_zkey(sext11(xy), sext11(xy >> 16));
		const psx3d_zent *e = &psx3d_ztable[psx3d_zhash(key)];
		// Up to two frames old: some games (R4) project in one frame and
		// draw in the next, at 30 fps.
		if (e->key == key && e->z && (uint16_t)(psx3d_zframe - e->frame) <= 2)
			z[k] = e->z;
	}
	gs_capture(list, cmd, gpu.ex_regs[3], gpu.ex_regs[4], gpu.ex_regs[5], z);
}

// The core turns GTE depth recording on/off through this (an empty stub
// in gpu_pica.c).
void __wrap_psx3d_stereo_set(int on)
{
	psx3d_stereo_set(on);
	memset(psx3d_ztable, 0, sizeof(psx3d_ztable));
	psx3d_zframe = 0;
	psx3d_stereo_enabled = on;
}

void gpustereo_frame(void)
{
	static int below;
	float slider = slider_get();
	below = slider < 0.02f ? below + 1 : 0;
	// a little hysteresis: a slider resting near 0 doesn't toggle recording
	// (each toggle recompiles the dynarec's code)
	bool on = gpustereo_mode() == 2 && renderer_is_gpu() && !failed &&
		(slider >= 0.05f || (tracking && below <= 30));
	if (on && !gs_core_alloc())
		on = false;
	if (on != tracking) {
		gs_core_clear();
		config_set_stereo_active(on);
		tracking = on;
	}
	if (on)
		psx3d_zframe++;
	gs_core_frame(on ? slider : 0, gpu.screen.hres);
	want = on ? slider : 0;
}

// --- GPU thread ------------------------------------------------------------

static void release(void)
{
	mirror = seeded = hold_valid = need_seed = false;
	if (right_tex.data) C3D_TexDelete(&right_tex);
	if (right_hold.data) C3D_TexDelete(&right_hold);
	memset(&right_tex, 0, sizeof(right_tex));
	memset(&right_hold, 0, sizeof(right_hold));
	if (ring) linearFree(ring);
	ring = NULL;
	canonical = hold = NULL;
}

static bool allocate(void)
{
	// The right eye must be a VRAM render target; the supersampled top
	// screens leave no room for it, so 3D gameplay uses native ones.
	if (!video_stereo_targets(true))
		return false;
	ring = linearAlloc(2 * RING_HALF * sizeof(gs_vtx));
	if (!ring || !C3D_TexInitVRAM(&right_tex, VRAM_W, VRAM_H, GPU_RGBA5551) ||
	    !C3D_TexInit(&right_hold, VRAM_W, VRAM_H, GPU_RGBA5551)) {
		log_printf("3D: no memory for the right eye (VRAM %uK, linear %uK free)\n",
			(unsigned)(vramSpaceFree() / 1024), (unsigned)(linearSpaceFree() / 1024));
		release();
		return false;
	}
	C3D_TexSetWrap(&right_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	C3D_TexSetWrap(&right_hold, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	ring_serial = video_frame_serial;
	ring_half = ring_pos = 0;
	log_printf("3D: right eye ready, VRAM %uK linear %uK free\n",
		(unsigned)(vramSpaceFree() / 1024), (unsigned)(linearSpaceFree() / 1024));
	return true;
}

// Between frames: nothing recorded refers to the right eye yet.
void gpustereo_after_present(void)
{
	float s = want;
	if (s > 0 && !failed && !right_tex.data && !allocate())
		failed = true;
	bool on = s > 0 && right_tex.data;
	if (on && !mirror) {
		need_seed = true;
		seeded = hold_valid = false;
	}
	if (!on)
		seeded = hold_valid = false;
	mirror = on;
}

void gpustereo_game_end(void)
{
	release();
	gs_core_free();
	if (tracking)
		config_set_stereo_active(false);
	tracking = failed = false;
	want = 0;
}

static inline bool in_canonical(const void *p)
{
	return canonical && (uintptr_t)p - (uintptr_t)canonical < VRAM_BYTES;
}

// Draws go on to the direct command path (picadirect.c), which falls back
// to citro3d itself for anything it does not write.
void picadirect_draw_arrays(GPU_Primitive_t primitive, int first, int size);
Result __real_GX_TextureCopy(u32 *src, u32 sdim, u32 *dst, u32 ddim, u32 size, u32 flags);

// The right eye starts as a copy of PS1 VRAM; queued after everything
// recorded so far.
static void seed(void)
{
	C3D_FrameSplit(0);
	__real_GX_TextureCopy(canonical, 0, right_tex.data, 0, VRAM_BYTES, 8);
	need_seed = false;
	seeded = true;
	hold_valid = false;
}

static noinline void mirror_draw(GPU_Primitive_t primitive, int first, int size)
{
	C3D_Context *ctx = C3Di_GetContext();
	C3D_FrameBuf *fb = &ctx->fb;
	if (fb->colorFmt != GPU_RB_RGBA5551 || fb->width != VRAM_W || fb->height != VRAM_H ||
	    fb->colorBuf == right_tex.data || primitive != GPU_TRIANGLES) {
		picadirect_draw_arrays(primitive, first, size);   // not PS1 VRAM
		return;
	}
	canonical = fb->colorBuf;
	if (need_seed)
		seed();
	picadirect_draw_arrays(primitive, first, size);

	C3D_BufInfo *bi = &ctx->bufInfo;
	C3D_BufCfg *buf = &bi->buffers[0];
	bool moved = false;
	u32 offset = buf->offset;
	if (bi->bufCount == 1 && ((buf->flags[1] >> 16) & 0xfff) == sizeof(gs_vtx)) {
		if (video_frame_serial != ring_serial) {
			// a new frame: the GPU has finished the one before last
			ring_serial = video_frame_serial;
			ring_half ^= 1;
			ring_pos = 0;
		}
		if (ring_pos + size <= RING_HALF) {
			extern u32 __ctru_linear_heap;
			const u8 *heap = (const u8 *)__ctru_linear_heap;
			const gs_vtx *src = (const gs_vtx *)(heap + (bi->base_paddr + offset -
				osConvertVirtToPhys(heap))) + first;
			gs_vtx *dst = ring + ring_half * RING_HALF + ring_pos;
			if (gs_shift(src, dst, size)) {
				moved = true;
				ring_pos += size;
				buf->offset = osConvertVirtToPhys(dst) - first * sizeof(gs_vtx) - bi->base_paddr;
				ctx->flags |= C3DiF_BufInfo;
				uintptr_t lo = (uintptr_t)dst, hi = lo + size * sizeof(gs_vtx);
				if (!dirty_hi) dirty_lo = lo, dirty_hi = hi;
				dirty_lo = lo < dirty_lo ? lo : dirty_lo;
				dirty_hi = hi > dirty_hi ? hi : dirty_hi;
			}
		}
	}
	void *color = fb->colorBuf;
	fb->colorBuf = right_tex.data;
	ctx->flags |= C3DiF_FrameBuf;
	picadirect_draw_arrays(primitive, first, size);
	fb->colorBuf = color;
	ctx->flags |= C3DiF_FrameBuf;
	if (moved) {
		buf->offset = offset;
		ctx->flags |= C3DiF_BufInfo;
	}
}

void __wrap_C3D_DrawArrays(GPU_Primitive_t primitive, int first, int size)
{
	if (__builtin_expect(mirror, 0))
		mirror_draw(primitive, first, size);
	else
		picadirect_draw_arrays(primitive, first, size);
}

// Copies inside PS1 VRAM are repeated inside the right eye; a copy out of
// it is the renderer's held picture (frameskip), kept for the right eye too.
static noinline void mirror_copy(u32 *src, u32 sdim, u32 *dst, u32 ddim, u32 size, u32 flags)
{
	if (!seeded)
		return;   // the seed will copy the result
	u8 *r = right_tex.data;
	if (in_canonical(src) && in_canonical(dst))
		__real_GX_TextureCopy((u32 *)(r + ((u8 *)src - (u8 *)canonical)), sdim,
			(u32 *)(r + ((u8 *)dst - (u8 *)canonical)), ddim, size, flags);
	else if (in_canonical(dst))
		__real_GX_TextureCopy(src, sdim, (u32 *)(r + ((u8 *)dst - (u8 *)canonical)), ddim, size, flags);
	// The held picture is copied as whole VRAM rows (no stride); strided
	// copies out of VRAM are the renderer's own snapshots (framebuffer
	// sprites), which both eyes sample from the left eye's VRAM.
	else if (in_canonical(src) && !sdim && !ddim && !gpustereo_copy_bypass) {
		uintptr_t off = (u8 *)src - (u8 *)canonical;
		void *base = (u8 *)dst - off;
		if (base != hold) {
			hold = base;
			hold_valid = false;
		}
		__real_GX_TextureCopy((u32 *)(r + off), sdim,
			(u32 *)((u8 *)right_hold.data + off), ddim, size, flags);
		hold_valid = true;
	}
}

Result __wrap_GX_TextureCopy(u32 *src, u32 sdim, u32 *dst, u32 ddim, u32 size, u32 flags)
{
	Result rc = __real_GX_TextureCopy(src, sdim, dst, ddim, size, flags);
	if (__builtin_expect(mirror, 0))
		mirror_copy(src, sdim, dst, ddim, size, flags);
	return rc;
}

// renderqueue.c, before a command list is handed to the GPU
void gpustereo_frame_end(void)
{
	if (dirty_hi) {
		svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)dirty_lo, dirty_hi - dirty_lo);
		dirty_lo = dirty_hi = 0;
	}
}

bool gpustereo_ready(void) { return mirror && seeded; }
float gpustereo_flat(void) { return mirror && seeded ? gs_flat() : 1; }

// the right eye's version of what pica_picture returned
void *gpustereo_right_for(void *picture)
{
	C3D_Tex *t = picture;
	if (!mirror || !seeded || !t)
		return NULL;
	if (t->data == canonical)
		return &right_tex;
	if (t->data == hold && hold_valid)
		return &right_hold;
	return NULL;
}

void gpustereo_status(char *out, int n)
{
	unsigned lookups, found, shifted, rejected;
	gs_core_counts(&lookups, &found, &shifted, &rejected);
	snprintf(out, n, ",\"stereo_mode\":%d,\"stereo\":{\"want\":%.3f,\"tracking\":%s,\"mirror\":%s,"
		"\"seeded\":%s,\"hold\":%s,\"failed\":%s,\"focus\":%.1f,\"lookups\":%u,\"found\":%u,\"shifted\":%u,\"rejected\":%u,"
		"\"flat\":%.2f,\"display3d\":%s,\"top_aa\":%s,\"free_vram\":%u}",
		gpustereo_mode(), want, tracking ? "true" : "false", mirror ? "true" : "false",
		seeded ? "true" : "false", hold_valid ? "true" : "false", failed ? "true" : "false",
		gs_focus(), lookups, found, shifted, rejected, gpustereo_flat(), video_stereo_on() ? "true" : "false",
		video_top_aa() ? "true" : "false", (unsigned)vramSpaceFree());
}
