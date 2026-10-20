#include "../gpulib/psx3d_frameskip.h"
/*
 * gpu_pica: the PS1 GPU drawn by the 3DS GPU (PICA200) through citro3d.
 * PSX3D, GPL v2 or later.
 *
 * gpulib parses the command stream, does VRAM transfers and timing; this
 * file turns primitives into GPU vertices. PS1 VRAM lives on the GPU as a
 * 1024x512 RGBA5551 render target (the PS1's own 15-bit + mask format), so
 * the displayed picture is a rectangle of it and needs no CPU work.
 *
 * Textures: a texture page (4/8-bit with a CLUT, or 15-bit) is decoded once
 * into a 256x256 RGBA8 texture and reused until that part of VRAM changes.
 * Texel alpha keeps the PS1's per-texel rules: 0 = transparent (colour 0),
 * 128 = semi-transparent bit set, 255 = opaque. That lets the common
 * blending modes (B/2+F/2 and B+F) draw in a single pass.
 *
 * Software VRAM (gpu.vram) stays the source for texture decoding and the
 * CPU's uploads; uploads are also copied into the render target.
 */
#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../gpulib/gpu.h"
#include "gpu_pica_thread.h"
#include "../gpulib/gpu_timing.h"
#include "../gpulib/gpu_async.h"
#include "gpu_pica.h"
#include "../gpulib/pica_layout.h"
#include "../../frontend/plugin_lib.h"

#define VRAM_W 1024
#define VRAM_H 512
#define MAX_VERTS (16 * 1024)   // ~2000 primitives; when full the GPU catches up
#define SEND_VERTS 1536   // ~500 triangles: then the GPU gets them
#define STAGE_W 512
#define STAGE_H 256
typedef struct { s16 x, y, u, v; u32 rgba; } pvtx;   // rgba bytes R,G,B,A

// Texture cache: an entry per (depth, page, CLUT) holding just the part of
// the page drawn with so far, an aligned square of 32, 64, 128 or 256
// texels (games use many CLUTs, each on a small part of a page). All the
// squares live in one shared atlas texture, so changing
// textures never ends a batch: draw calls are expensive (~20us each on an
// Old 3DS).
#define NSIZES 4
// 1024x1024 (4 MB), shared by all sizes: 32x32 cells of 32x32 texels, a
// square of 32 << lg takes an aligned block of (1 << lg)^2 cells. (Fixed
// regions per size thrashed: racing games ran out of one size while the
// others sat empty.) An Old 3DS has 13 MB of GPU memory in all; the library
// music is let go during games to make room.
#define CELLS 32
#define CELL_FREE (-1)
#define CELL_PLAIN (-2)
static struct {
	C3D_Tex tex;
	int w, h;
	bool rebind;       // squares were decoded into it: rebind so the GPU texture cache drops old data
} atlas[1];
#define NENTRIES (CELLS * CELLS)

typedef struct tcache {
	bool valid;
	bool gpu_tiles;
	u8 depth;          // 0 4-bit, 1 8-bit, 2 15-bit
	u8 lg;             // size class: squares are 32 << lg
	u8 cw, ch;         // size in 32x32 cells (squares: 1 << lg)
	u8 rx, ry;         // the square's origin in page texels
	s16 ax, ay;        // ...and in its atlas
	u16 page;          // texpage bits 0-4 (x/64, y/256)
	u16 clut;          // CLUT position word (x/16, y)
	u32 win;           // texture window (GP0 E2) it was decoded with, 0 = none
	u32 stamp;         // VRAM write generation it was decoded at
	u32 checked;       // gen_clock when last found still valid
	u64 tiles;         // decoded 32x32 tiles inside the square (8x8 at most)
	u32 used_seq;      // last batch that drew with it
	u32 lru;
	u16 slot;          // its place in active[lg]
	struct tcache *next;   // hash chain
} tcache;

static void pica_settle_rect_local(int x, int y, int w, int h);
static void pica_settle_vram_local(void);
static void pica_readback_rect_local(int x, int y, int w, int h);
static void pica_readback_all_local(void);
static void pica_dump_vram_local(const char *path);
static void pica_frame_started_local(void);
static void pica_frame_ended_local(void);
static void pica_drop_fills_local(void);
static void renderer_sync_ecmds_local(uint32_t *ecmds);
static void renderer_update_caches_local(int x, int y, int w, int h, int state_changed);
static void renderer_flush_queues_local(void);
static void pica_flip_local(void);
static void pica_prepare_present_local(void);
static void PICA_COLD pica_stats_local(char *buf, int n);
static void PICA_COLD pica_probe_local(char *buf, int n);
// The command-list loop has no thread check. Only CPU-visible boundaries
// and frontend inspection use these synchronous owner-thread requests.
int (*pica_thread_call)(void (*call)(void *), void *data);
enum thread_op {
  TH_SETTLE_RECT, TH_SETTLE_VRAM, TH_DROP_FILLS, TH_READ_RECT, TH_READ_ALL,
  TH_DUMP, TH_SYNCE, TH_UPDATE, TH_FLUSH, TH_FLIP, TH_STATS, TH_PROBE,
  TH_FRAME_START, TH_FRAME_END, TH_PREPARE_PRESENT, TH_COPY
};
struct thread_request { enum thread_op op; int x, y, w, h; void *data; };
static void thread_execute(void *data);
static inline int thread_request(enum thread_op op, int x, int y, int w, int h, void *data)
{
  if (!pica_thread_call)
    return 0;
  struct thread_request req = { op, x, y, w, h, data };
  return pica_thread_call(thread_execute, &req);
}
static void thread_noop(void *data) { (void)data; }
void pica_thread_sync(void)
{
  if (pica_thread_call)
    pica_thread_call(thread_noop, NULL);
}

// ------------------------------------------------------------------ state

static DVLB_s *dvlb;
static shaderProgram_s prog;
static int u_proj, u_texscale;
static C3D_Tex vram_tex;              // the PS1 VRAM render target
static C3D_RenderTarget *target, *atlas_target;
static C3D_Tex dither_tex;            // repeated PS1 4x4 signed offsets (8x8 hardware minimum)
static int applied_dither = -1;     // independent of stage 0 / STP mask pass
static unsigned dither_config = 1;   // 0 off, 1 E1-controlled, 2 force
static C3D_Tex stage_tex;             // CPU uploads on their way into the target
// A private copy of the last drawn scanout. Skipped frames still execute
// large fills and CPU uploads, so holding VRAM coordinates is not enough.
static C3D_Tex disp_tex;
static bool disp_used;
static int disp_x, disp_y, disp_w, disp_h;
static int stage_y;                   // stage rows used by uploads the GPU hasn't run yet
static pvtx *vbuf, *vbuf_base;
static int vlimit = MAX_VERTS, vbank;
static u32 frame_seq;
static int vpos, vstart, vclean;
static int sent_vpos;   // vertex buffer position at the last hand-off to the GPU
static bool ready, frame_open;
static bool base_dirty = true, state_dirty = true;
static u32 seq = 1, synced_seq;
static tcache cache[NENTRIES];
static s16 cell_owner[CELLS * CELLS];   // entry, CELL_FREE or CELL_PLAIN
static u32 cell_rows[CELLS];            // occupied cells, a bit per column
static u16 active[NSIZES][NENTRIES];    // entries in use, by size
static int nactive[NSIZES];
static u16 free_list[NENTRIES];
static int nfree;
static tcache plain_e;
static tcache *plain;
#ifdef PICA_SIM_HASH
unsigned long long pica_sim_hash = 1469598103934665603ull;
// HTRACE=file: every hashed record as text, to find where two builds part
static FILE *tr; static int tr_init;
void pica_sim_mark(int f) { if (tr) fprintf(tr, "FRAME %d\n", f); }
static void sim_hash(const void *p, size_t n)
{
	if (!tr_init) { tr_init = 1; if (getenv("HTRACE")) tr = fopen(getenv("HTRACE"), "w"); }
	if (tr) { const u32 *w = p; for (size_t i = 0; i < n / 4; i++) fprintf(tr, "%08x ", w[i]); fprintf(tr, "\n"); }
	const unsigned char *b = p;
	for (size_t i = 0; i < n; i++)
		pica_sim_hash = (pica_sim_hash ^ b[i]) * 1099511628211ull;
}
void pica_sim_final(void) {}
// a vertex as the PS1 sees it: position, colour, and for texture coordinates
// the texture (page, CLUT, depth, window) and texel they point at, plus the
// decoded texel there; independent of where the atlas keeps squares
static inline u32 tiled(int x, int y, int w);
static void sim_vertex(const pvtx *v)
{
	u32 rec[8] = { (u16)v->x | ((u32)(u16)v->y << 16), v->rgba };
	int u = v->u, w = v->v;
	const tcache *hit = NULL;
	static const int du[4] = { 0, -1, 0, -1 }, dv[4] = { 0, 0, -1, -1 };
	for (int k = 0; k < 4 && !hit; k++) {
		int uu = u + du[k], vv = w + dv[k];
		if (uu >= plain->ax && uu < plain->ax + 32 && vv >= plain->ay && vv < plain->ay + 32) {
			rec[2] = 0xffffffff; rec[3] = (uu - plain->ax) | ((vv - plain->ay) << 16);
			hit = plain;
			u = uu; w = vv;
			break;
		}
		for (int i = 0; i < NENTRIES; i++) {
			const tcache *c = &cache[i];
			if (c->valid && uu >= c->ax && uu < c->ax + 32 * c->cw && vv >= c->ay && vv < c->ay + 32 * c->ch) {
				rec[2] = c->depth | (c->page << 8) | ((u32)c->clut << 16);
				rec[3] = c->win;
				rec[4] = (c->rx + uu - c->ax) | ((c->ry + vv - c->ay) << 16);
				hit = c; u = uu; w = vv;
				break;
			}
		}
	}
	if (hit && u >= 0 && u < atlas[0].w && w >= 0 && w < atlas[0].h)
		rec[5] = ((u32 *)atlas[0].tex.data)[tiled(u, atlas[0].h - 1 - w, atlas[0].w)];
	if (!hit) rec[6] = (u16)v->u | ((u32)(u16)v->v << 16);
	sim_hash(rec, sizeof(rec));
	static FILE *dump;
	if (!dump && getenv("SIMDUMP")) dump = fopen(getenv("SIMDUMP"), "w");
	if (dump) fprintf(dump, "%08x %08x %08x %08x %08x %08x %08x  %d %d\n", rec[0], rec[1], rec[2], rec[3], rec[4], rec[5], rec[6], v->u, v->v);
}
#endif
static tcache *last_tex;             // the previous lookup's result
static bool have_last;                // reserved square: texels for untextured drawing
static tcache *buckets[256];
static u32 lru_clock, lru_frame, lru_prev_frame;   // lru_clock at the last two frame starts

// VRAM write generations in 64x16 blocks: texture pages and CLUTs depend
// on them (CLUTs often sit right next to a framebuffer that is cleared
// every frame, so the blocks must be small)
static u32 vram_gen[32][16];
static u32 gen_clock = 1;
// 8x8 is the native PICA tile. Keep palette rows and framebuffer edges
// independent; the old 64x16 tracker remains available for hardware A/B.
#define RTT_OLD 262144
#define RTT_FORCE_NEW 1048576
static bool rtt_detected, rtt_cache_rebuild;
extern int pica_debug;
static u32 fine_gen[64][128], fine_draw_gen[64][128];
// CPU palette uploads are often adjacent rows in the same native tile.
// Keep their row versions independent from conservative GPU tile bounds.
static u32 cpu_row_gen[512];
static u32 fine_drawn[64][4], fine_snapshot[64][4];
static unsigned st_gpu_tiles, st_gpu_copies;
static bool gpu_readable;
static int known_fx0 = 1, known_fx1, known_fy0, known_fy1;
static int alt_fx0 = 1, alt_fx1, alt_fy0, alt_fy1;   // a second one (rcpu bit 32)
static unsigned st_read_pixels, st_read_bytes;
static inline bool rtt_old(void)
{
	return (pica_debug & RTT_OLD) || (!rtt_detected && !(pica_debug & RTT_FORCE_NEW));
}


// PS1 drawing state (GP0 E1-E6)
static struct {
	u32 texpage;          // E1 bits 0-13
	u32 texwin;
	int dx1, dy1, dx2, dy2;   // drawing area, x2/y2 exclusive
	int ox, oy;               // drawing offset
	u32 mask;                 // E6
	u32 area_gen;             // bumped when the drawing area changes
} ps;

// what the current batch is drawn with
enum { BL_OPAQUE, BL_HALF, BL_ADD, BL_SUB, BL_QUARTER };
enum { TX_NONE, TX_MOD, TX_RAW };
typedef struct {
	int atlas;     // which atlas texture
	u8 blend;      // BL_*
	u8 texmode;    // TX_*
	u8 pass;       // two-pass modes: 0 opaque texels, 1 semi-transparent texels
	u8 dither;     // ordered dither on eligible polygons only
	u8 mask;       // GP0 E6: bit 0 drawn pixels get the mask bit, bit 1 masked pixels are kept
	int sc[4];     // scissor x1, y1, x2, y2 in VRAM coordinates
} drawstate;
static drawstate cur, sent;
static bool sent_valid;
static bool batch_semi;   // any primitive in this batch actually enables blending
static bool batch_untex;  // ...is an untextured effect (rcpu bit 2)
// Additive effects may wait behind later, disjoint solid/half primitives.
// Cached staging avoids reading back the write-only linear vertex buffer.
#define ADD_PRIMS 32
#define ADD_VERTS (ADD_PRIMS * 6)
static struct {
	pvtx verts[ADD_VERTS];
	s16 bounds[ADD_PRIMS][4];
	tcache *tex[ADD_PRIMS];
	int n, nv;
	u32 area_gen;
	bool untex;
	drawstate state;
} addq;
static C3D_Tex tex_alias[2];   // a rebind under a new address clears the GPU texture cache
static int alias_i;

// CPU uploads made while no GPU frame was open (e.g. a state load), pushed
// when the next one opens; a bounding box could cover drawn pixels with
// stale data, so the rectangles are kept (merged only when there are many)
typedef struct { s16 x, y, w, h; } prect;
static prect pend[16];
static int npend;
bool pica_texmask = true, pica_texmemo = true; // named diagnostic A/B switches
int pica_debug;   // set by the frontend (test switch gldbg)
unsigned pica_submit = 7; // named submitstate/submitclip/submittex switches
void (*pica_pmu)(int end); // optional frontend kernel-PMU sampler
unsigned pica_prims = 7;   // named pica_prims_* experiments, separate from gldbg
// Per-primitive experiments; opt in individually or together in the same binary.
#define PRIM_PACK_COPY 1u
#define PRIM_CLIP_PLANES 2u
#define PRIM_VFP_ROUND 4u
#ifdef PICA_PRIMS_FIXED
#define PRIM_TEST(mask) (PICA_PRIMS_FIXED & (mask))
#else
#define PRIM_TEST(mask) (pica_prims & (mask))
#endif
// Renderer CPU experiments (switch rcpu=N, a bit per change) for same-binary A/B.
unsigned pica_rcpu;
#define RCPU_DEFER 1u   // blended passes wait in addq: masked additive, subtract/quarter pass 1
#define RCPU_ADDMASK 2u // additive batches of textured primitives write the mask bit in their one pass
#define RCPU_EVICT 8u   // atlas eviction search by sliding maxima (same choice)
#define RCPU_ROWS 16u   // texture squares start at 8-row alignment below GPU-drawn rows
#define RCPU_KNOWN 32u  // the known drawn rectangle grows to the union of covered drawing
#define RCPU(bit) (pica_rcpu & (bit))
// Set by the frontend: opens a GPU frame (waiting for the previous one to
// finish). Called when the first drawing command of a frame arrives, so the
// GPU finishes the last frame while the game's own code runs.
void (*pica_open_frame)(void);
bool pica_async, pica_double_buffer;
void (*pica_wait_frame)(void);
bool (*pica_frame_pending)(void);
static u64 t_state, t_draw, t_tex, t_list, t_svc;
/* gldbg=8388608: sample each hot phase at 1/16 calls. Inclusive estimates:
 * list contains primitive, texture contains decode, primitive contains state/
 * submission. Low-frequency list/readback boundaries are measured exactly. */
static unsigned phase_random[8] = {1,2,3,4,5,6,7,8};
#ifdef PICA_LAYOUT_SPLIT
static PICA_COLD u64 phase_begin_sample(unsigned k)
{
 if (k != 0 && k != 6) {
  phase_random[k] = phase_random[k] * 1664525u + 1013904223u;
  if (phase_random[k] >> 28) return 0;
 }
 return svcGetSystemTick();
}
static inline u64 phase_begin(unsigned k)
{
 if (PICA_UNLIKELY(pica_debug & 8388608)) return phase_begin_sample(k);
 return 0;
}
static PICA_COLD void phase_end_sample(unsigned k, u64 start)
{
 u64 ticks = svcGetSystemTick() - start;
 ticks = ticks > t_svc ? ticks - t_svc : 0;
 psx3d_skip.phase_ticks[k] += ticks * ((k == 0 || k == 6) ? 1 : 16);
}
static inline void phase_end(unsigned k, u64 start)
{
 if (PICA_UNLIKELY(start != 0)) phase_end_sample(k, start);
}
#else
static u64 phase_begin(unsigned k)
{
 if (!(pica_debug & 8388608)) return 0;
 if (k != 0 && k != 6) {
  phase_random[k] = phase_random[k] * 1664525u + 1013904223u;
  if (phase_random[k] >> 28) return 0;
 }
 return svcGetSystemTick();
}
static void phase_end(unsigned k, u64 start)
{
 if (!start) return;
 u64 ticks = svcGetSystemTick() - start;
 ticks = ticks > t_svc ? ticks - t_svc : 0;
 psx3d_skip.phase_ticks[k] += ticks * ((k == 0 || k == 6) ? 1 : 16);
}
#endif
static unsigned n_list, n_flush;   // diagnostics: ticks in state setup, draw calls, textures
static unsigned st_why[5], st_evict_lg[NSIZES];   // batch ends: atlas, blend/mode, area, rebind, other
static unsigned st_batches, st_prims, st_decodes, st_uploads, st_syncs, st_evict, st_inval;

static void flush(void);
static bool decode_gpu(tcache *c, u64 need);
static void PICA_COLD push_to_target(int x, int y, int w, int h);

// ------------------------------------------------------------------ helpers

#define MIN3(a, b, c) ((a) < (b) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))
#define MAX3(a, b, c) ((a) > (b) ? ((a) > (c) ? (a) : (c)) : ((b) > (c) ? (b) : (c)))

static inline int sext11(u32 v) { return ((s32)(v << 21)) >> 21; }

static void mark_fine(int x, int y, int w, int h, bool drawn)
{
	for (int row=0; row<h; row++) cpu_row_gen[(y+row)&511]=gen_clock;
	for (int yy = 0; yy < h; ) {
		int py = (y + yy) & 511, by = py >> 3;
		for (int xx = 0; xx < w; ) {
			int px = (x + xx) & 1023, bx = px >> 3;
			fine_gen[by][bx] = gen_clock;
			if (drawn) fine_drawn[by][bx >> 5] |= 1u << (bx & 31);
			xx += 8 - (px & 7);
		}
		yy += 8 - (py & 7);
	}
}

static void mark_vram(int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0)
		return;
	gen_clock++;
	if (!rtt_old()) mark_fine(x, y, w, h, false);
	int bx0 = (x & 1023) >> 6, bx1 = ((x + w - 1) & 1023) >> 6;
	int by0 = (y & 511) >> 4, by1 = ((y + h - 1) & 511) >> 4;
	if (h >= 512) { by0 = 0; by1 = 31; }
	if (w >= 1024) { bx0 = 0; bx1 = 15; }
	for (int by = by0;; by = (by + 1) & 31) {
		for (int bx = bx0;; bx = (bx + 1) & 15) {
			vram_gen[by][bx] = gen_clock;
			if (bx == bx1) break;
		}
		if (by == by1) break;
	}
}

// position of texel (x, y) in the GPU's tiled layout: 8x8 tiles, Morton inside
static inline u32 tiled(int x, int y, int w)
{
	u32 m = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
	return ((y >> 3) * (w >> 3) + (x >> 3)) * 64 + m;
}

// PS1 15-bit texel -> RGBA8 (0xRRGGBBAA) with the alpha rules above
static inline u32 texel_rgba(u16 p)
{
	if (!p)
		return 0;
	u32 r = p & 31, g = (p >> 5) & 31, b = (p >> 10) & 31;
	// PS1 modulation operates on 5-bit channels: (tex5 * colour8) / 16
	// before dithering/truncation. Bit replication brightens modulated colours
	// by up to 7/248 and moves different channels across quantization levels.
	// A raw texel modulated by 0x80 still truncates back to its original tex5.
	r <<= 3; g <<= 3; b <<= 3;
	return (r << 24) | (g << 16) | (b << 8) | ((p & 0x8000) ? 128 : 255);
}

// PS1 15-bit pixel -> RGBA5551 for the render target
static inline u16 pixel_5551(u16 p)
{
	return ((p & 31) << 11) | (((p >> 5) & 31) << 6) | (((p >> 10) & 31) << 1) | (p >> 15);
}

typedef u32 __attribute__((may_alias)) u32a;

// ------------------------------------------------------------------ deferred fills

typedef struct { s16 x, y, w, h; u16 col; } pfill;
static pfill fills[32];
static int nfills;

static void apply_fill_reference(const pfill *f)
{
	for (int r = 0; r < f->h; r++) {
		u16 *row = gpu.vram + ((f->y + r) & 511) * VRAM_W;
		for (int k = 0; k < f->w; k++)
			row[(f->x + k) & 1023] = f->col;
	}
}

static void apply_fill(const pfill *f)
{
	// gldbg=131072: baseline renderer CPU paths.
	if (pica_debug & 131072) { apply_fill_reference(f); return; }
	// GP0 fills start and end on 16-pixel boundaries. Split horizontal
	// wrapping once, then fill pairs without a wrap test for every pixel.
	int first = f->w < VRAM_W - f->x ? f->w : VRAM_W - f->x;
	u32 col = f->col | ((u32)f->col << 16);
	for (int r = 0; r < f->h; r++) {
		u32a *row = (u32a *)(gpu.vram + ((f->y + r) & 511) * VRAM_W);
		for (int k = 0; k < first / 2; k++)
			row[f->x / 2 + k] = col;
		for (int k = 0; k < (f->w - first) / 2; k++)
			row[k] = col;
	}
}

static void defer_fill(int x, int y, int w, int h, u16 col)
{
	for (int i = 0; i < nfills; i++)   // the same area again: the old fill no longer matters
		if (fills[i].x == x && fills[i].y == y && fills[i].w == w && fills[i].h == h) {
			fills[i].col = col;
			if (i != nfills - 1) {   // keep the order: it is the newest now
				pfill f = fills[i];
				memmove(&fills[i], &fills[i + 1], (nfills - 1 - i) * sizeof(pfill));
				fills[nfills - 1] = f;
			}
			return;
		}
	if (nfills == 32) {
		apply_fill(&fills[0]);
		memmove(&fills[0], &fills[1], 31 * sizeof(pfill));
		nfills--;
	}
	fills[nfills++] = (pfill){ x, y, w, h, col };
}

// write the pending fills that touch this VRAM rectangle (in order)
static void settle_fills(int x, int y, int w, int h)
{
	int keep = 0;
	for (int i = 0; i < nfills; i++) {
		const pfill *f = &fills[i];
		bool hit = !(f->x >= x + w || f->x + f->w <= x || f->y >= y + h || f->y + f->h <= y);
		if (hit)
			apply_fill(f);
		else
			fills[keep++] = *f;
	}
	nfills = keep;
}

// gpulib reads or writes software VRAM itself (transfers, copies, saving
// a state): the fills there have to be written first
static void pica_settle_rect_local(int x, int y, int w, int h)
{
	if (nfills)
		settle_fills(x, y, w, h);
}

static void pica_settle_vram_local(void)
{
	for (int i = 0; i < nfills; i++)
		apply_fill(&fills[i]);
	nfills = 0;
}

static void pica_drop_fills_local(void);   // a loaded state replaced VRAM (defined below)

// ------------------------------------------------------------------ GPU-drawn VRAM
// What the GPU draws lands only in its render target. These 64x16 blocks
// hold GPU drawing that software VRAM (gpu.vram) hasn't seen yet; before
// anything reads software VRAM there (a texture decode, a CPU read, a VRAM
// copy, a saved state), the blocks are copied back from the render target.

static u16 drawn[32];   // bit bx of row by: block (bx, by) is newer on the GPU
static int known_bx0 = 1, known_bx1 = 0, known_by0, known_by1;   // blocks known to be all marked
static unsigned st_readbacks;

// rcpu bit 32: the known rectangle skips tile bookkeeping for primitives
// inside it, but it was only ever replaced by a bigger one, so drawing into
// the other framebuffer of a double-buffered game, or anywhere outside the
// first big primitive, walked tiles already marked (200-370 times a frame
// in GT2, CTR, FF7, SH). Grow it to the union with a just-marked rectangle
// when all of the union is marked; otherwise grow a second rectangle the
// same way, which takes over when it is as large (the other framebuffer).
// This changes nothing but the shortcut.
static bool all_marked(int x0, int y0, int x1, int y1)
{
	for (int by = y0; by <= y1; by++)
		for (int word = x0 >> 5; word <= (x1 >> 5); word++) {
			int lo = word == (x0 >> 5) ? (x0 & 31) : 0;
			int hi = word == (x1 >> 5) ? (x1 & 31) : 31;
			u32 mask = (~0u << lo) & (~0u >> (31-hi));
			if (mask & (~fine_drawn[by][word] | fine_snapshot[by][word]))
				return false;
		}
	return true;
}

// grow rectangle r (x0, y0, x1, y1; empty when x0 > x1) by a marked one
static bool grow_rect(int *r, int bx0, int by0, int bx1, int by1)
{
	if (r[0] > r[1]) {
		r[0] = bx0; r[1] = bx1; r[2] = by0; r[3] = by1;
		return true;
	}
	int x0 = bx0 < r[0] ? bx0 : r[0], x1 = bx1 > r[1] ? bx1 : r[1];
	int y0 = by0 < r[2] ? by0 : r[2], y1 = by1 > r[3] ? by1 : r[3];
	if (x0 == r[0] && x1 == r[1] && y0 == r[2] && y1 == r[3])
		return true;
	if (!all_marked(x0, y0, x1, y1))
		return false;
	r[0] = x0; r[1] = x1; r[2] = y0; r[3] = y1;
	return true;
}

static PICA_COLD void grow_known(int bx0, int by0, int bx1, int by1)
{
	int k[4] = { known_fx0, known_fx1, known_fy0, known_fy1 };
	int a[4] = { alt_fx0, alt_fx1, alt_fy0, alt_fy1 };
	if (!grow_rect(k, bx0, by0, bx1, by1) && !grow_rect(a, bx0, by0, bx1, by1) &&
		(bx1 - bx0 + 1) * (by1 - by0 + 1) > (a[1] - a[0] + 1) * (a[3] - a[2] + 1)) {
		a[0] = bx0; a[1] = bx1; a[2] = by0; a[3] = by1;
	}
	if (a[0] <= a[1] && (a[1] - a[0] + 1) * (a[3] - a[2] + 1) >= (k[1] - k[0] + 1) * (k[3] - k[2] + 1)) {
		int t[4]; memcpy(t, k, sizeof(t)); memcpy(k, a, sizeof(k)); memcpy(a, t, sizeof(a));
	}
	known_fx0 = k[0]; known_fx1 = k[1]; known_fy0 = k[2]; known_fy1 = k[3];
	alt_fx0 = a[0]; alt_fx1 = a[1]; alt_fy0 = a[2]; alt_fy1 = a[3];
}

// inside the second rectangle: make it the known one
static inline bool in_alt(int bx0, int by0, int bx1, int by1)
{
	if (!RCPU(RCPU_KNOWN) || !(bx0 >= alt_fx0 && bx1 <= alt_fx1 && by0 >= alt_fy0 && by1 <= alt_fy1))
		return false;
	int t;
	t = known_fx0; known_fx0 = alt_fx0; alt_fx0 = t;
	t = known_fx1; known_fx1 = alt_fx1; alt_fx1 = t;
	t = known_fy0; known_fy0 = alt_fy0; alt_fy0 = t;
	t = known_fy1; known_fy1 = alt_fy1; alt_fy1 = t;
	return true;
}

// the GPU is about to draw into pixels x0..x1, y0..y1 (inclusive)
#ifdef PICA_LAYOUT_SPLIT
static PICA_COLD void mark_drawn_fine(int x0, int y0, int x1, int y1)
{

		gpu_readable = false;
		int bx0 = x0 >> 3, bx1 = x1 >> 3, by0 = y0 >> 3, by1 = y1 >> 3;
		if (bx0 >= known_fx0 && bx1 <= known_fx1 && by0 >= known_fy0 && by1 <= known_fy1) return;
		if (in_alt(bx0, by0, bx1, by1)) return;
		// Keep the largest covered rectangle, rather than replacing a
		// framebuffer-sized hit with each tiny sprite outside it.
		if (known_fx0 > known_fx1 || (bx1-bx0+1)*(by1-by0+1) >
			(known_fx1-known_fx0+1)*(known_fy1-known_fy0+1)) {
			known_fx0=bx0; known_fx1=bx1; known_fy0=by0; known_fy1=by1;
		}
		// A CPU-decoded texture has read back (and cleared) its source
		// tiles. Writes to tiles still dirty cannot invalidate a CPU decode.
		u32 stamp = gen_clock + 1;
		bool changed = false;
		for (int by = by0; by <= by1; by++) {
			for (int word = bx0 >> 5; word <= (bx1 >> 5); word++) {
				int lo = word == (bx0 >> 5) ? (bx0 & 31) : 0;
				int hi = word == (bx1 >> 5) ? (bx1 & 31) : 31;
				u32 mask = (~0u << lo) & (~0u >> (31-hi));
				u32 add = mask & (~fine_drawn[by][word] | fine_snapshot[by][word]);
				if (!add) continue;
				fine_snapshot[by][word] &= ~mask;
				fine_drawn[by][word] |= mask;
				changed |= add != 0;
				for (; add; add &= add-1) {
					int bx=word*32+__builtin_ctz(add);
					fine_gen[by][bx] = fine_draw_gen[by][bx] = stamp;
				}
			}
		}
		if (changed) gen_clock = stamp;
		if (RCPU(RCPU_KNOWN)) grow_known(bx0, by0, bx1, by1);
}
#endif

static noinline void PICA_HOT(mark_drawn_slow) mark_drawn_slow(int x0, int y0, int x1, int y1)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > VRAM_W - 1) x1 = VRAM_W - 1;
	if (y1 > VRAM_H - 1) y1 = VRAM_H - 1;
	if (x1 < x0 || y1 < y0)
		return;
	if (!rtt_old()) {
#ifdef PICA_LAYOUT_SPLIT
  mark_drawn_fine(x0, y0, x1, y1);
#else

		gpu_readable = false;
		int bx0 = x0 >> 3, bx1 = x1 >> 3, by0 = y0 >> 3, by1 = y1 >> 3;
		if (bx0 >= known_fx0 && bx1 <= known_fx1 && by0 >= known_fy0 && by1 <= known_fy1) return;
		if (in_alt(bx0, by0, bx1, by1)) return;
		// Keep the largest covered rectangle, rather than replacing a
		// framebuffer-sized hit with each tiny sprite outside it.
		if (known_fx0 > known_fx1 || (bx1-bx0+1)*(by1-by0+1) >
			(known_fx1-known_fx0+1)*(known_fy1-known_fy0+1)) {
			known_fx0=bx0; known_fx1=bx1; known_fy0=by0; known_fy1=by1;
		}
		// A CPU-decoded texture has read back (and cleared) its source
		// tiles. Writes to tiles still dirty cannot invalidate a CPU decode.
		u32 stamp = gen_clock + 1;
		bool changed = false;
		for (int by = by0; by <= by1; by++) {
			for (int word = bx0 >> 5; word <= (bx1 >> 5); word++) {
				int lo = word == (bx0 >> 5) ? (bx0 & 31) : 0;
				int hi = word == (bx1 >> 5) ? (bx1 & 31) : 31;
				u32 mask = (~0u << lo) & (~0u >> (31-hi));
				u32 add = mask & (~fine_drawn[by][word] | fine_snapshot[by][word]);
				if (!add) continue;
				fine_snapshot[by][word] &= ~mask;
				fine_drawn[by][word] |= mask;
				changed |= add != 0;
				for (; add; add &= add-1) {
					int bx=word*32+__builtin_ctz(add);
					fine_gen[by][bx] = fine_draw_gen[by][bx] = stamp;
				}
			}
		}
		if (changed) gen_clock = stamp;
		if (RCPU(RCPU_KNOWN)) grow_known(bx0, by0, bx1, by1);
#endif
		return;
	}
	int bx0 = x0 >> 6, bx1 = x1 >> 6, by0 = y0 >> 4, by1 = y1 >> 4;
	// most primitives land where the previous ones did
	if (bx0 >= known_bx0 && bx1 <= known_bx1 && by0 >= known_by0 && by1 <= known_by1)
		return;
	known_bx0 = bx0; known_bx1 = bx1; known_by0 = by0; known_by1 = by1;
	u16 m = (u16)(((2u << bx1) - 1) & ~((1u << bx0) - 1));
	for (int by = by0; by <= by1; by++) {
		u16 add = m & ~drawn[by];
		if (!add)
			continue;
		// textures decoded from these blocks are out of date from now on
		drawn[by] |= add;
		gen_clock++;
		for (int bx = 0; bx < 16; bx++)
			if (add & (1u << bx))
				vram_gen[by][bx] = gen_clock;
	}
}

// The common covered-rectangle case stays in the primitive hot path. Tile
// bit/version bookkeeping is called only when the bounds grow or move.
static PICA_INLINE_HOT(mark_drawn) void mark_drawn(int x0, int y0, int x1, int y1)
{
	if (!rtt_old()) {
		gpu_readable=false;
		if ((x0>>3)>=known_fx0 && (x1>>3)<=known_fx1 &&
			(y0>>3)>=known_fy0 && (y1>>3)<=known_fy1) return;
	} else if ((pica_debug & 33554432) && x0 >= 0 && y0 >= 0 &&
		x1 < VRAM_W && y1 < VRAM_H &&
		(x0 >> 6) >= known_bx0 && (x1 >> 6) <= known_bx1 &&
		(y0 >> 4) >= known_by0 && (y1 >> 4) <= known_by1) {
		// Coarse mode also has a common already-covered rectangle. Avoid
		// the cold bookkeeping call only when the complete bounds are valid.
		return;
	}
	mark_drawn_slow(x0,y0,x1,y1);
}

enum sync_reason { SY_READ_CPU, SY_READ_TEX, SY_READ_CLUT, SY_EVICT,
	SY_INVALIDATE, SY_GROW, SY_VERTICES, SY_STAGE, SY_COUNT };
static unsigned st_sync_reason[SY_COUNT], st_prev_texture, st_prev_upload;
static enum sync_reason read_reason = SY_READ_CPU;
#ifdef PSX3D_SIM
static FILE *cache_trace_log(void)
{
	static FILE *log;
	static bool checked;
	if (!checked) {
		checked = true;
		if (getenv("PICA_CACHE_TRACE")) log = fopen(getenv("PICA_CACHE_TRACE"), "w");
	}
	return log;
}
static void trace_cache(const char *event, const tcache *c, u64 tiles)
{
	FILE *log = cache_trace_log();
	if (log) fprintf(log, "%s %u %u %u %u %u %u %u %016llx %u\n", event,
		c->depth, c->page, c->clut, c->win, c->lg, c->rx, c->ry,
		(unsigned long long)tiles, gen_clock);
}
static void trace_upload(int x, int y, int w, int h)
{
	FILE *log = cache_trace_log();
	if (log) fprintf(log, "upload %d %d %d %d %u\n", x, y, w, h, gen_clock);
}
#else
#define trace_cache(event,c,tiles) ((void)0)
#define trace_upload(x,y,w,h) ((void)0)
#endif
static void gpu_sync(enum sync_reason why);

// wait until the GPU has run everything queued
static void gpu_idle(void)
{
	if (frame_open) {
		gpu_sync(read_reason);
	} else {
		st_sync_reason[read_reason]++;
		st_syncs++;
		C3D_FrameBegin(0);   // waits for the previous frame
		C3D_FrameEnd(0);
		C3D_FrameBegin(0);   // and for that empty one
		C3D_FrameEnd(0);
	}
}

// Host-only dependency log. Recording does not change synchronization or
// cache policy, so the baseline can be classified before enabling RTT.
#ifdef PSX3D_SIM
static void trace_rtt(const tcache *c, const char *kind, int x, int y, int w, int h)
{
	static FILE *log;
	static bool checked;
	if (!checked) {
		checked = true;
		if (getenv("PICA_RTT_TRACE")) log = fopen(getenv("PICA_RTT_TRACE"), "w");
	}
	if (!log) return;
	unsigned blocks = 0;
	for (int yy = y & ~15; yy < y + h; yy += 16)
		for (int xx = x & ~63; xx < x + w; xx += 64)
			blocks += !!(drawn[(yy >> 4) & 31] & (1u << ((xx >> 6) & 15)));
	fprintf(log, "%s %u %u %u %u %d %d %d %d %u %u %d %d %d %d\n",
		kind, c->depth, c->page, c->clut, c->win, x, y, w, h,
		blocks, gen_clock, gpu.screen.src_x, gpu.screen.src_y, gpu.screen.w, gpu.screen.h, rtt_old() ? "old" : "new");
}
#else
#define trace_rtt(c, kind, x, y, w, h) ((void)0)
#endif

static void PICA_COLD readback_fine(int x, int y, int w, int h)
{
	int x0 = x >> 3, x1 = (x + w - 1) >> 3;
	int y0 = y >> 3, y1 = (y + h - 1) >> 3;
	bool any = false;
	for (int by = y0; by <= y1; by++)
		for (int bx = x0; bx <= x1; bx++)
			any |= !!(fine_drawn[by][bx >> 5] & (1u << (bx & 31)));
	if (!any) return;
	// Fills precede newer GPU work. Settle before reading back, then never
	// let a deferred old fill overwrite the pixels just made CPU-visible.
	if (nfills) settle_fills(x0 * 8, y0 * 8, (x1-x0+1)*8, (y1-y0+1)*8);
	if (!gpu_readable) { gpu_idle(); gpu_readable = true; }
	const u16 *src = vram_tex.data;
	for (int by = y0; by <= y1; by++) {
		int invalidated_until = x0-1;
		for (int bx = x0; bx <= x1; bx++) {
			u32 bit = 1u << (bx & 31);
			if (!(fine_drawn[by][bx >> 5] & bit)) continue;
			// Coalesce adjacent dirty native tiles into one cache IPC.
			if (bx > invalidated_until) {
				int end=bx+1;
				while (end<=x1 && (fine_drawn[by][end>>5] & (1u<<(end&31)))) end++;
				int ty = (pica_debug & 16384) ? 63 - by : by;
				GSPGPU_InvalidateDataCache(src + tiled(bx*8, ty*8, VRAM_W), (end-bx)*128);
				st_read_bytes += (end-bx)*128;
				invalidated_until=end-1;
			}
			for (int r = 0; r < 8; r++) {
				int py = by*8+r;
				u16 *dst = gpu.vram + py*VRAM_W + bx*8;
				for (int c = 0; c < 8; c++) {
					u16 p = src[tiled(bx*8+c, (pica_debug & 16384) ? VRAM_H-1-py : py, VRAM_W)];
					dst[c] = (p >> 11) | (((p >> 6) & 31) << 5) | (((p >> 1) & 31) << 10) | ((p & 1) << 15);
				}
			}
			st_read_pixels += 64;
			fine_drawn[by][bx >> 5] &= ~bit;
		}
	}
	known_fx0 = 1; known_fx1 = 0;
	alt_fx0 = 1; alt_fx1 = 0;
	st_readbacks++;
}

// copy the drawn blocks within this rectangle (no wrapping) to software VRAM
// Enter fine tracking once a real consumer intersects old GPU drawing.
// Drain readers before changing cache generations; expand the conservative
// old debt into native tiles so the first feedback read cannot lose pixels.
static void PICA_COLD activate_rtt(void)
{
	if (!rtt_old() || (pica_debug & RTT_OLD)) return;
	gpu_idle();
	gen_clock++;
	for (int y = 0; y < 64; y++) {
		for (int x = 0; x < 128; x++) {
			bool debt = drawn[y >> 1] & (1u << (x >> 3));
			fine_gen[y][x] = gen_clock;
			fine_draw_gen[y][x] = debt ? gen_clock : 0;
			if (debt) fine_drawn[y][x >> 5] |= 1u << (x & 31);
		}
	}
	for (int y = 0; y < 512; y++) cpu_row_gen[y] = gen_clock;
	// Keep allocations and texels: a decode can be in progress. All entries
	// revalidate against the seeded generations on their next lookup.
	for (unsigned i = 0; i < sizeof(cache)/sizeof(cache[0]); i++)
		cache[i].stamp = cache[i].checked = 0;
	known_fx0 = 1; known_fx1 = 0;
	alt_fx0 = 1; alt_fx1 = 0;
	have_last = false;
	gpu_readable = false;
	rtt_detected = true;
	rtt_cache_rebuild = true;
}

static void PICA_COLD readback_part_impl(int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0 || !ready)
		return;
	if (!rtt_old()) { readback_fine(x, y, w, h); return; }
	int bx0 = x >> 6, bx1 = (x + w - 1) >> 6, by0 = y >> 4, by1 = (y + h - 1) >> 4;
	u16 m = (u16)(((2u << bx1) - 1) & ~((1u << bx0) - 1));
	bool any = false;
	for (int by = by0; by <= by1; by++)
		any |= (drawn[by] & m) != 0;
	if (!any)
		return;
	if (!(pica_debug & RTT_OLD)) {
		activate_rtt();
		readback_fine(x, y, w, h);
		return;
	}
	// pending fills there are in the render target already; written to
	// software VRAM later they would cover newer drawing
	if (nfills)
		settle_fills(bx0 * 64, by0 * 16, (bx1 - bx0 + 1) * 64, (by1 - by0 + 1) * 16);
	gpu_idle();
	const u16 *src = vram_tex.data;
#ifdef PSX3D_SIM
	unsigned changed=0, pixels=0;
#endif
	GSPGPU_InvalidateDataCache(src, VRAM_W * VRAM_H * 2);
	st_read_bytes += VRAM_W * VRAM_H * 2;
	for (int by = by0; by <= by1; by++) {
		u16 bits = drawn[by] & m;
		if (!bits)
			continue;
		for (int bx = bx0; bx <= bx1; bx++) {
			if (!(bits & (1u << bx)))
				continue;
			st_read_pixels += 1024;
			for (int r = 0; r < 16; r++) {
				int py = by * 16 + r;
				u16 *dst = gpu.vram + py * VRAM_W + bx * 64;
				for (int c = 0; c < 64; c++) {
					// render target memory holds VRAM rows top-down (the
					// display samples VRAM row y at t = 1 - y / 512)
					u16 p = src[tiled(bx * 64 + c, (pica_debug & 16384) ? VRAM_H - 1 - py : py, VRAM_W)];
					// RGBA5551 -> PS1 15-bit, the alpha bit is the mask bit
					u16 value = (p >> 11) | (((p >> 6) & 31) << 5) | (((p >> 1) & 31) << 10) | ((p & 1) << 15);
#ifdef PSX3D_SIM
					changed += value != dst[c]; pixels++;
#endif
					dst[c] = value;
				}
			}
		}
		drawn[by] &= ~bits;
	}
	known_bx0 = 1; known_bx1 = 0;   // forget the shortcut
	st_readbacks++;
#ifdef PSX3D_SIM
	if (getenv("PICA_RTT_TRACE"))
		fprintf(stderr,"RTT readback %d %d %d %d pixels %u changed %u\n",x,y,w,h,pixels,changed);
#endif
}

static void readback_part(int x, int y, int w, int h)
{
 u64 started = phase_begin(6);
 readback_part_impl(x, y, w, h);
 phase_end(6, started);
}


// copy GPU drawing in this VRAM rectangle (wrapping at the edges) to software VRAM
static void pica_readback_rect_local(int x, int y, int w, int h)
{
	x &= VRAM_W - 1;
	y &= VRAM_H - 1;
	if (w > VRAM_W) w = VRAM_W;
	if (h > VRAM_H) h = VRAM_H;
	int w1 = x + w > VRAM_W ? VRAM_W - x : w, h1 = y + h > VRAM_H ? VRAM_H - y : h;
	readback_part(x, y, w1, h1);
	if (w1 < w) readback_part(0, y, w - w1, h1);
	if (h1 < h) readback_part(x, 0, w1, h - h1);
	if (w1 < w && h1 < h) readback_part(0, 0, w - w1, h - h1);
}

static void pica_readback_all_local(void)
{
	readback_part(0, 0, VRAM_W, VRAM_H);
}

// test: all of VRAM, GPU drawing included, to a file (compared against the
// software renderer's VRAM in the PC simulator)
static void pica_dump_vram_local(const char *path)
{
	pica_readback_all_local();
	for (int i = 0; i < nfills; i++)
		apply_fill(&fills[i]);
	nfills = 0;
	FILE *f = fopen(path, "wb");
	if (f) {
		fwrite(gpu.vram, 2, VRAM_W * VRAM_H, f);
		fclose(f);
	}
}

// ------------------------------------------------------------------ GPU frame / sync

static void pica_frame_started_local(void)
{
	frame_open = true;
	gpu_readable = false;
	base_dirty = state_dirty = true;
	sent_valid = false;
	vpos = vstart = sent_vpos = vclean = 0;
	addq.n = addq.nv = 0;
	frame_seq = seq;
	lru_prev_frame = lru_frame;
	lru_frame = lru_clock;
	if (!pica_async || !pica_frame_pending || !pica_frame_pending()) synced_seq = seq;
	if (vbuf_base) {
		vlimit = pica_double_buffer ? MAX_VERTS / 2 : MAX_VERTS;
		vbank = pica_async ? (vbank ^ 1) : 0;
		vbuf = vbuf_base + vbank * vlimit;
	}
	batch_semi = batch_untex = false;
	stage_y = 0;
	if (npend) {
		prect p[16];
		int n = npend;
		memcpy(p, pend, n * sizeof(prect));
		npend = 0;
		for (int i = 0; i < n; i++)
			push_to_target(p[i].x, p[i].y, p[i].w, p[i].h);
	}
}

static void pica_frame_ended_local(void)
{
	frame_open = false;
}
void pica_state_lost(void) { base_dirty = state_dirty = true; sent_valid = false; }

// let the GPU run everything queued and wait, so memory it was going to
// read (a texture, the upload stage) can be rewritten in place
static void gpu_sync(enum sync_reason why)
{
	flush();
	if (!frame_open)
		return;
	C3D_FrameEnd(0);
	C3D_FrameBegin(0);
	st_syncs++;
	st_sync_reason[why]++;
	pica_async = false;   // FrameBegin(0) just retired all submitted work
	pica_frame_started_local();
}

// ------------------------------------------------------------------ texture cache

// newest write among nbx x nby blocks starting at block (bx, by)
static u32 block_stamp(int bx, int by, int nbx, int nby)
{
	u32 s = 0;
	for (int j = 0; j < nby; j++)
		for (int i = 0; i < nbx; i++) {
			u32 g = vram_gen[(by + j) & 31][(bx + i) & 15];
			if (g > s) s = g;
		}
	return s;
}

static bool cache_valid_reference(tcache *c)
{
	if (c->checked == gen_clock)   // nothing written since the last check
		return true;
	int px = c->page & 15, py = (c->page >> 4) & 1;
	int nb = c->depth == 0 ? 1 : c->depth == 1 ? 2 : 4;
	if (block_stamp(px, py * 16, nb, 16) > c->stamp)
		return false;
	if (c->depth < 2) {
		int cx = (c->clut & 0x3f) * 16, cy = (c->clut >> 6) & 511;
		int cw = c->depth == 0 ? 16 : 256;
		if (block_stamp(cx >> 6, cy >> 4, ((cx + cw - 1) >> 6) - (cx >> 6) + 1, 1) > c->stamp)
			return false;
	}
	c->checked = gen_clock;
	return true;
}

static u32 fine_stamp(int x, int y, int w, int h)
{
	u32 stamp = 0;
	for (int yy = y & ~7; yy < y + h; yy += 8)
		for (int xx = x & ~7; xx < x + w; xx += 8) {
			u32 g = fine_gen[(yy >> 3) & 63][(xx >> 3) & 127];
			if (g > stamp) stamp = g;
		}
	return stamp;
}


static u32 palette_stamp(int x, int y, int w)
{
	u32 stamp=0, cpu_tiles=0;
	for (int xx=x; xx<x+w; xx+=8) {
		int bx=(xx>>3)&127;
		u32 g=fine_draw_gen[y>>3][bx];
		if (g>stamp) stamp=g;
		if (fine_gen[y>>3][bx]>cpu_tiles) cpu_tiles=fine_gen[y>>3][bx];
	}
	// Both row and tile must have changed for a CPU write to matter.
	// Taking their minimum can conservatively over-invalidate, never miss
	// an intersecting write, without a large per-pixel generation map.
	u32 cpu=cpu_row_gen[y]<cpu_tiles ? cpu_row_gen[y] : cpu_tiles;
	return cpu>stamp ? cpu : stamp;
}

static bool cache_valid_fine(tcache *c)
{
	if (c->checked == gen_clock) return true;
	u64 old = c->tiles;
	if (c->depth < 2 && palette_stamp((c->clut & 63)*16, (c->clut >> 6)&511, c->depth ? 256 : 16) > c->stamp) {
		trace_cache("palette", c, old);
		c->tiles = 0;
	} else {
		int px = (c->page & 15)*64, py = (c->page >> 4)*256, sh = 2-c->depth;
		if (c->win) {
			if (fine_stamp(px, py, 256 >> sh, 256) > c->stamp) c->tiles = 0;
		} else {
			for (u64 bits = c->tiles; bits; bits &= bits-1) {
				int t = __builtin_ctzll(bits);
				if (fine_stamp(px + ((c->rx+(t&7)*32) >> sh), py+c->ry+(t>>3)*32, 32 >> sh, 32) > c->stamp)
					c->tiles &= ~(1ull << t);
			}
		}
		if (c->tiles != old) trace_cache("source", c, old & ~c->tiles);
	}
	c->stamp = c->checked = gen_clock;
	return c->tiles == old;
}

static bool PICA_COLD cache_valid(tcache *c)
{
	if (!rtt_old()) return cache_valid_fine(c);
	// gldbg=131072: baseline renderer CPU paths.
	if (pica_debug & 131072) { return cache_valid_reference(c); }
	if (c->checked == gen_clock)   // nothing written since the last check
		return true;
	int px = (c->page & 15) * 64, py = ((c->page >> 4) & 1) * 256;
	int sh = 2 - c->depth, w = 256 >> sh, h = 256;
	// Without a texture window, only this cached square can be read.
	// Writes elsewhere in the page must not invalidate its decoded tiles.
	if (!c->win) {
		px += c->rx >> sh;
		py += c->ry;
		h = 32 * c->ch;
		w = (32 * c->cw) >> sh;
	}
	int nbx = ((px + w - 1) >> 6) - (px >> 6) + 1;
	if (block_stamp(px >> 6, py >> 4, nbx, h >> 4) > c->stamp)
		return false;
	if (c->depth < 2) {
		int cx = (c->clut & 0x3f) * 16, cy = (c->clut >> 6) & 511;
		int cw = c->depth == 0 ? 16 : 256;
		if (block_stamp(cx >> 6, cy >> 4, ((cx + cw - 1) >> 6) - (cx >> 6) + 1, 1) > c->stamp)
			return false;
	}
	c->checked = gen_clock;
	return true;
}

static inline int key_hash(int depth, int page, int clut, u32 win)
{
	return ((depth * 0x9e3779b1u) ^ (page * 0x85ebca6bu) ^ (clut * 0xc2b2ae35u) ^ (win * 0x27d4eb2fu)) >> 24;
}

static void unhash(tcache *c)
{
	tcache **pp = &buckets[key_hash(c->depth, c->page, c->clut, c->win)];
	while (*pp && *pp != c)
		pp = &(*pp)->next;
	if (*pp)
		*pp = c->next;
	c->next = NULL;
}

// decode the parts of the page rectangle u0..u1 x v0..v1 not decoded yet
// write CPU-cached data out to memory for the GPU (a plain kernel call; no
// inter-process call like GSPGPU_FlushDataCache, and no kernel backdoor: a
// whole-cache clean through svcBackdoor crashed the New 3DS)
static void flush_range(const void *p, u32 n)
{
#ifdef PSX3D_SIM
	GSPGPU_FlushDataCache(p, n);
#else
	svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)p, n);
#endif
}

// Vertices are append-only between GPU synchronizations. Flush the newly
// written suffix once per submission, rather than inspecting every draw.
void pica_flush_vertices(void)
{
	if (vpos > vclean) {
		flush_range(vbuf + vclean, (vpos - vclean) * sizeof(pvtx));
		vclean = vpos;
	}
}

// Only called for missing tiles. Keep the 1KB palette and decoder register
// saves off the overwhelmingly common decoded-texture hit path.
static void PICA_COLD decode_rect_impl(tcache *c, u64 need, unsigned rect)
{
	int t0=rect&7, t1=(rect>>3)&7, s0=(rect>>6)&7, s1=(rect>>9)&7;
	if (pica_debug & 131072) {
		need = 0;
		for (int r=s0; r<=s1; r++)
			for (int k=t0; k<=t1; k++) need |= 1ull << (r*8+k);
		need &= ~c->tiles;
	} else if (!pica_texmask) {
		u64 columns = ((1u << (t1-t0+1))-1) << t0;
		need = 0;
		for (int r=s0; r<=s1; r++) need |= columns << (r*8);
		need &= ~c->tiles;
	}
	if (!need) return;
	if (!(pica_debug & 131072) && !rtt_old() && c->depth == 2 && !c->win && decode_gpu(c, need)) return;
	trace_cache("decode", c, need);
	u32 *dst = atlas[0].tex.data;
	int aw = atlas[0].w, ah = atlas[0].h;
	const u16 *vram = gpu.vram;
	int px = (c->page & 15) * 64, py = ((c->page >> 4) & 1) * 256;
	{
		// the VRAM the texels needed come from (the window can fetch from
		// anywhere in the page), and the CLUT: GPU drawing there
		// (render-to-texture) and pending fills have to reach software VRAM
		int sh = 2 - c->depth;   // texels per VRAM word: 4, 2, 1
		int lo = c->rx + t0 * 32, hi = c->rx + t1 * 32 + 31;
		int vx = px, vy = py, vw = 256 >> sh, vh = 256;
		if (!c->win) {
			vx = px + (lo >> sh);
			vy = py + c->ry + s0 * 32;
			vw = (hi >> sh) - (lo >> sh) + 1;
			vh = (s1 - s0 + 1) * 32;
		}
		int cx = (c->clut & 0x3f) * 16, cy = (c->clut >> 6) & 511, cw = c->depth == 0 ? 16 : 256;
		trace_rtt(c, "tex", vx, vy, vw, vh);
		read_reason = SY_READ_TEX;
		pica_readback_rect_local(vx, vy, vw, vh);
		read_reason = SY_READ_CPU;
		if (c->depth < 2) {
			trace_rtt(c, "clut", cx, cy, cw, 1);
			read_reason = SY_READ_CLUT;
			pica_readback_rect_local(cx, cy, cw, 1);
			read_reason = SY_READ_CPU;
		}
		// (settling the whole page wrote CTR's screen clears out every frame)
		if (nfills) {
			settle_fills(vx, vy, vw, vh);
			if (c->depth < 2)
				settle_fills(cx, cy, cw, 1);
		}
	}
	u32 pal[256];
	if (c->depth < 2) {
		int cx = (c->clut & 0x3f) * 16, cy = (c->clut >> 6) & 511;
		int n = c->depth == 0 ? 16 : 256;
		const u16 *crow = vram + cy * VRAM_W;
		for (int i = 0; i < n; i++)
			pal[i] = texel_rgba(crow[(cx + i) & 1023]);
	}
	// the atlas is 8x8 tiles of 64 texels in Z order: offsets of the 32
	// texels of a block row from its first, and of the row within a tile
	static const u16 xoff[32] = {
		0, 1, 4, 5, 16, 17, 20, 21, 64, 65, 68, 69, 80, 81, 84, 85,
		128, 129, 132, 133, 144, 145, 148, 149, 192, 193, 196, 197, 208, 209, 212, 213 };
	static const u8 yoff[8] = { 0, 2, 8, 10, 32, 34, 40, 42 };
	for (int t = 0; t < 64; t++) {
		if (!(need & (1ull << t)))
			continue;
		int lu = (t & 7) * 32, lv = (t >> 3) * 32;           // inside the square
		int tu = c->rx + lu;                                  // in the page
		int au = c->ax + lu;                                  // in the atlas
		for (int y = 0; y < 32; y++) {
			int v = c->ry + lv + y;
			// texture memory runs bottom-up: atlas row r is sampled at t = (r + 0.5) / height
			int ty = ah - 1 - (c->ay + lv + y);
			u32 *d = dst + ((ty >> 3) * (aw >> 3) + (au >> 3)) * 64 + yoff[ty & 7];
			if (c->win) {
				// texture window: page texel (u, v) shows texel
				// ((u & ~mask) | (offset & mask), same for v)
				u32 w = c->win;
				int mx = (w & 0x1f) * 8, my = ((w >> 5) & 0x1f) * 8;
				int ox = ((w >> 10) & 0x1f) * 8 & mx, oy = ((w >> 15) & 0x1f) * 8 & my;
				int wv = ((v & ~my) | oy) & 255;
				const u16 *row = vram + ((py + wv) & 511) * VRAM_W;
				for (int x = 0; x < 32; x++) {
					int wu = (((tu + x) & ~mx) | ox) & 255;
					u32 texel;
					if (c->depth == 0)
						texel = pal[(row[(px + wu / 4) & 1023] >> ((wu & 3) * 4)) & 15];
					else if (c->depth == 1)
						texel = pal[(row[(px + wu / 2) & 1023] >> ((wu & 1) * 8)) & 255];
					else
						texel = texel_rgba(row[(px + wu) & 1023]);
					d[xoff[x]] = texel;
				}
				continue;
			}
			const u16 *row = vram + ((py + v) & 511) * VRAM_W;
			if (c->depth == 0) {
				const u16 *s = row + ((px + tu / 4) & 1023);   // 8 words, inside one page row
				for (int x = 0; x < 8; x++) {
					u32 w = s[x];
					u32 *dd = d + xoff[x * 4];   // 4 texels: offsets +0 +1 +4 +5
					dd[0] = pal[w & 15];
					dd[1] = pal[(w >> 4) & 15];
					dd[4] = pal[(w >> 8) & 15];
					dd[5] = pal[w >> 12];
				}
			} else if (c->depth == 1) {
				for (int x = 0; x < 16; x++) {
					u32 w = row[(px + tu / 2 + x) & 1023];
					u32 *dd = d + xoff[x * 2];   // 2 texels: +0 +1
					dd[0] = pal[w & 255];
					dd[1] = pal[w >> 8];
				}
			} else {
				for (int x = 0; x < 32; x++)
					d[xoff[x]] = texel_rgba(row[(px + tu + x) & 1023]);
			}
		}
		st_decodes++;
	}
	// the GPU reads the atlas from memory: write the decoded texels out of
	// the CPU cache. A 32x32 block is 4 runs of 1KB (4 tiles of an 8-row
	// band); neighbouring blocks of a row continue those runs.
	for (int r = 0; r < 8; r++) {
		u32 row = (u32)(need >> (r * 8)) & 0xff;
		while (row) {
			int k0 = __builtin_ctz(row), k1 = k0;
			while (k1 < 7 && (row >> (k1 + 1)) & 1)
				k1++;
			row &= ~(((2u << k1) - 1) & ~((1u << k0) - 1));
			int ty0 = ah - 1 - (c->ay + r * 32 + 31);   // the lowest atlas row of the blocks
			for (int band = 0; band < 4; band++)
				flush_range(dst + (((ty0 >> 3) + band) * (aw >> 3) + ((c->ax + k0 * 32) >> 3)) * 64,
					(k1 - k0 + 1) * 1024);   // 4 tiles x 64 texels x 4 bytes per block
		}
	}
	c->tiles |= need;
	atlas[0].rebind = true;
}

static void decode_rect(tcache *c, u64 need, unsigned rect)
{
 u64 started = phase_begin(3);
 decode_rect_impl(c, need, rect);
 phase_end(3, started);
}


static void cells_rect(int cx, int cy, int w, int h, s16 v)
{
	for (int y = cy; y < cy + h; y++) {
		for (int x = cx; x < cx + w; x++) {
			s16 *o = &cell_owner[y * CELLS + x];
			*o = v;
		}
		u32 bits = (w == 32 ? ~0u : (1u << w) - 1) << cx;
		cell_rows[y] = v == CELL_FREE ? cell_rows[y] & ~bits : cell_rows[y] | bits;
	}
}
static void cells_set(int cx, int cy, int n, s16 v) { cells_rect(cx, cy, n, n, v); }

static void texture_idle(tcache *c, enum sync_reason why)
{
	if (c->used_seq <= synced_seq) return;
	if (pica_async && c->used_seq <= frame_seq && pica_wait_frame) {
		pica_wait_frame();
		st_prev_texture++;
		synced_seq = frame_seq;
	} else {
		gpu_sync(why);
	}
}

// forget an entry and free its cells (the caller syncs if queued draws use it)
static u32 texmemo_epoch = 1;
static void drop_entry(tcache *c)
{
	texmemo_epoch++;
	unhash(c);
	c->valid = false;
	cells_rect(c->ax >> 5, c->ay >> 5, c->cw, c->ch, CELL_FREE);
	int last = active[c->lg][--nactive[c->lg]];
	active[c->lg][c->slot] = last;
	cache[last].slot = c->slot;
	free_list[nfree++] = c - cache;
}

static void evict(tcache *c)
{
	st_evict++;
	st_evict_lg[c->lg]++;
	trace_cache("evict", c, c->tiles);
	texture_idle(c, SY_EVICT);
	drop_entry(c);
}

// The rectangle of whole cells holding u0..u1 x v0..v1 (inside the page).
// Squares rounded up to 32 << lg wasted half the atlas on 3D scenes.
static void rect_for(int u0, int u1, int v0, int v1, int *rx, int *ry, int *cw, int *ch)
{
	*rx = u0 & ~31;
	*ry = v0 & ~31;
	*cw = (u1 >> 5) - (u0 >> 5) + 1;
	*ch = (v1 >> 5) - (v0 >> 5) + 1;
}

// A new entry of cw x ch cells: free space if there is any (large ones from
// the bottom, small ones from the top, keeping large free areas together),
// else the window whose newest occupant is oldest, preferring one no queued
// draw reads. Evicting only the same size froze each size's share of a full
// atlas: stale squares of one size were never reused while another size
// thrashed, and synced, for the rest of the session.
static PICA_COLD tcache *take_rect(int cw, int ch)
{
	int bx = -1, by = -1;
	bool big = cw * ch >= 8;
	for (int i = 0; i <= CELLS - ch && bx < 0; i++) {
		int y = big ? CELLS - ch - i : i;
		u32 m = 0;
		for (int k = 0; k < ch; k++)
			m |= cell_rows[y + k];
		u32 f = ~m;
		for (int k = 1; k < cw; k++)
			f &= ~m >> k;   // bit x: cells x..x+cw-1 are free
		if (f) { bx = __builtin_ctz(f); by = y; }
	}
	if (bx < 0 && RCPU(RCPU_EVICT)) {
		// The same window, found cheaply: each cell's value once (from the
		// entries in use, each read once: their cells are exactly theirs),
		// then maxima over cw columns and ch rows by doubling spans in place
		// (a window of w is the maximum of two overlapping power-of-two
		// spans), then the first smallest window in the same scan order.
		static u64 m[CELLS][CELLS];
		memset(m, 0, sizeof(m));
		m[plain->ay >> 5][plain->ax >> 5] = ~0ull;
		for (int lg = 0; lg < NSIZES; lg++)
			for (int i = 0; i < nactive[lg]; i++) {
				const tcache *o = &cache[active[lg][i]];
				u64 v = ((u64)(o->used_seq > synced_seq) << 32) | o->lru;
				for (int y = o->ay >> 5; y < (o->ay >> 5) + o->ch; y++)
					for (int x = o->ax >> 5; x < (o->ax >> 5) + o->cw; x++)
						m[y][x] = v;
			}
		int p = 1;
		for (; 2 * p <= cw; p *= 2)
			for (int y = 0; y < CELLS; y++)
				for (int x = 0; x + 2 * p <= CELLS; x++)
					if (m[y][x + p] > m[y][x]) m[y][x] = m[y][x + p];
		if (p < cw)
			for (int y = 0; y < CELLS; y++)
				for (int x = 0; x + cw <= CELLS; x++)
					if (m[y][x + cw - p] > m[y][x]) m[y][x] = m[y][x + cw - p];
		for (p = 1; 2 * p <= ch; p *= 2)
			for (int y = 0; y + 2 * p <= CELLS; y++)
				for (int x = 0; x + cw <= CELLS; x++)
					if (m[y + p][x] > m[y][x]) m[y][x] = m[y + p][x];
		if (p < ch)
			for (int y = 0; y + ch <= CELLS; y++)
				for (int x = 0; x + cw <= CELLS; x++)
					if (m[y + ch - p][x] > m[y][x]) m[y][x] = m[y + ch - p][x];
		u64 best = ~0ull;
		for (int y = 0; y + ch <= CELLS; y++)
			for (int x = 0; x + cw <= CELLS; x++)
				if (m[y][x] < best) { best = m[y][x]; bx = x; by = y; }
		for (int y = by; y < by + ch; y++)
			for (int x = bx; x < bx + cw; x++)
				if (cell_owner[y * CELLS + x] >= 0)
					evict(&cache[cell_owner[y * CELLS + x]]);
	} else if (bx < 0) {
		// row maxima over cw cells, then the best window over ch rows
		static u64 hmax[CELLS * CELLS];
		for (int y = 0; y < CELLS; y++)
			for (int x = 0; x + cw <= CELLS; x++) {
				u64 m = 0;
				for (int k = 0; k < cw; k++) {
					int o = cell_owner[y * CELLS + x + k];
					u64 v = o == CELL_PLAIN ? ~0ull : o < 0 ? 0 :
						((u64)(cache[o].used_seq > synced_seq) << 32) | cache[o].lru;
					if (v > m) m = v;
				}
				hmax[y * CELLS + x] = m;
			}
		u64 best = ~0ull;
		for (int y = 0; y + ch <= CELLS; y++)
			for (int x = 0; x + cw <= CELLS; x++) {
				u64 m = 0;
				for (int k = 0; k < ch && m < best; k++)
					if (hmax[(y + k) * CELLS + x] > m) m = hmax[(y + k) * CELLS + x];
				if (m < best) { best = m; bx = x; by = y; }
			}
		for (int y = by; y < by + ch; y++)
			for (int x = bx; x < bx + cw; x++)
				if (cell_owner[y * CELLS + x] >= 0)
					evict(&cache[cell_owner[y * CELLS + x]]);
	}
	int lg = cw > ch ? cw : ch;
	lg = lg <= 1 ? 0 : lg <= 2 ? 1 : lg <= 4 ? 2 : 3;
	int e = free_list[--nfree];
	tcache *c = &cache[e];
	memset(c, 0, sizeof(*c));
	c->ax = bx * 32;
	c->ay = by * 32;
	c->lg = lg;
	c->cw = cw;
	c->ch = ch;
	c->slot = nactive[lg];
	active[lg][nactive[lg]++] = e;
	cells_rect(bx, by, cw, ch, e);
	return c;
}

static inline bool region_cache(void);

// rcpu bit 16: a square starting 32-row aligned can take in rows above the
// request that the GPU draws (CTR keeps textures right below its 216-row
// framebuffer). Those rows then invalidated it every frame and made each
// decode read the framebuffer back, a full GPU wait, for texels never drawn
// with. Start it at the request's 8-row tile instead when that leaves such
// rows out and the square still ends inside the page.
static void avoid_drawn_rows(int depth, u16 pg, int v0, int v1, int rx, int cw, int *ry, int *ch)
{
	int r8 = v0 & ~7, c8 = ((v1 - r8) >> 5) + 1;
	if (r8 == *ry || r8 + 32 * c8 > 256)
		return;
	int sh = 2 - depth, px = (pg & 15) * 64, py = (pg >> 4) * 256;
	int x0 = (px + (rx >> sh)) >> 3, x1 = (px + ((rx + 32 * cw - 1) >> sh)) >> 3;
	for (int y = (py + *ry) >> 3; y < (py + r8) >> 3; y++)
		for (int x = x0; x <= x1; x++)
			if (fine_draw_gen[y & 63][x & 127]) {
				*ry = r8;
				*ch = c8;
				return;
			}
}

// a new entry for u0..u1 x v0..v1 of the page
static tcache *take_entry(int depth, u16 pg, int u0, int u1, int v0, int v1, int *rx, int *ry)
{
	int cw, ch;
	rect_for(u0, u1, v0, v1, rx, ry, &cw, &ch);
	if (RCPU(RCPU_ROWS) && region_cache())
		avoid_drawn_rows(depth, pg, v0, v1, *rx, cw, ry, &ch);
	return take_rect(cw, ch);
}

// Several disjoint sprites may share a page/CLUT. Growing their union to
// 256x256 throws away decoded cells, wastes the atlas and waits for queued
// readers. Keep independent, tightly bounded squares for those regions.
static inline bool region_cache(void) { return !rtt_old() && !(pica_debug & 524288); }
static inline bool covers(const tcache *c, int u0, int u1, int v0, int v1)
{
	return u0 >= c->rx && u1 < c->rx + 32 * c->cw && v0 >= c->ry && v1 < c->ry + 32 * c->ch;
}

// A small direct-mapped memo of successful requests. A matching VRAM
// generation and cache epoch prove the decoded rectangle is still alive.
// Raw page/CLUT/window keys may conservatively miss, never alias textures.
static struct {
	u32 key, win, generation, epoch;
	tcache *tex;
	int u0, u1, v0, v1;
} texmemo[32];

#ifdef PICA_LAYOUT_SPLIT
static PICA_COLD tcache *texture_new(int depth, u16 pg, u16 cl, u32 win, int u0, int u1, int v0, int v1)
{
 tcache *c;
		int rx, ry;
		c = take_entry(depth, pg, u0, u1, v0, v1, &rx, &ry);
		c->gpu_tiles = false;
		c->depth = depth;
		c->page = pg;
		c->clut = cl;
		c->win = win;
		c->rx = rx;
		c->ry = ry;
		c->tiles = 0;
		c->valid = true;
		c->stamp = c->checked = gen_clock;
		int h = key_hash(depth, pg, cl, win);
		c->next = buckets[h];
		buckets[h] = c;
 return c;
}
#endif

// the texture for drawing texels u0..u1 x v0..v1 (inclusive; past 255 they
// wrap, then the whole page is used) of this page and CLUT
static noinline tcache *PICA_HOT(texture_for_slow) texture_for_slow(u32 page, u32 clut, int u0, int u1, int v0, int v1)
{
	if (rtt_cache_rebuild) {
		// Activation can happen inside decode_rect. Wait until the next
		// lookup to retire old grown squares, after draining their readers.
		// Otherwise their oversized allocations can keep the region cache
		// thrashing long after the one-time transition (GT2).
		gpu_idle();
		for (unsigned i = 0; i < sizeof(cache)/sizeof(cache[0]); i++)
			if (cache[i].valid) drop_entry(&cache[i]);
		have_last = false;
		rtt_cache_rebuild = false;
	}
	int depth = (page >> 7) & 3;
	if (depth == 3)
		depth = 2;
	u16 pg = page & 0x1f;
	u16 cl = depth < 2 ? (clut & 0x7fff) : 0;
	u32 win = (ps.texwin & 0x3ff) ? (ps.texwin & 0xfffff) : 0;   // only when it masks anything
	if (u1 > 255 || v1 > 255 || u0 < 0 || v0 < 0) {
		u0 = 0; u1 = 255; v0 = 0; v1 = 255;
	}
	tcache *c = have_last ? last_tex : NULL;
	if (!(c && c->valid && c->depth == depth && c->page == pg && c->clut == cl && c->win == win)) {
		for (c = buckets[key_hash(depth, pg, cl, win)]; c; c = c->next)
			if (c->depth == depth && c->page == pg && c->clut == cl && c->win == win)
				break;
	}

	if (c) {
		int sw = 32 * c->cw, sh = 32 * c->ch;
		if (u0 < c->rx || u1 >= c->rx + sw || v0 < c->ry || v1 >= c->ry + sh) {
			if (region_cache()) {
				// The common covering hit keeps the original lookup path.
				// Only a region miss searches for another square of this key.
				for (c = buckets[key_hash(depth, pg, cl, win)]; c; c = c->next)
					if (c->depth == depth && c->page == pg && c->clut == cl && c->win == win &&
						covers(c, u0, u1, v0, v1)) break;
			} else {
				// outside the square: move to one that holds both
				int nu0 = u0 < c->rx ? u0 : c->rx, nu1 = u1 > c->rx + sw - 1 ? u1 : c->rx + sw - 1;
				int nv0 = v0 < c->ry ? v0 : c->ry, nv1 = v1 > c->ry + sh - 1 ? v1 : c->ry + sh - 1;
				u0 = nu0; u1 = nu1; v0 = nv0; v1 = nv1;
				trace_cache("grow", c, c->tiles);
				texture_idle(c, SY_GROW);
				drop_entry(c);
				c = NULL;
			}
		}
	}
	if (c && !cache_valid(c)) {
		if (!(c->gpu_tiles && !rtt_old() && atlas_target && frame_open)) texture_idle(c, SY_INVALIDATE);
		if (rtt_old()) c->tiles = 0;
		c->stamp = c->checked = gen_clock;
		trace_cache("invalidate", c, c->tiles);
		st_inval++;
	}
	if (!c) {
#ifdef PICA_LAYOUT_SPLIT
  c = texture_new(depth, pg, cl, win, u0, u1, v0, v1);
#else
		int rx, ry;
		c = take_entry(depth, pg, u0, u1, v0, v1, &rx, &ry);
		c->gpu_tiles = false;
		c->depth = depth;
		c->page = pg;
		c->clut = cl;
		c->win = win;
		c->rx = rx;
		c->ry = ry;
		c->tiles = 0;
		c->valid = true;
		c->stamp = c->checked = gen_clock;
		int h = key_hash(depth, pg, cl, win);
		c->next = buckets[h];
		buckets[h] = c;
#endif
	}
	// texmask=old keeps the row loop for fixed-layout A/B.
	int t0 = (u0-c->rx)>>5, t1 = (u1-c->rx)>>5;
	int s0 = (v0-c->ry)>>5, s1 = (v1-c->ry)>>5;
	u32 columns = ((1u << (t1-t0+1))-1) << t0;
	u64 need;
	if (pica_texmask && !(pica_debug & 131072)) {
		static const u64 rows[9] = { 0, 0xff, 0xffff, 0xffffff,
			0xffffffff, 0xffffffffff, 0xffffffffffff, 0xffffffffffffff, ~0ull };
		u32 repeated = columns * 0x01010101u;
		need = ((u64)repeated | ((u64)repeated << 32)) & (rows[s1+1] ^ rows[s0]);
		need &= ~c->tiles;
	} else need = 0;
	if (need || (!pica_texmask || (pica_debug & 131072))) decode_rect(c, need, t0 | (t1<<3) | (s0<<6) | (s1<<9));
	c->lru = ++lru_clock;
	last_tex = c;
	have_last = true;
	return c;
}

// This deliberately stays out of the primitive code: one small hot function
// replaces the large lookup/allocator/decoder path on repeated requests.
static noinline tcache *PICA_HOT(texture_for_impl) texture_for_impl(u32 page, u32 clut,
	int u0, int u1, int v0, int v1)
{
	if (!pica_texmemo) return texture_for_slow(page, clut, u0, u1, v0, v1);
	u32 key = (page & 0x19f) | ((clut & 0x7fff) << 9);
	unsigned i = (page ^ clut ^ (clut >> 6)) & 31;
	__typeof__(texmemo[0]) *m = &texmemo[i];
	if (m->generation == gen_clock && m->epoch == texmemo_epoch &&
		m->key == key && m->win == ps.texwin &&
		u0 >= m->u0 && u1 <= m->u1 && v0 >= m->v0 && v1 <= m->v1) {
		tcache *c = m->tex;
		c->lru = ++lru_clock;
		last_tex = c;
		have_last = true;
		return c;
	}
	tcache *c = texture_for_slow(page, clut, u0, u1, v0, v1);
	if (rtt_cache_rebuild) return c;
	// A wrapped request decodes the full page. Record its normalized bounds.
	if ((unsigned)u0 > 255 || (unsigned)u1 > 255 ||
		(unsigned)v0 > 255 || (unsigned)v1 > 255) {
		u0 = v0 = 0; u1 = v1 = 255;
	}
	// all cw x ch tiles: the row's bits repeated in each of ch rows
	u64 full = (u64)((1u << c->cw) - 1) * (0x0101010101010101ull >> (64 - 8 * c->ch));
	if (c->tiles == full) {
		u0 = c->rx; v0 = c->ry;
		u1 = u0 + 32 * c->cw - 1;
		v1 = v0 + 32 * c->ch - 1;
	}
	// the decoded tiles around the request, counted from the square's origin
	*m = (__typeof__(*m)){ key, ps.texwin, gen_clock, texmemo_epoch, c,
		c->rx + ((u0 - c->rx) & ~31), c->rx + ((u1 - c->rx) | 31),
		c->ry + ((v0 - c->ry) & ~31), c->ry + ((v1 - c->ry) | 31) };
	return c;
}

static tcache *texture_for(u32 page, u32 clut, int u0, int u1, int v0, int v1)
{
 u64 started = phase_begin(2);
 tcache * result = texture_for_impl(page, clut, u0, u1, v0, v1);
 phase_end(2, started);
 return result;
}


// ------------------------------------------------------------------ draw state -> citro3d

// The GPU caches texels: after the CPU rewrote texture memory it may still
// draw with the old ones (Azahar doesn't emulate that cache, the hardware
// showed it). Bit 16 of the texture unit register clears it; only that
// byte is written, citro3d's own setting stays.
static inline void clear_tex_cache(void)
{
	GPUCMD_AddMaskedWrite(GPUREG_TEXUNIT_CONFIG, 0x4, 1 << 16);
}

// Clipping to the PS1 drawing area: the viewport covers just that area and
// the projection maps it to the whole clip space, so the GPU clips the
// geometry exactly (its scissor misbehaved near the target's edges).
static void set_area(int x1, int y1, int x2, int y2)
{
	if (x2 <= x1 || y2 <= y1) {   // empty area: nothing may draw
		x2 = x1 + 1; y2 = y1 + 1;
	}
	// the viewport counts rows from the bottom of the target
	C3D_SetViewport(x1, VRAM_H - y2, x2 - x1, y2 - y1);
	float w = x2 - x1, h = y2 - y1;
	// positions arrive in sixteenths of a pixel (see to_gpu)
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_proj + 0, 0.125f / w, 0, 0, -1.0f - 2.0f * x1 / w);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_proj + 1, 0, -0.125f / h, 0, 1.0f + 2.0f * y1 / h);
}

// Vertex positions go to the GPU in sixteenths of a pixel (VRAM fits in 16
// bits), kept just inside the target: the GPU can't hold a screen x of 1024
// (or y of 512), a triangle touching that edge came out wrong (1023.9375
// still covers the last pixel's centre).
#define GPU_UNIT 16
#define POLY_SHIFT_X 8   // polygons: 1/2 pixel across, 7/16 down; see emit_tris
#define POLY_SHIFT_Y 7
static inline void to_gpu(pvtx *v, int n)
{
	for (int i = 0; i < n; i++) {
		int x = v[i].x * GPU_UNIT, y = v[i].y * GPU_UNIT;
		v[i].x = x > VRAM_W * GPU_UNIT - 1 ? VRAM_W * GPU_UNIT - 1 : x;
		v[i].y = y > VRAM_H * GPU_UNIT - 1 ? VRAM_H * GPU_UNIT - 1 : y;
	}
}

static void set_proj(void)
{
	// VRAM sixteenth pixels -> clip space; y = 0 at the top, the GPU's depth range is 0..-1
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_proj + 0, 2.0f / (VRAM_W * GPU_UNIT), 0, 0, -1.0f);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_proj + 1, 0, -2.0f / (VRAM_H * GPU_UNIT), 0, 1.0f);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_proj + 2, 0, 0, 0, -0.5f);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_proj + 3, 0, 0, 0, 1.0f);
}

static void apply_base(void)
{
	C3D_FrameDrawOn(target);
	C3D_SetViewport(0, 0, VRAM_W, VRAM_H);
	C3D_BindProgram(&prog);
	C3D_AttrInfo *ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_SHORT, 2);
	AttrInfo_AddLoader(ai, 1, GPU_SHORT, 2);
	AttrInfo_AddLoader(ai, 2, GPU_UNSIGNED_BYTE, 4);
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, vbuf, sizeof(pvtx), 3, 0x210);
	set_proj();
	C3D_CullFace(GPU_CULL_NONE);
	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
	for (int i = 1; i < 6; i++)
		C3D_TexEnvInit(C3D_GetTexEnv(i));
	applied_dither = -1;
	base_dirty = false;
	sent_valid = false;
}

static void PICA_COLD apply_state_impl(void)
{
	if (base_dirty)
		apply_base();
	const drawstate *d = &cur;
	if (!sent_valid || ((pica_submit & PICA_SUBMIT_CLIP) ?
		sent.sc[0] != d->sc[0] || sent.sc[2] != d->sc[2] :
		memcmp(sent.sc, d->sc, sizeof(d->sc)))) {
		// columns by the GPU's scissor; rows are cut on the CPU (emit_tris):
		// limiting rows on the GPU, by scissor or viewport, lost pixels at
		// the sides of the picture
		// apply_base already installed this full-target viewport/projection.
		// Row clipping is CPU-only; changing it cannot affect GPU scissoring.
		if (!(pica_submit & PICA_SUBMIT_CLIP)) set_area(0, 0, VRAM_W, VRAM_H);
		C3D_SetScissor(GPU_SCISSOR_NORMAL, d->sc[0], 0, d->sc[2], VRAM_H);
	}
	if (!sent_valid || sent.blend != d->blend || sent.texmode != d->texmode || sent.pass != d->pass || sent.mask != d->mask) {
		bool tx = d->texmode != TX_NONE;
		C3D_TexEnv *env = C3D_GetTexEnv(0);
		C3D_TexEnvInit(env);
		if (!tx) {
			C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
			C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
		} else {
			if (d->texmode == TX_MOD) {
				// PS1: texel * colour / 128
				C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
				C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
				C3D_TexEnvScale(env, C3D_RGB, GPU_TEVSCALE_2);
			} else {
				C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, 0, 0);
				C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
			}
			if (d->blend == BL_HALF) {
				// solid and half-transparent primitives share this state:
				// alpha = texel alpha * vertex alpha * 2, vertex alpha 255 for
				// solid primitives (any visible texel -> 255) and 128 for
				// semi-transparent ones (texel alpha 128 or 255 kept)
				C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
				C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
				C3D_TexEnvScale(env, C3D_Alpha, GPU_TEVSCALE_2);
			} else {
				C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
				C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
			}
			if (d->blend == BL_ADD || (d->blend == BL_OPAQUE && !(d->mask & 1))) {
				// blend factor (1 - a) * 2: semi-transparent texels 1, opaque 0
				C3D_TexEnvOpAlpha(env, GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA, 0, 0);
				C3D_TexEnvScale(env, C3D_Alpha, GPU_TEVSCALE_2);
			}
		}

		// The target alpha is the PS1 mask bit (E6 force OR texture STP).
		// Opaque batches write it directly. Semi-transparent batches need
		// alpha for RGB blending first, then flush writes the final mask.
		GPU_BLENDFACTOR MASK_SA = ((d->mask & 5) || d->blend == BL_OPAQUE) ? GPU_ONE : GPU_ZERO;
		switch (d->blend) {
		case BL_OPAQUE:
			C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, MASK_SA, GPU_ZERO);
			C3D_AlphaTest(tx, (d->mask & 1) ? GPU_GREATER : GPU_LESS, (d->mask & 1) ? 0 : 255);
			break;
		case BL_HALF:
			if (tx) {   // a = 0.5 for semi-transparent texels, 1 for opaque ones
				C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, MASK_SA, GPU_ZERO);
				C3D_AlphaTest(true, GPU_GREATER, 0);
			} else {
				C3D_BlendingColor(0x80000000);
				C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_CONSTANT_ALPHA, GPU_ONE_MINUS_CONSTANT_ALPHA, MASK_SA, GPU_ZERO);
				C3D_AlphaTest(false, GPU_ALWAYS, 0);
			}
			break;
		case BL_ADD:
			if (tx)
				C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_SRC_ALPHA, MASK_SA, GPU_ZERO);
			else
				C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, MASK_SA, GPU_ZERO);
			// The inverted alpha is 255 only for transparent texels. They
			// contribute no RGB, but must not clear the destination mask bit.
			C3D_AlphaTest(tx, GPU_LESS, 255);
			break;
		case BL_SUB:
		case BL_QUARTER:
			if (tx && d->pass == 0) {   // the opaque texels, unblended
				C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, MASK_SA, GPU_ZERO);
				C3D_AlphaTest(true, GPU_EQUAL, 255);
				break;
			}
			if (d->blend == BL_SUB)
				C3D_AlphaBlend(GPU_BLEND_REVERSE_SUBTRACT, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, MASK_SA, GPU_ZERO);
			else {
				C3D_BlendingColor(0x40000000);
				C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_CONSTANT_ALPHA, GPU_ONE, MASK_SA, GPU_ZERO);
			}
			C3D_AlphaTest(tx, GPU_EQUAL, 128);
			break;
		}
		if (d->mask & 2) {
			// mask check: pixels whose mask bit (target alpha) is set are
			// kept. Exact for solid, additive and subtractive drawing:
			// out = src * (1 - dst alpha) + dst * (dst alpha or 1); half and
			// quarter blending are drawn like solid there (close enough)
			bool add = d->blend == BL_ADD || ((d->blend == BL_SUB || d->blend == BL_QUARTER) && (d->pass == 1 || !tx));
			C3D_AlphaBlend(d->blend == BL_SUB && add ? GPU_BLEND_REVERSE_SUBTRACT : GPU_BLEND_ADD, GPU_BLEND_ADD,
				GPU_ONE_MINUS_DST_ALPHA, add ? GPU_ONE : GPU_DST_ALPHA, MASK_SA, GPU_ONE);
		}
	}
	// The STP pass changes only stage 0. Keep stage 1 independently valid
	// instead of resending its registers and texture binding after each mask.
	if (applied_dither != d->dither) {
		C3D_TexEnv *env = C3D_GetTexEnv(1);
		C3D_TexEnvInit(env);   // alpha passes through unchanged (STP/blending)
		if (d->dither) {
			C3D_TexBind(1, &dither_tex);
			C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_TEXTURE1, 0);
			C3D_TexEnvFunc(env, C3D_RGB, GPU_ADD_SIGNED);
		}
		// Stage 1 passes through when dithering is off; its texture is
		// unused. citro3d 1.7.1 dereferences the texture on units 1/2,
		// so NULL cannot be used to unbind it there.
		applied_dither = d->dither;
	}
	if (d->texmode != TX_NONE && (!sent_valid || sent.atlas != d->atlas || sent.texmode == TX_NONE || atlas[d->atlas].rebind)) {
		if (atlas[d->atlas].rebind)
			clear_tex_cache();
		// A rewritten atlas needs a texel-cache clear, not new dimensions,
		// a uniform upload and a full texture-unit register block.
		bool cache_only = (pica_submit & PICA_SUBMIT_TEX) && sent_valid &&
			sent.atlas == d->atlas && sent.texmode != TX_NONE;
		atlas[d->atlas].rebind = false;
		if (!cache_only) {
		C3D_FVUnifSet(GPU_VERTEX_SHADER, u_texscale, 1.0f / atlas[d->atlas].w, 1.0f / atlas[d->atlas].h, 0, 0);
		alias_i ^= 1;
		tex_alias[alias_i] = atlas[d->atlas].tex;
		C3D_TexBind(0, &tex_alias[alias_i]);
		}
	}
	sent = *d;
	sent_valid = true;
	state_dirty = false;
}

static void apply_state(void)
{
 u64 started = phase_begin(4);
 apply_state_impl();
 phase_end(4, started);
}


static void flush_current(void)
{
	if (vpos == vstart || !ready)
		return;
	if (!frame_open) {   // nowhere to draw (no GPU frame open)
		vpos = vstart;
		return;
	}
	n_flush++;
	// Opaque primitives can write RGB and the STP mask in one draw. Only
	// batches containing semi-transparent commands need a separate mask pass.
	int original_blend = cur.blend;
	u8 original_mask = cur.mask;
	if (cur.blend == BL_HALF && !batch_semi)
		cur.blend = BL_OPAQUE;
	// rcpu bit 2: the alpha an additive batch blends with is 0 for opaque
	// texels and 1 for STP ones, the PS1 mask bit itself; without untextured
	// primitives (alpha tagged 0 for the mask pass) it can be written directly
	if (cur.blend == BL_ADD && !(cur.mask & 3) && !batch_untex &&
		(pica_rcpu & (RCPU_DEFER | RCPU_ADDMASK)) == (RCPU_DEFER | RCPU_ADDMASK))
		cur.mask = 4;
	if (!sent_valid || sent.blend != cur.blend || sent.mask != cur.mask)
		state_dirty = true;
#ifdef PICA_TIMERS
	u64 t0 = svcGetSystemTick();
#endif
	if (state_dirty || base_dirty)
		apply_state();
#ifdef PICA_TIMERS
	u64 t1 = svcGetSystemTick();
#endif
	u64 submit_start = phase_begin(5);
	if (!(pica_debug & 1024))   // debug: no GPU drawing (CPU cost alone)
		C3D_DrawArrays(GPU_TRIANGLES, vstart, vpos - vstart);
	// gldbg=32768: skip this pass for a hardware submission-cost comparison.
	if (!(pica_debug & (1024 | 32768)) && cur.blend != BL_OPAQUE && (cur.blend < BL_SUB || cur.pass == 1) &&
		!(cur.blend == BL_HALF && (cur.mask & 1)) && !(cur.mask & 4)) {
		// RGB blending needs alpha 1 for opaque texels and 1/2 for STP.
		// VRAM's mask bit instead needs 0 for opaque and 1 for STP. Write
		// it separately on the GPU, preserving RGB and skipping colour 0.
		C3D_TexEnv *env = C3D_GetTexEnv(0);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
		if (!(cur.mask & 1)) {
			if (cur.blend != BL_HALF) {
				// Other untextured draws use vertex alpha 0 as a tag:
				// their dummy half-alpha texel is not a PS1 STP texel.
				C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
				C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
			}
			C3D_TexEnvOpAlpha(env, GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA, 0, 0);
			C3D_TexEnvScale(env, C3D_Alpha, GPU_TEVSCALE_2);
			C3D_AlphaTest(true, GPU_LESS, 255);
		} else
			C3D_AlphaTest(true, GPU_GREATER, 0);
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ZERO, GPU_ONE,
			GPU_ONE, (cur.mask & 2) ? GPU_ONE : GPU_ZERO);
		C3D_DrawArrays(GPU_TRIANGLES, vstart, vpos - vstart);
		// The mask draw changes TEV0/blend/alpha only. Keep the scissor,
		// atlas binding and texture scale valid for the following batch.
		if (pica_submit & PICA_SUBMIT_STATE) sent.blend = 0xff;
		else sent_valid = false;
		state_dirty = true;
	}
	phase_end(5, submit_start);
#ifdef PICA_SIM_HASH
	{   // simulator: what was drawn, to compare builds
		// the state only when it changes: how draws are grouped doesn't count
		static u32 last_st[4] = { ~0u };
		u32 st[4] = { cur.atlas, cur.blend, cur.texmode, cur.pass | (cur.dither << 8) };
		if (memcmp(st, last_st, sizeof(st)) && vpos > vstart) {
			sim_hash(st, sizeof(st));
			memcpy(last_st, st, sizeof(st));
		}
		for (int i = vstart; i < vpos; i++)
			sim_vertex(&vbuf[i]);
	}
#endif
#ifdef PICA_TIMERS
	t_state += t1 - t0;
	t_draw += svcGetSystemTick() - t1;
#endif
	if (C3D_GetCmdBufUsage() > 0.8f)
		C3D_FrameSplit(0);
	seq++;
	vstart = vpos;
	cur.blend = original_blend;
	cur.mask = original_mask;
	batch_semi = batch_untex = false;
	st_batches++;
}

// start drawing with this state: ends the batch if anything differs
static u32 cur_area_gen = ~0u;

static inline bool polygon_dither(u32 cmd)
{
	return dither_config && (dither_config == 2 || (ps.texpage & 0x200)) &&
		((cmd & 4) ? !(cmd & 1) : (cmd & 0x10));
}

#ifdef PICA_LAYOUT_SPLIT
static PICA_COLD void use_state_changed(int at, int blend, int texmode, int pass, bool dither, u8 mask)
{
	st_why[at != cur.atlas ? 0 : (blend != cur.blend || texmode != cur.texmode || pass != cur.pass) ? 1 :
		ps.area_gen != cur_area_gen ? 2 : 3]++;
	flush();
	cur.atlas = at;
	cur.blend = blend;
	cur.texmode = texmode;
	cur.pass = pass;
	cur.mask = mask;
	cur.dither = dither;
	cur.sc[0] = ps.dx1; cur.sc[1] = ps.dy1; cur.sc[2] = ps.dx2; cur.sc[3] = ps.dy2;
	cur_area_gen = ps.area_gen;
	state_dirty = true;
}
#endif

static PICA_INLINE_HOT(use_state) void use_state(int at, int blend, int texmode, int pass, bool dither)
{
	u8 mask = ps.mask & 3;
	// Mask-check primitives must observe the preceding STP write.
	if ((mask & 2) && vpos != vstart)
		flush();
	// The four adjacent mode bytes include dither; mask is separate.
	u32 mode, want = blend | (texmode << 8) | (pass << 16) | ((u32)dither << 24);
	memcpy(&mode, &cur.blend, sizeof(mode));
	if (at == cur.atlas && mode == want && mask == cur.mask && ps.area_gen == cur_area_gen && !atlas[at].rebind)
		return;
#ifdef PICA_LAYOUT_SPLIT
 use_state_changed(at, blend, texmode, pass, dither, mask);
#else
	st_why[at != cur.atlas ? 0 : (blend != cur.blend || texmode != cur.texmode || pass != cur.pass) ? 1 :
		ps.area_gen != cur_area_gen ? 2 : 3]++;
	flush();
	cur.atlas = at;
	cur.blend = blend;
	cur.texmode = texmode;
	cur.pass = pass;
	cur.mask = mask;
	cur.dither = dither;
	cur.sc[0] = ps.dx1; cur.sc[1] = ps.dy1; cur.sc[2] = ps.dx2; cur.sc[3] = ps.dy2;
	cur_area_gen = ps.area_gen;
	state_dirty = true;
#endif
}

static unsigned st_splits;

// Split command lists into bounded pieces; the frontend submits them at
// FrameEnd, after flushing the vertices and textures they read.
static noinline void send_early(void)
{
	flush();
	if (frame_open && !(pica_debug & 8192)) {
		C3D_FrameSplit(0);
		st_splits++;
	}
	sent_vpos = vpos;
}

#ifdef PICA_LAYOUT_SPLIT
static PICA_COLD pvtx *alloc_verts_slow(int n)
{
	if (vpos - sent_vpos >= SEND_VERTS)
		send_early();
	if (vpos + n > vlimit) {
		gpu_sync(SY_VERTICES);   // the GPU has used everything: start the buffer over
		if (vpos + n > vlimit)
			return NULL;
	}
 pvtx *v = vbuf + vpos;
 vpos += n;
 return v;
}
#endif

static inline pvtx *alloc_verts(int n)
{
#ifdef PICA_LAYOUT_SPLIT
 if (PICA_UNLIKELY(vpos - sent_vpos >= SEND_VERTS || vpos + n > vlimit))
  return alloc_verts_slow(n);
#else
	if (vpos - sent_vpos >= SEND_VERTS)
		send_early();
	if (vpos + n > vlimit) {
		gpu_sync(SY_VERTICES);   // the GPU has used everything: start the buffer over
		if (vpos + n > vlimit)
			return NULL;
	}
#endif
	pvtx *v = vbuf + vpos;
	vpos += n;
	return v;
}

// Every externally visible flush is an ordering/lifetime barrier, including
// readbacks, atlas reuse, uploads, frame end and command-list splits.
static void flush(void)
{
	flush_current();
	if (!addq.n)
		return;
	int n = addq.n, nv = addq.nv;
	bool untex = addq.untex;
	addq.n = addq.nv = 0;   // alloc_verts may synchronize and call flush again
	addq.untex = false;
	drawstate save = cur;
	u32 area = cur_area_gen;
	cur = addq.state;
	cur_area_gen = addq.area_gen;
	state_dirty = true;
	pvtx *o = alloc_verts(nv);
	if (o) {
		batch_semi = true;
		batch_untex = untex;
		memcpy(o, addq.verts, nv * sizeof(pvtx));
		for (int i = 0; i < n; i++)
			addq.tex[i]->used_seq = seq + 1;
		flush_current();
	}
	cur = save;
	cur_area_gen = area;
	state_dirty = true;
}

static bool add_overlap(int x0, int y0, int x1, int y1)
{
	for (int i = 0; i < addq.n; i++) {
		const s16 *b = addq.bounds[i];
		// Inclusive edges conservatively cover rasterization/sample rounding.
		if (x0 <= b[2] && x1 >= b[0] && y0 <= b[3] && y1 >= b[1])
			return true;
	}
	return false;
}

// rcpu bit 1: a blended pass (additive, or pass 1 of subtract/quarter) may
// wait in addq while later disjoint primitives draw, so alternating effects
// stop ending a batch each. E6 mask setting is a per-pixel write and is kept
// in the queued state; only mask checking needs the original order. Queued
// entries share one state. Plain additive ones may overlap each other, as
// before; the others are kept pairwise disjoint, so nothing new assumes that
// blending commutes.
static inline bool defer_allowed(void)
{
	return RCPU(RCPU_DEFER) && !(ps.mask & 2) && !(pica_debug & 65536);
}

// the batch being filled already has this state: draw straight into it
static inline bool batch_is(int blend, int pass, bool dither)
{
	return !addq.n && cur.atlas == 0 && cur.blend == blend && cur.texmode == TX_MOD && cur.pass == pass &&
		cur.dither == dither && cur.mask == (ps.mask & 3) && ps.area_gen == cur_area_gen && !atlas[0].rebind;
}

static noinline pvtx *defer_verts(int blend, int pass, bool dither, tcache *tex, int nv,
	int bx0, int by0, int bx1, int by1)
{
	u8 mask = ps.mask & 1;
	if (addq.n && (addq.state.blend != blend || addq.state.pass != pass || addq.state.dither != dither ||
		addq.state.mask != mask || addq.n == ADD_PRIMS || addq.nv + nv > ADD_VERTS ||
		ps.area_gen != addq.area_gen || ((blend != BL_ADD || mask) && add_overlap(bx0, by0, bx1, by1))))
		flush();
	if (!addq.n) {
		addq.state = (drawstate){ .atlas = 0, .blend = blend, .texmode = TX_MOD, .pass = pass, .dither = dither,
			.mask = mask, .sc = { ps.dx1, ps.dy1, ps.dx2, ps.dy2 } };
		addq.area_gen = ps.area_gen;
	}
	s16 *b = addq.bounds[addq.n];
	b[0] = bx0; b[1] = by0; b[2] = bx1; b[3] = by1;
	addq.untex |= tex == plain;
	addq.tex[addq.n++] = tex;
	pvtx *o = addq.verts + addq.nv;
	addq.nv += nv;
	return o;
}

// Keep the common opaque/half path small; only effects and primitives
// that might pass queued effects need the overlap/state work below.
static noinline pvtx *effect_verts(int blend, tcache *tex, int nv,
	int bx0, int by0, int bx1, int by1, bool dither)
{
	if (RCPU(RCPU_DEFER)) {
		if (blend == BL_ADD && defer_allowed() && !batch_is(BL_ADD, 0, dither))
			return defer_verts(BL_ADD, 0, dither, tex, nv, bx0, by0, bx1, by1);
		if (addq.n && ((ps.mask & 2) || ps.area_gen != addq.area_gen || add_overlap(bx0, by0, bx1, by1)))
			flush();
		use_state(0, blend == BL_ADD ? BL_ADD : BL_HALF, TX_MOD, 0, dither);
		pvtx *o = alloc_verts(nv);
		if (o) {
			batch_semi |= blend != BL_OPAQUE;
			batch_untex |= tex == plain;
		}
		return o;
	}
	bool defer = blend == BL_ADD && !(ps.mask & 3) && !(pica_debug & 65536);
	if (addq.n && (ps.mask || ps.area_gen != addq.area_gen || dither != addq.state.dither ||
		addq.n == ADD_PRIMS || (!defer && add_overlap(bx0, by0, bx1, by1))))
		flush();
	pvtx *o;
	if (defer && cur.blend != BL_ADD) {
		if (!addq.n) {
			addq.state = (drawstate){ .atlas = 0, .blend = BL_ADD, .texmode = TX_MOD, .dither = dither,
				.sc = { ps.dx1, ps.dy1, ps.dx2, ps.dy2 } };
			addq.area_gen = ps.area_gen;
		}
		s16 *b = addq.bounds[addq.n];
		b[0] = bx0; b[1] = by0; b[2] = bx1; b[3] = by1;
		addq.tex[addq.n++] = tex;
		o = addq.verts + addq.nv;
		addq.nv += nv;
	} else {
		use_state(0, blend == BL_ADD ? BL_ADD : BL_HALF, TX_MOD, 0, dither);
		o = alloc_verts(nv);
		if (!o)
			return NULL;
		batch_semi |= blend != BL_OPAQUE;
	}
	return o;
}

// Decode native RGB5A1 into the shared RGBA8 atlas in GPU order. Sampling
// a separate target avoids framebuffer feedback, including self-overlap.
// Three byte-quantized TEV stages recover RGB5 then multiply by eight,
// exactly matching texel_rgba instead of retaining hardware bit replication.
static bool decode_gpu(tcache *c, u64 need)
{
	if (!atlas_target || !frame_open) return false;
	if (!c->gpu_tiles) {
		bool dirty = false;
		int px = (c->page & 15)*64, py=(c->page >> 4)*256;
		for (u64 bits=need; bits && !dirty; bits &= bits-1) {
			int t=__builtin_ctzll(bits), x=px+c->rx+(t&7)*32, y=py+c->ry+(t>>3)*32;
			for (int yy=y; yy<y+32; yy+=8)
				for (int xx=x; xx<x+32; xx+=8)
					dirty |= !!(fine_drawn[(yy>>3)&63][((xx>>3)&127)>>5] & (1u<<((xx>>3)&31)));
		}
		if (!dirty) return false;
	}
	int count = __builtin_popcountll(need)*6;
	flush();
	pvtx *v = alloc_verts(count);
	if (!v) return false;
	int first = (int)(v-vbuf), n = 0;
	int px = (c->page & 15)*64, py = (c->page >> 4)*256;
	// A page crossing VRAM's right edge is split by 32-texel tiles (the
	// page origin and each tile are 64/32 aligned).
	for (u64 bits=need; bits; bits &= bits-1) {
		int tile = __builtin_ctzll(bits), lu=(tile&7)*32, lv=(tile>>3)*32;
		int x=c->ax+lu, y=c->ay+lv;
		int u=(px+c->rx+lu)&1023, t=VRAM_H-(py+c->ry+lv);
		pvtx q[6] = {
			{ x*4,y*4,u,t,~0u }, { (x+32)*4,y*4,u+32,t,~0u }, { x*4,(y+32)*4,u,t-32,~0u },
			{ (x+32)*4,y*4,u+32,t,~0u }, { (x+32)*4,(y+32)*4,u+32,t-32,~0u }, { x*4,(y+32)*4,u,t-32,~0u } };
		// Keep the far target edge inside PICA's 1024-pixel limit,
		// as the ordinary VRAM drawing path does in to_gpu().
		for (int i=0;i<6;i++) {
			if (q[i].x>4095) q[i].x=4095;
			if (q[i].y>4095) q[i].y=4095;
		}
		memcpy(v+n,q,sizeof(q)); n+=6;
		// A subsequent write must advance versions even while CPU VRAM
		// remains dirty: GPU snapshots do not clear CPU readback debt.
		for (int yy=py+c->ry+lv; yy<py+c->ry+lv+32; yy+=8)
			for (int xx=u; xx<u+32; xx+=8)
				fine_snapshot[(yy>>3)&63][xx>>8] |= 1u << ((xx>>3)&31);
	}
	known_fx0=1; known_fx1=0;
	alt_fx0=1; alt_fx1=0;
	if (base_dirty) apply_base();
	C3D_FrameSplit(0);
	st_splits++;
	C3D_FrameDrawOn(atlas_target);
	C3D_SetViewport(0,0,1024,1024);
	C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,1024,1024);
	C3D_FVUnifSet(GPU_VERTEX_SHADER,u_proj+0,0.5f/1024,0,0,-1);
	C3D_FVUnifSet(GPU_VERTEX_SHADER,u_proj+1,0,0.5f/1024,0,-1);
	C3D_FVUnifSet(GPU_VERTEX_SHADER,u_texscale,1.f/VRAM_W,1.f/VRAM_H,0,0);
	C3D_TexBind(0,&vram_tex);
	clear_tex_cache();
	for (int i=0; i<6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	C3D_TexEnv *e=C3D_GetTexEnv(0);
	C3D_TexEnvSrc(e,C3D_RGB,GPU_TEXTURE0,GPU_CONSTANT,0);
	C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
	C3D_TexEnvColor(e,0x001f1f1f);
	C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE0,GPU_TEXTURE0,0);
	C3D_TexEnvOpAlpha(e,GPU_TEVOP_A_SRC_R,GPU_TEVOP_A_SRC_G,0);
	C3D_TexEnvFunc(e,C3D_Alpha,GPU_ADD);
	C3D_TexEnvScale(e,C3D_Alpha,GPU_TEVSCALE_4);
	e=C3D_GetTexEnv(1);
	C3D_TexEnvScale(e,C3D_RGB,GPU_TEVSCALE_4);
	C3D_TexEnvSrc(e,C3D_Alpha,GPU_PREVIOUS,GPU_TEXTURE0,0);
	C3D_TexEnvOpAlpha(e,GPU_TEVOP_A_SRC_ALPHA,GPU_TEVOP_A_SRC_B,0);
	C3D_TexEnvFunc(e,C3D_Alpha,GPU_ADD);
	C3D_TexEnvScale(e,C3D_Alpha,GPU_TEVSCALE_4);
	e=C3D_GetTexEnv(2);
	C3D_TexEnvScale(e,C3D_RGB,GPU_TEVSCALE_2);
	C3D_TexEnvScale(e,C3D_Alpha,GPU_TEVSCALE_4);
	e=C3D_GetTexEnv(3);
	C3D_TexEnvScale(e,C3D_Alpha,GPU_TEVSCALE_4);
	C3D_AlphaTest(false,GPU_ALWAYS,0);
	C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
	C3D_DrawArrays(GPU_TRIANGLES,first,n);
	// The second pass writes only STP texels' alpha (128), including
	// black+STP. Zero without STP remains transparent from the first pass.
	for (int i=0; i<4; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	e=C3D_GetTexEnv(0);
	C3D_TexEnvSrc(e,C3D_Both,GPU_TEXTURE0,GPU_CONSTANT,0);
	C3D_TexEnvFunc(e,C3D_Alpha,GPU_MODULATE);
	C3D_TexEnvColor(e,0x80000000);
	C3D_AlphaTest(true,GPU_GREATER,0);
	C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ZERO,GPU_ONE,GPU_ONE,GPU_ZERO);
	C3D_DrawArrays(GPU_TRIANGLES,first,n);
	vstart=vpos; seq++;
	st_gpu_tiles += n/6;
	c->tiles |= need; c->gpu_tiles=true;
	base_dirty=state_dirty=true; sent_valid=false;
	apply_base();
	clear_tex_cache();
	atlas[0].rebind=true;
	return true;
}

// ------------------------------------------------------------------ primitives

static int blend_of(u32 cmd)
{
	if (!(cmd & 2))
		return BL_OPAQUE;
	return BL_HALF + ((ps.texpage >> 5) & 3);
}

// Clipping deltas are finite, small signed values. VCVTR obeys FPSCR's
// rounding mode, as lrintf does, without newlib's general exponent/bit path.
// Keep the A/B decision in one small leaf instead of eight copies per edge.
static noinline int clip_round(float f)
{
#if defined(__arm__)
	if (PRIM_TEST(PRIM_VFP_ROUND)) {
		float bits;
		int result;
		__asm__("vcvtr.s32.f32 %0, %1" : "=t" (bits) : "t" (f));
		memcpy(&result, &bits, sizeof(result));
		return result;
	}
#endif
	return (int)lrintf(f);
}

// one textured or plain triangle list entry; two-pass modes draw it twice
// The part of triangle t inside the drawing area x0 <= x < x1, y0 <= y < y1,
// as a fan of up to 5 triangles into out; returns how many. The GPU only
// ever sees geometry inside the area: the 3DS GPU mangles triangles that
// reach far outside its viewport (PS1 skies are huge polygons).
static int clip_area(const pvtx *t, int x0, int y0, int x1, int y1, unsigned planes, pvtx *out)
{
	pvtx a[9], b[9];
	int n = 3, m;
	memcpy(a, t, 3 * sizeof(pvtx));
	for (int side = 0; side < 4; side++) {
		// Clipping and integer rounding cannot leave the original bounds.
		// A containing plane would only copy every vertex back unchanged.
		if (!(planes & (1u << side))) continue;
		m = 0;
		for (int i = 0; i < n; i++) {
			const pvtx *p = &a[i], *q = &a[(i + 1) % n];
			int pc = side == 0 ? p->x - x0 : side == 1 ? x1 - p->x : side == 2 ? p->y - y0 : y1 - p->y;
			int qc = side == 0 ? q->x - x0 : side == 1 ? x1 - q->x : side == 2 ? q->y - y0 : y1 - q->y;
			if (pc >= 0)
				b[m++] = *p;
			if ((pc >= 0) != (qc >= 0)) {   // the edge crosses this side
				float f = (float)pc / (pc - qc);
				pvtx *o = &b[m++];
				o->x = p->x + clip_round((q->x - p->x) * f);
				o->y = p->y + clip_round((q->y - p->y) * f);
				if (side < 2) o->x = side == 0 ? x0 : x1;
				else o->y = side == 2 ? y0 : y1;
				o->u = p->u + clip_round((q->u - p->u) * f);
				o->v = p->v + clip_round((q->v - p->v) * f);
				u32 c = 0;
				for (int k = 0; k < 32; k += 8) {
					int cp = (p->rgba >> k) & 0xff, cq = (q->rgba >> k) & 0xff;
					c |= (u32)(cp + clip_round((cq - cp) * f)) << k;
				}
				o->rgba = c;
			}
		}
		if (m < 3)
			return 0;
		memcpy(a, b, m * sizeof(pvtx));
		n = m;
	}
	for (int i = 1; i + 1 < n; i++) {
		out[(i - 1) * 3] = a[0];
		out[(i - 1) * 3 + 1] = a[i];
		out[(i - 1) * 3 + 2] = a[i + 1];
	}
	return n - 2;
}

#ifdef PICA_LAYOUT_SPLIT
static PICA_COLD int clip_tris(const pvtx *v, int ntri, int x0, int y0, int x1, int y1, pvtx *clipped)
{
		int n = 0;
		for (int t = 0; t < ntri; t++) {
			const pvtx *tv = &v[t * 3];
			int lx = MIN3(tv[0].x, tv[1].x, tv[2].x), hx = MAX3(tv[0].x, tv[1].x, tv[2].x);
			int ly = MIN3(tv[0].y, tv[1].y, tv[2].y), hy = MAX3(tv[0].y, tv[1].y, tv[2].y);
			if (hx <= x0 || lx >= x1 || hy <= y0 || ly >= y1)
				continue;   // entirely outside
			if (lx >= x0 && hx <= x1 && ly >= y0 && hy <= y1) {
				memcpy(&clipped[n * 3], tv, 3 * sizeof(pvtx));
				n++;
			} else
				n += clip_area(tv, x0, y0, x1, y1,
					(PRIM_TEST(PRIM_CLIP_PLANES)) ?
						(lx < x0) | ((hx > x1) << 1) | ((ly < y0) << 2) | ((hy > y1) << 3) : 15,
					&clipped[n * 3]);
		}
 return n;
}
#endif

static void noinline PICA_HOT(emit_tris) emit_tris(const pvtx *v, int ntri, int blend, int texmode, tcache *tex, bool polygon, bool dither)
{
	bool semi = blend != BL_OPAQUE;
	if (!ready || (pica_debug & 4096))   // debug 4096: primitives decoded, not drawn
		return;
	// anything reaching outside the drawing area is cut to it here
	pvtx clipped[2 * 3 * 7];
	int x0 = ps.dx1, x1 = ps.dx2, y0 = ps.dy1, y1 = ps.dy2;
	bool cut = false;
	for (int i = 0; i < ntri * 3; i++)
		if (v[i].x < x0 || v[i].x > x1 || v[i].y < y0 || v[i].y > y1) { cut = true; break; }
	if (cut) {
#ifdef PICA_LAYOUT_SPLIT
  int n = clip_tris(v, ntri, x0, y0, x1, y1, clipped);
#else
		int n = 0;
		for (int t = 0; t < ntri; t++) {
			const pvtx *tv = &v[t * 3];
			int lx = MIN3(tv[0].x, tv[1].x, tv[2].x), hx = MAX3(tv[0].x, tv[1].x, tv[2].x);
			int ly = MIN3(tv[0].y, tv[1].y, tv[2].y), hy = MAX3(tv[0].y, tv[1].y, tv[2].y);
			if (hx <= x0 || lx >= x1 || hy <= y0 || ly >= y1)
				continue;   // entirely outside
			if (lx >= x0 && hx <= x1 && ly >= y0 && hy <= y1) {
				memcpy(&clipped[n * 3], tv, 3 * sizeof(pvtx));
				n++;
			} else
				n += clip_area(tv, x0, y0, x1, y1,
					(PRIM_TEST(PRIM_CLIP_PLANES)) ?
						(lx < x0) | ((hx > x1) << 1) | ((ly < y0) << 2) | ((hy > y1) << 3) : 15,
					&clipped[n * 3]);
		}
#endif
		if (!n)
			return;
		v = clipped;
		ntri = n;
	}
	int lx = 0x7fff, hx = -0x8000, ly = 0x7fff, hy = -0x8000;
	for (int i = 0; i < ntri * 3; i++) {
		lx = v[i].x < lx ? v[i].x : lx; hx = v[i].x > hx ? v[i].x : hx;
		ly = v[i].y < ly ? v[i].y : ly; hy = v[i].y > hy ? v[i].y : hy;
	}
	const int qx0 = lx, qy0 = ly, qx1 = hx, qy1 = hy;   // bounds against queued passes
	// (an additive primitive that will wait in the queue itself is checked there)
	if (addq.n && (!RCPU(RCPU_DEFER) || (ps.mask & 2) || ps.area_gen != addq.area_gen ||
		(!(blend == BL_ADD && defer_allowed()) && add_overlap(lx, ly, hx, hy))))
		flush();
	{
		if (!rtt_old()) {
			if (!polygon) { hx--; hy--; }
			if (hx >= ps.dx2) hx = ps.dx2-1;
			if (hy >= ps.dy2) hy = ps.dy2-1;
		}
		mark_drawn(lx, ly, hx, hy);
	}
	// untextured: drawn through the same path with a mid-grey texel
	// (x2 modulation gives the colour back); its alpha marks semi-transparency
	bool untex = !tex;
	if (untex) {
		tex = plain;
		texmode = TX_MOD;
	}
	// raw textures: modulated by 0x80 (x2 = unchanged), so they batch with the rest
	u32 col_and = ~0u, col_or = 0;
	if (texmode == TX_RAW) {
		texmode = TX_MOD;
		col_and = 0xff000000;
		col_or = 0x00808080;
	}
	// solid primitives join the half-transparent state (see apply_state)
	if (blend == BL_OPAQUE) {
		blend = BL_HALF;
		col_and &= 0x00ffffff;
		col_or |= 0xff000000;
	} else if (blend == BL_HALF) {
		col_and &= 0x00ffffff;
		col_or |= untex ? 0x40000000 : 0x80000000;
	} else if (untex) {
		col_and &= 0x00ffffff;   // mask pass: plain texel has no STP
	}
	// finish the vertices here (in cache) and write them out once: reading
	// back from the vertex buffer missed the cache every time (the Old
	// 3DS's cache only fills on reads; its memory reads are slow)
	int nv = ntri * 3;
	pvtx fin[2 * 3 * 7];
	memcpy(fin, v, nv * sizeof(pvtx));
	to_gpu(fin, nv);
	// PS1 polygons interpolate at integer pixel positions; PICA samples at
	// pixel centres. Half a pixel of translation aligns those samples, which
	// keeps 1:1 sprites (also mirrored ones) on exact texels. In y exactly
	// half a pixel put every PS1 row edge on a sample point, and real PICA
	// breaks those ties its own way (rows are flipped in the target):
	// polygons lost their first row and gained one below (Crash: the sky
	// fill showed as a blue line at the top). 7/16 keeps row edges off the
	// sample points; interpolation moves by only 1/16 pixel.
	// Rectangles/uploads already map their pixel centres to texel centres.
	if (polygon)
		for (int i = 0; i < nv; i++) {
			fin[i].x = fin[i].x < VRAM_W * GPU_UNIT - POLY_SHIFT_X ? fin[i].x + POLY_SHIFT_X : VRAM_W * GPU_UNIT - 1;
			fin[i].y = fin[i].y < VRAM_H * GPU_UNIT - POLY_SHIFT_Y ? fin[i].y + POLY_SHIFT_Y : VRAM_H * GPU_UNIT - 1;
		}
	if (col_and != ~0u || col_or)
		for (int i = 0; i < nv; i++)
			fin[i].rgba = (fin[i].rgba & col_and) | col_or;
	if (untex) {
		s16 pu = plain->ax + (blend == BL_HALF ? 8 : 24), pv = plain->ay + 8;
		for (int i = 0; i < nv; i++) {
			fin[i].u = pu;
			fin[i].v = pv;
		}
	}
	int passes = (blend == BL_SUB || blend == BL_QUARTER) ? 2 : 1;
	for (int p = 0; p < passes; p++) {
		pvtx *o;
		// the blended pass of an effect waits for later disjoint primitives
		if ((blend == BL_ADD || p == 1) && defer_allowed() && !batch_is(blend, p, dither))
			o = defer_verts(blend, p, dither, tex, nv, qx0, qy0, qx1, qy1);
		else {
			use_state(0, blend, texmode, p, dither);
			o = alloc_verts(nv);
		}
		if (!o)
			return;
		memcpy(o, fin, nv * sizeof(pvtx));
		batch_semi |= semi;
		batch_untex |= untex;
	}
	tex->used_seq = seq + 1;   // the batch it is in: seq + 1 once sent
	st_prims++; psx3d_skip.render_prims++;
}

static void noinline __attribute__((cold)) draw_poly_slow(pvtx *v, int n, bool quad, bool textured, u32 page, u32 clut, u32 cmd);

static void PICA_HOT(draw_poly_impl) draw_poly_impl(const u32 *w, u32 cmd)
{
	bool quad = cmd & 8, gouraud = cmd & 0x10, textured = cmd & 4;
	int n = quad ? 4 : 3;
	pvtx v[4];
	u32 col = w[0] & 0xffffff;
	u32 clut = 0, page = ps.texpage;
	int i = 1;
	// decode, tracking the bounding box and the texture coordinate range
	int bx0 = 0x7fff, bx1 = -0x8000, by0 = 0x7fff, by1 = -0x8000;
	int u0 = 255, u1 = 0, v0 = 255, v1 = 0;
	for (int k = 0; k < n; k++) {
		if (gouraud && k)
			col = w[i++] & 0xffffff;
		u32 xy = w[i++];
		// Keep relative positions across x=1024/y=1024. The PS1 wraps one
		// offset for the whole triangle, not each post-offset vertex.
		int x = sext11(xy) + ps.ox, y = sext11(xy >> 16) + ps.oy;
		v[k].x = x;
		v[k].y = y;
		v[k].rgba = col;
		bx0 = x < bx0 ? x : bx0; bx1 = x > bx1 ? x : bx1;
		by0 = y < by0 ? y : by0; by1 = y > by1 ? y : by1;
		if (textured) {
			u32 uv = w[i++];
			int tu = uv & 0xff, tv = (uv >> 8) & 0xff;
			v[k].u = tu;
			v[k].v = tv;
			u0 = tu < u0 ? tu : u0; u1 = tu > u1 ? tu : u1;
			v0 = tv < v0 ? tv : v0; v1 = tv > v1 ? tv : v1;
			if (k == 0) clut = uv >> 16;
			if (k == 1) page = uv >> 16;
		}
	}
	if (textured)   // textured polygons also set the global texpage (bits 0-8, and 11)
		ps.texpage = (ps.texpage & ~0x9ff) | (page & 0x9ff);
	int blend = blend_of(cmd);
	// fast path, nearly every polygon: inside the drawing area, smaller than
	// the PS1's size limit (so no triangle of it is dropped), solid, half-transparent
	// or additive. Finished vertices go straight to the buffer.
	if (bx0 < ps.dx1 || bx1 > ps.dx2 || by0 < ps.dy1 || by1 > ps.dy2 || bx1 - bx0 > 1023 || by1 - by0 > 511 ||
		blend > BL_ADD || !ready || (pica_debug & 4096)) {
		for (int k = 0; k < n; k++)
			v[k].rgba |= 0xff000000;
		draw_poly_slow(v, n, quad, textured, page, clut, cmd);
		return;
	}
	tcache *tex = textured ? texture_for(page, clut, u0, u1, v0, v1) : plain;
	mark_drawn(bx0, by0, !rtt_old() && bx1 >= ps.dx2 ? ps.dx2-1 : bx1,
		!rtt_old() && by1 >= ps.dy2 ? ps.dy2-1 : by1);
	// see emit_tris: alpha 255 solid / 128 half; raw textures colour 0x80
	u32 a = blend == BL_OPAQUE ? 0xff000000 : blend == BL_HALF ?
		(textured ? 0x80000000 : 0x40000000) : (textured ? 0xff000000 : 0);
	u32 rawcol = (textured && (cmd & 1)) ? 0x808080 : 0;
	int du = tex->ax - tex->rx, dv = tex->ay - tex->ry;
	pvtx *o;
	if (blend == BL_ADD || addq.n)
		o = effect_verts(blend, tex, quad ? 6 : 3, bx0, by0, bx1, by1, polygon_dither(cmd));
	else {
		use_state(0, BL_HALF, TX_MOD, 0, polygon_dither(cmd));
		o = alloc_verts(quad ? 6 : 3);
		if (o) batch_semi |= blend != BL_OPAQUE;
	}
	if (!o)
		return;
	// finished vertices as three words each, built in cache and stored once
	// (never read back from the buffer: the Old 3DS cache fills on reads only).
	// Exact target edges need the same sub-pixel clamp as to_gpu:
	// PICA cannot represent screen x=1024 or y=512 reliably.
	u64 vertex_start = phase_begin(7);
	u32 q[4][3];
	if (!textured)
		du = plain->ax + (blend == BL_ADD ? 24 : 8), dv = plain->ay + 8;
	for (int k = 0; k < n; k++) {
		int u = textured ? v[k].u + du : du, vv = textured ? v[k].v + dv : dv;
		int gx = v[k].x * GPU_UNIT + POLY_SHIFT_X, gy = v[k].y * GPU_UNIT + POLY_SHIFT_Y;   // see emit_tris
		gx = gx < VRAM_W * GPU_UNIT ? gx : VRAM_W * GPU_UNIT - 1;
		gy = gy < VRAM_H * GPU_UNIT ? gy : VRAM_H * GPU_UNIT - 1;
		q[k][0] = (u32)gx | ((u32)gy << 16);
		q[k][1] = (u32)(u & 0xffff) | ((u32)vv << 16);
		q[k][2] = (rawcol ? rawcol : v[k].rgba) | a;
	}
	u32a *ow = (u32a *)o;
	static const u8 order[6] = { 0, 1, 2, 1, 3, 2 };
	if ((pica_debug & 16777216) || PRIM_TEST(PRIM_PACK_COPY)) {
		// Same triangle order, fixed copies let ARM use block transfers.
		memcpy(ow, q, 3 * sizeof(q[0]));
		if (quad) {
			memcpy(ow + 9, q[1], sizeof(q[0]));
			memcpy(ow + 12, q[3], sizeof(q[0]));
			memcpy(ow + 15, q[2], sizeof(q[0]));
		}
	} else {
	for (int k = 0; k < (quad ? 6 : 3); k++) {
		ow[k * 3] = q[order[k]][0];
		ow[k * 3 + 1] = q[order[k]][1];
		ow[k * 3 + 2] = q[order[k]][2];
	}
	}
	phase_end(7, vertex_start);
	tex->used_seq = seq + 1;
	st_prims++; psx3d_skip.render_prims++;
}

static void draw_poly(const u32 *w, u32 cmd)
{
 u64 started = phase_begin(1);
 draw_poly_impl(w, cmd);
 phase_end(1, started);
}


// polygons that need clipping, lose a triangle to the size limit, or use
// the two-pass blend modes
static void draw_poly_slow(pvtx *v, int n, bool quad, bool textured, u32 page, u32 clut, u32 cmd)
{
	// the PS1 skips triangles wider than 1023 or taller than 511
	pvtx tri[6];
	int ntri = 0;
	static const int idx[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };
	for (int t = 0; t < n - 2; t++) {
		const pvtx *a = &v[idx[t][0]], *b = &v[idx[t][1]], *c = &v[idx[t][2]];
		int x0 = MIN3(a->x, b->x, c->x), x1 = MAX3(a->x, b->x, c->x);
		int y0 = MIN3(a->y, b->y, c->y), y1 = MAX3(a->y, b->y, c->y);
		if (x1 - x0 > 1023 || y1 - y0 > 511)
			continue;
		tri[ntri * 3] = *a; tri[ntri * 3 + 1] = *b; tri[ntri * 3 + 2] = *c;
		// Like UNAI/neon: wrap the lowest coordinate and translate all
		// three vertices equally, after the size test. Each quad half can
		// have a different minimum. Most triangles need no adjustment.
		int dx = sext11(x0) - x0, dy = sext11(y0) - y0;
		if (dx || dy)
			for (int k = 0; k < 3; k++) {
				tri[ntri * 3 + k].x += dx;
				tri[ntri * 3 + k].y += dy;
			}
		ntri++;
	}
	if (!ntri)
		return;
	tcache *tex = NULL;
	if (textured) {
		int u0 = 255, u1 = 0, v0 = 255, v1 = 0;
		for (int k = 0; k < n; k++) {
			u0 = v[k].u < u0 ? v[k].u : u0;
			u1 = v[k].u > u1 ? v[k].u : u1;
			v0 = v[k].v < v0 ? v[k].v : v0;
			v1 = v[k].v > v1 ? v[k].v : v1;
		}
		tex = texture_for(page, clut, u0, u1, v0, v1);
		int du = tex->ax - tex->rx, dv = tex->ay - tex->ry;
		for (int k = 0; k < ntri * 3; k++) {   // into the square's place in its atlas
			tri[k].u += du;
			tri[k].v += dv;
		}
	}
	int texmode = !textured ? TX_NONE : (cmd & 1) ? TX_RAW : TX_MOD;
	emit_tris(tri, ntri, blend_of(cmd), texmode, tex, true, polygon_dither(cmd));
}

// one sprite (or a part of one) at x, y with page texels from u, v
static void sprite(int x, int y, int sw, int sh, int u, int v, u32 col, u32 cmd, u32 clut)
{
	bool textured = cmd & 4;
	int u0 = 0, v0 = 0, u1 = 0, v1 = 0;
	tcache *tex = NULL;
	if (textured) {
		bool fx = ps.texpage & (1 << 12), fy = ps.texpage & (1 << 13);
		// Split at each wrap in either direction, including repeated pages.
		// Flipped texels run u, u-1, ... 0, 255; their first span is u+1.
		if (sw > (fx ? u + 1 : 256 - u)) {
			int k = fx ? u + 1 : 256 - u;
			sprite(x, y, k, sh, u, v, col, cmd, clut);
			sprite(x + k, y, sw - k, sh, fx ? 255 : 0, v, col, cmd, clut);
			return;
		}
		if (sh > (fy ? v + 1 : 256 - v)) {
			int k = fy ? v + 1 : 256 - v;
			sprite(x, y, sw, k, u, v, col, cmd, clut);
			sprite(x, y + k, sw, sh - k, u, fy ? 255 : 0, col, cmd, clut);
			return;
		}
		u0 = u; v0 = v;
		u1 = u0 + sw; v1 = v0 + sh;
		if (fx) { u0 = u0 + 1; u1 = u0 - sw; }   // x flip
		if (fy) { v0 = v0 + 1; v1 = v0 - sh; }   // y flip
		int ua = u0 < u1 ? u0 : u1, ub = u0 < u1 ? u1 : u0;
		int va = v0 < v1 ? v0 : v1, vb = v0 < v1 ? v1 : v0;
		tex = texture_for(ps.texpage, clut, ua, ub - 1, va, vb - 1);
		u0 += tex->ax - tex->rx; u1 += tex->ax - tex->rx;
		v0 += tex->ay - tex->ry; v1 += tex->ay - tex->ry;
	}
	pvtx q[6] = {
		{ x, y, u0, v0, col }, { x + sw, y, u1, v0, col }, { x, y + sh, u0, v1, col },
		{ x + sw, y, u1, v0, col }, { x + sw, y + sh, u1, v1, col }, { x, y + sh, u0, v1, col } };
	int texmode = !textured ? TX_NONE : (cmd & 1) ? TX_RAW : TX_MOD;
	emit_tris(q, 2, blend_of(cmd), texmode, tex, false, false);
}

static void PICA_COLD draw_rect_impl(const u32 *w, u32 cmd)
{
	bool textured = cmd & 4;
	int sw, sh;
	switch ((cmd >> 3) & 3) {
	case 1: sw = sh = 1; break;
	case 2: sw = sh = 8; break;
	case 3: sw = sh = 16; break;
	default: {
		u32 wh = w[textured ? 3 : 2];
		sw = wh & 0x3ff; sh = (wh >> 16) & 0x1ff;
	}
	}
	if (!sw || !sh)
		return;
	int x = sext11(sext11(w[1]) + ps.ox), y = sext11(sext11(w[1] >> 16) + ps.oy);
	u32 col = (w[0] & 0xffffff) | 0xff000000;
	u32 uv = textured ? w[2] : 0;
	sprite(x, y, sw, sh, uv & 0xff, (uv >> 8) & 0xff, col, cmd, uv >> 16);
}

static void draw_rect(const u32 *w, u32 cmd)
{
 u64 started = phase_begin(1);
 draw_rect_impl(w, cmd);
 phase_end(1, started);
}


// a line becomes a quad one pixel thick across its main direction
static void noinline draw_line(int x0, int y0, u32 c0, int x1, int y1, u32 c1, int blend)
{
	int dx = x1 - x0, dy = y1 - y0;
	if ((dx < 0 ? -dx : dx) > 1023 || (dy < 0 ? -dy : dy) > 511)
		return;
	pvtx q[6];
	bool horiz = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
	int ex0 = x0, ey0 = y0, ex1 = x1, ey1 = y1;
	// include the end pixel
	if (horiz) { if (x1 >= x0) ex1++; else ex0++; } else { if (y1 >= y0) ey1++; else ey0++; }
	int ox = horiz ? 0 : 1, oy = horiz ? 1 : 0;
	pvtx a = { ex0, ey0, 0, 0, c0 | 0xff000000 }, b = { ex1, ey1, 0, 0, c1 | 0xff000000 };
	pvtx a2 = a, b2 = b;
	a2.x += ox; a2.y += oy; b2.x += ox; b2.y += oy;
	q[0] = a; q[1] = b; q[2] = a2; q[3] = b; q[4] = b2; q[5] = a2;
	emit_tris(q, 2, blend, TX_NONE, NULL, false, false);
}

// GP0 02: fill a VRAM rectangle (ignores the drawing area and mask)
static void noinline fill_rect(const u32 *w)
{
	int x = w[1] & 0x3f0, y = (w[1] >> 16) & 0x1ff;
	int fw = ((w[2] & 0x3ff) + 15) & ~15, fh = (w[2] >> 16) & 0x1ff;
	if (!fw || !fh)
		return;
	// software VRAM too (textures are decoded from it), but only when
	// something reads that area: games clear their framebuffer every frame
	u32 c = w[0];
	defer_fill(x, y, fw, fh, ((c >> 3) & 0x1f) | (((c >> 11) & 0x1f) << 5) | (((c >> 19) & 0x1f) << 10));
	mark_vram(x, y, fw, fh);
	u32 col = (w[0] & 0xffffff) | 0xff000000;
	int save[4] = { ps.dx1, ps.dy1, ps.dx2, ps.dy2 };
	u32 save_mask = ps.mask;
	ps.mask = 0;   // fills write mask 0 whatever E6 says
	ps.dx1 = 0; ps.dy1 = 0; ps.dx2 = VRAM_W; ps.dy2 = VRAM_H;
	ps.area_gen++;
	pvtx q[6] = {
		{ x, y, 0, 0, col }, { x + fw, y, 0, 0, col }, { x, y + fh, 0, 0, col },
		{ x + fw, y, 0, 0, col }, { x + fw, y + fh, 0, 0, col }, { x, y + fh, 0, 0, col } };
	emit_tris(q, 2, BL_OPAQUE, TX_NONE, NULL, false, false);
	ps.dx1 = save[0]; ps.dy1 = save[1]; ps.dx2 = save[2]; ps.dy2 = save[3];
	ps.area_gen++;
	ps.mask = save_mask;
}

static void set_ex(u32 word)
{
	switch ((word >> 24) & 7) {
	case 1: ps.texpage = word & 0x3fff; break;
	case 2: ps.texwin = word & 0xfffff; break;
	case 3: ps.dx1 = word & 0x3ff; ps.dy1 = (word >> 10) & 0x1ff; ps.area_gen++; break;
	case 4: ps.dx2 = (word & 0x3ff) + 1; ps.dy2 = ((word >> 10) & 0x1ff) + 1; ps.area_gen++; break;
	case 5: ps.ox = sext11(word); ps.oy = sext11(word >> 11); break;
	case 6: ps.mask = word & 3; break;
	}
}

struct copy_request { int sx, sy, dx, dy, w, h; unsigned mask; int result; };
static void copy_on_thread(void *data)
{
	struct copy_request *r=data;
	int sx=r->sx, sy=r->sy, dx=r->dx, dy=r->dy, w=r->w, h=r->h;
	r->result=0;
	if (rtt_old() && !(pica_debug & RTT_OLD) && sx >= 0 && sy >= 0 &&
		w > 0 && h > 0 && sx+w <= VRAM_W && sy+h <= VRAM_H) {
		unsigned mask = ((2u << ((sx+w-1)>>6))-1) & ~((1u << (sx>>6))-1);
		for (int y = sy>>4; y <= (sy+h-1)>>4; y++)
			if (drawn[y] & mask) { activate_rtt(); break; }
	}
	// Native tile copies preserve every RGB and mask bit. Partial tiles,
	// wrapping, overlap and E6 mask operations use the CPU fallback.
	if (!ready || rtt_old() || r->mask || ((sx|sy|dx|dy|w|h)&7) ||
		sx+w>VRAM_W || dx+w>VRAM_W || sy+h>VRAM_H || dy+h>VRAM_H ||
		(sx<dx+w && dx<sx+w && sy<dy+h && dy<sy+h)) return;
	if (!frame_open && pica_open_frame) pica_open_frame();
	if (!frame_open) return;
	flush();
	C3D_FrameSplit(0);
	st_splits++;
	// A native 8-row band is w*16 bytes; GX line/gap fields use
	// 16-byte units. One strided transfer avoids consuming a GX queue
	// entry for every band of a tall rectangle.
	u32 dim=GX_BUFFER_DIM(w,VRAM_W-w);
	GX_TextureCopy((u32*)((u16*)vram_tex.data+tiled(sx,sy,VRAM_W)),dim,
		(u32*)((u16*)vram_tex.data+tiled(dx,dy,VRAM_W)),dim,w*h*2,8);
	// The CPU copy is deferred until someone actually reads the destination.
	// Mark only complete native tiles; the source was never read by the CPU.
	mark_vram(dx,dy,w,h);
	known_fx0=1; known_fx1=0;
	alt_fx0=1; alt_fx1=0;
	mark_drawn(dx,dy,dx+w-1,dy+h-1);
	clear_tex_cache();
	base_dirty=state_dirty=true; sent_valid=false;
	st_gpu_copies++;
	r->result=1;
}

int pica_vram_copy(int sx, int sy, int dx, int dy, int w, int h, unsigned mask)
{
	struct copy_request r={sx,sy,dx,dy,w,h,mask,0};
	if (!thread_request(TH_COPY,0,0,0,0,&r)) copy_on_thread(&r);
	return r.result;
}

// ------------------------------------------------------------------ CPU uploads -> render target

// the CPU wrote a VRAM rectangle (upload, copy, state load): put it into
// the render target too, through a staging texture
static void push_to_target_reference(int x, int y, int w, int h)
{
	if (!ready || w <= 0 || h <= 0)
		return;
	if (!frame_open) {   // e.g. a state load: do it when the next GPU frame opens
		if (npend == 16) {   // too many: everything, once
			pend[0] = (prect){ 0, 0, VRAM_W, VRAM_H };
			npend = 1;
		}
		if (!(npend == 1 && pend[0].w == VRAM_W && pend[0].h == VRAM_H))
			pend[npend++] = (prect){ x, y, w, h };
		return;
	}
	// parts past the right or bottom edge wrap around to the left or top
	x &= VRAM_W - 1;
	y &= VRAM_H - 1;
	if (x + w > VRAM_W || y + h > VRAM_H) {
		int w1 = x + w > VRAM_W ? VRAM_W - x : w, h1 = y + h > VRAM_H ? VRAM_H - y : h;
		push_to_target(x, y, w1, h1);
		if (w1 < w) push_to_target(0, y, w - w1, h1);
		if (h1 < h) push_to_target(x, 0, w1, h - h1);
		if (w1 < w && h1 < h) push_to_target(0, 0, w - w1, h - h1);
		return;
	}
	// the stage is smaller than VRAM (GPU memory is scarce): bigger areas go in pieces
	if (w > STAGE_W || h > STAGE_H) {
		for (int py = y; py < y + h; py += STAGE_H)
			for (int px = x; px < x + w; px += STAGE_W)
				push_to_target(px, py, x + w - px < STAGE_W ? x + w - px : STAGE_W,
					y + h - py < STAGE_H ? y + h - py : STAGE_H);
		return;
	}
	// Flush first: emitting deferred effects or running out of vertices
	// can synchronize and reset stage_y. Reserve the upload only after
	// those barriers, so a later upload cannot overwrite its pending data.
	flush();
	if (vpos + 6 > vlimit || stage_y + h > STAGE_H)
		gpu_sync(stage_y + h > STAGE_H ? SY_STAGE : SY_VERTICES);
	if (pica_async && pica_frame_pending && pica_frame_pending() && pica_wait_frame) {
		pica_wait_frame();
		st_prev_upload++;
		synced_seq = frame_seq;
	}
	int sy = stage_y;
	stage_y += h;
	u16 *dst = stage_tex.data;
	const u16 *vram = gpu.vram;
	for (int r = 0; r < h; r++) {
		const u16 *src = vram + (y + r) * VRAM_W;
		// like the texture pages: stage row r is sampled at t = (r + 0.5) / STAGE_H
		for (int c = 0; c < w; c++)
			dst[tiled(c, STAGE_H - 1 - (sy + r), STAGE_W)] = pixel_5551(src[x + c]);
	}
	{   // the 8-row tile rows written
		int tr0 = (STAGE_H - 1 - (sy + h - 1)) >> 3, tr1 = (STAGE_H - 1 - sy) >> 3;
		GSPGPU_FlushDataCache(dst + tr0 * (STAGE_W / 8) * 64, (tr1 - tr0 + 1) * (STAGE_W / 8) * 64 * 2);
	}
	if (base_dirty)
		apply_base();
	// copied 1:1, unblended, over everything
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
	set_area(0, 0, VRAM_W, VRAM_H);
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	C3D_TexEnvInit(C3D_GetTexEnv(1));   // CPU uploads must never be dithered
	applied_dither = -1;
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	alias_i ^= 1;
	tex_alias[alias_i] = stage_tex;
	C3D_TexBind(0, &tex_alias[alias_i]);
	clear_tex_cache();
	s16 x0 = x, y0 = y, x1 = x + w, y1 = y + h, sw = w, v0 = sy, v1 = sy + h;
	// u, v in stage texels; the shader scale is switched to the stage size
	pvtx q[6] = {
		{ x0, y0, 0, v0, 0 }, { x1, y0, sw, v0, 0 }, { x0, y1, 0, v1, 0 },
		{ x1, y0, sw, v0, 0 }, { x1, y1, sw, v1, 0 }, { x0, y1, 0, v1, 0 } };
	to_gpu(q, 6);
	memcpy(vbuf + vpos, q, sizeof(q));
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_texscale, 1.0f / STAGE_W, 1.0f / STAGE_H, 0, 0);
	C3D_DrawArrays(GPU_TRIANGLES, vpos, 6);   // the next textured batch sets its own scale
	vpos += 6;
	vstart = vpos;
	seq++;
	sent_valid = false;
	state_dirty = true;
	st_uploads++;
}

static void PICA_COLD push_to_target(int x, int y, int w, int h)
{
	gpu_readable = false;
	// gldbg=131072: baseline renderer CPU paths.
	if (pica_debug & 131072) { push_to_target_reference(x, y, w, h); return; }
	if (!ready || w <= 0 || h <= 0)
		return;
	if (!frame_open) {   // e.g. a state load: do it when the next GPU frame opens
		if (npend == 16) {   // too many: everything, once
			pend[0] = (prect){ 0, 0, VRAM_W, VRAM_H };
			npend = 1;
		}
		if (!(npend == 1 && pend[0].w == VRAM_W && pend[0].h == VRAM_H))
			pend[npend++] = (prect){ x, y, w, h };
		return;
	}
	// parts past the right or bottom edge wrap around to the left or top
	x &= VRAM_W - 1;
	y &= VRAM_H - 1;
	if (x + w > VRAM_W || y + h > VRAM_H) {
		int w1 = x + w > VRAM_W ? VRAM_W - x : w, h1 = y + h > VRAM_H ? VRAM_H - y : h;
		push_to_target(x, y, w1, h1);
		if (w1 < w) push_to_target(0, y, w - w1, h1);
		if (h1 < h) push_to_target(x, 0, w1, h - h1);
		if (w1 < w && h1 < h) push_to_target(0, 0, w - w1, h - h1);
		return;
	}
	// the stage is smaller than VRAM (GPU memory is scarce): bigger areas go in pieces
	if (w > STAGE_W || h > STAGE_H) {
		for (int py = y; py < y + h; py += STAGE_H)
			for (int px = x; px < x + w; px += STAGE_W)
				push_to_target(px, py, x + w - px < STAGE_W ? x + w - px : STAGE_W,
					y + h - py < STAGE_H ? y + h - py : STAGE_H);
		return;
	}
	// Flush first: emitting deferred effects or running out of vertices
	// can synchronize and reset stage_y. Reserve the upload only after
	// those barriers, so a later upload cannot overwrite its pending data.
	flush();
	if (vpos + 6 > vlimit || stage_y + h > STAGE_H)
		gpu_sync(stage_y + h > STAGE_H ? SY_STAGE : SY_VERTICES);
	if (pica_async && pica_frame_pending && pica_frame_pending() && pica_wait_frame) {
		pica_wait_frame();
		st_prev_upload++;
		synced_seq = frame_seq;
	}
	int sy = stage_y;
	stage_y += h;
	u16 *dst = stage_tex.data;
	const u16 *vram = gpu.vram;
	static const u8 xoff[8] = { 0, 1, 4, 5, 16, 17, 20, 21 };
	for (int r = 0; r < h; r++) {
		const u16 *src = vram + (y + r) * VRAM_W + x;
		// Adjacent pairs stay adjacent in Morton order. Convert both pixels
		// together; memcpy also accepts an odd VRAM x.
		u16 *row = dst + tiled(0, STAGE_H - 1 - (sy + r), STAGE_W);
		int c = 0;
		for (; c + 1 < w; c += 2) {
			u32 p;
			memcpy(&p, src + c, sizeof(p));
			u32 q = ((p & 0x001f001f) << 11) | ((p & 0x03e003e0) << 1) |
				((p & 0x7c007c00) >> 9) | ((p >> 15) & 0x00010001);
			*(u32a *)(row + (c >> 3) * 64 + xoff[c & 7]) = q;
		}
		if (c < w)
			row[(c >> 3) * 64 + xoff[c & 7]] = pixel_5551(src[c]);
	}
	{   // the 8-row tile rows written
		int tr0 = (STAGE_H - 1 - (sy + h - 1)) >> 3, tr1 = (STAGE_H - 1 - sy) >> 3;
		GSPGPU_FlushDataCache(dst + tr0 * (STAGE_W / 8) * 64, (tr1 - tr0 + 1) * (STAGE_W / 8) * 64 * 2);
	}
	if (base_dirty)
		apply_base();
	// copied 1:1, unblended, over everything
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
	set_area(0, 0, VRAM_W, VRAM_H);
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	C3D_TexEnvInit(C3D_GetTexEnv(1));   // CPU uploads must never be dithered
	applied_dither = -1;
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	alias_i ^= 1;
	tex_alias[alias_i] = stage_tex;
	C3D_TexBind(0, &tex_alias[alias_i]);
	clear_tex_cache();
	s16 x0 = x, y0 = y, x1 = x + w, y1 = y + h, sw = w, v0 = sy, v1 = sy + h;
	// u, v in stage texels; the shader scale is switched to the stage size
	pvtx q[6] = {
		{ x0, y0, 0, v0, 0 }, { x1, y0, sw, v0, 0 }, { x0, y1, 0, v1, 0 },
		{ x1, y0, sw, v0, 0 }, { x1, y1, sw, v1, 0 }, { x0, y1, 0, v1, 0 } };
	to_gpu(q, 6);
	memcpy(vbuf + vpos, q, sizeof(q));
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_texscale, 1.0f / STAGE_W, 1.0f / STAGE_H, 0, 0);
	C3D_DrawArrays(GPU_TRIANGLES, vpos, 6);   // the next textured batch sets its own scale
	vpos += 6;
	vstart = vpos;
	seq++;
	sent_valid = false;
	state_dirty = true;
	st_uploads++;
}

// A state replaces CPU VRAM. Old deferred fills/readbacks must never overwrite
// it; update_caches below will queue the upload for the next open GPU frame.
static void pica_drop_fills_local(void)
{
	texmemo_epoch++;
	rtt_detected = rtt_cache_rebuild = false;
	nfills = npend = 0;
	memset(drawn, 0, sizeof(drawn));
	memset(fine_drawn, 0, sizeof(fine_drawn));
	memset(fine_snapshot, 0, sizeof(fine_snapshot));
	memset(fine_gen, 0, sizeof(fine_gen));
	memset(fine_draw_gen, 0, sizeof(fine_draw_gen));
	memset(cpu_row_gen, 0, sizeof(cpu_row_gen));
	known_fx0 = 1; known_fx1 = 0;
	alt_fx0 = 1; alt_fx1 = 0;
	known_bx0 = 1; known_bx1 = 0;
	have_last = false;
}

// ------------------------------------------------------------------ gpulib renderer interface

int renderer_init(void)
{
	pica_drop_fills_local();
	memset(&ps, 0, sizeof(ps));
	ps.dx2 = VRAM_W;
	ps.dy2 = VRAM_H;
	ps.area_gen = 1;
	return 0;
}

void renderer_finish(void) {}
void renderer_notify_screen_change(const struct psx_gpu_screen *screen) { (void)screen; }
void renderer_set_interlace(int enable, int is_odd) { (void)enable; (void)is_odd; }
void renderer_set_config(const struct rearmed_cbs *config)
{
	// Vertices already queued retain their captured state. The next primitive
	// gets the new option and use_state splits the batch when necessary.
	dither_config = config->dithering;
}
static void renderer_flush_queues_local(void)
{
	flush();
}

static void renderer_sync_ecmds_local(uint32_t *ecmds)
{
	for (int i = 1; i <= 6; i++)
		set_ex(ecmds[i]);
}

static void renderer_update_caches_local(int x, int y, int w, int h, int state_changed)
{
	if (state_changed)
		disp_used = false;
	trace_upload(x, y, w, h);
	mark_vram(x, y, w, h);
	push_to_target(x, y, w, h);
}

#ifdef PICA_LAYOUT_SPLIT
static PICA_COLD int line_cmd(u32 *list, u32 *list_end, u32 cmd, u32 *len_out, int *sum_out, int *cycles_out)
{
 u32 len = *len_out;
 int cpu_cycles_sum = 0, cpu_cycles = *cycles_out;
 // Write accumulated timing even on an incomplete polyline.
 #define LINE_PARTIAL() do { *sum_out += cpu_cycles_sum; *cycles_out = cpu_cycles; return -1; } while (0)
 switch (cmd) {
		case 0x40 ... 0x47:
			draw_line(sext11(sext11(list[1]) + ps.ox), sext11(sext11(list[1] >> 16) + ps.oy), list[0] & 0xffffff,
				sext11(sext11(list[2]) + ps.ox), sext11(sext11(list[2] >> 16) + ps.oy), list[0] & 0xffffff, blend_of(cmd));
			gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
			break;
		case 0x48 ... 0x4f: {   // flat polyline: xy words until a 0x5xxx5xxx terminator
			u32 *p = list + 1;
			for (bool first = true;; first = false) {
				if (p + 1 >= list_end) { cmd = -1; LINE_PARTIAL(); }
				// the second vertex is always a vertex; the terminator can follow it
				if (!first && (p[1] & 0xf000f000) == 0x50005000)
					break;
				draw_line(sext11(sext11(p[0]) + ps.ox), sext11(sext11(p[0] >> 16) + ps.oy), list[0] & 0xffffff,
					sext11(sext11(p[1]) + ps.ox), sext11(sext11(p[1] >> 16) + ps.oy), list[0] & 0xffffff, blend_of(cmd));
				gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
				p++;
			}
			len = (p + 1) - list;   // through the terminator
		} break;
		case 0x50 ... 0x57:
			draw_line(sext11(sext11(list[1]) + ps.ox), sext11(sext11(list[1] >> 16) + ps.oy), list[0] & 0xffffff,
				sext11(sext11(list[3]) + ps.ox), sext11(sext11(list[3] >> 16) + ps.oy), list[2] & 0xffffff, blend_of(cmd));
			gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
			break;
		case 0x58 ... 0x5f: {   // gouraud polyline: colour, xy pairs until the terminator
			u32 *p = list;
			for (bool first = true;; first = false) {
				if (p + 2 >= list_end) { cmd = -1; LINE_PARTIAL(); }
				// the second colour is always a colour; the terminator can follow it
				if (!first && (p[2] & 0xf000f000) == 0x50005000)
					break;
				if (p + 3 >= list_end) { cmd = -1; LINE_PARTIAL(); }
				draw_line(sext11(sext11(p[1]) + ps.ox), sext11(sext11(p[1] >> 16) + ps.oy), p[0] & 0xffffff,
					sext11(sext11(p[3]) + ps.ox), sext11(sext11(p[3] >> 16) + ps.oy), p[2] & 0xffffff, blend_of(cmd));
				gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
				p += 2;
			}
			len = (p + 2) - list;
		} break;

 }
 *len_out = len;
 *sum_out += cpu_cycles_sum;
 *cycles_out = cpu_cycles;
 #undef LINE_PARTIAL
 return 0;
}
#endif

extern int psx3d_skipgate;
static int PICA_HOT(renderer_do_cmd_list_impl) renderer_do_cmd_list_impl(uint32_t *list_, int list_len, uint32_t *ex_regs,
	int *cycles_sum_out, int *cycles_last, int *last_cmd)
{
#ifdef PICA_TIMERS
	u64 tl0 = svcGetSystemTick();
#endif
	int cpu_cycles_sum = 0, cpu_cycles = *cycles_last;
	u32 *list = (u32 *)list_, *list_start = list, *list_end = list + list_len;
	if ((pica_debug & 4194304) && !frame_open && pica_open_frame && ready) pica_open_frame();
	u32 cmd = 0, len;
	const bool skip_draw = !ready || (pica_debug & 2048);

	for (; list < list_end; list += 1 + len) {
		cmd = list[0] >> 24;
		len = cmd_lengths[cmd];
		if (list + 1 + len > list_end) {
			cmd = -1;
			break;
		}
		// State-only lists must not open/wait for a GPU frame.
		if (!frame_open && pica_open_frame && ready && cmd >= 0x02 && cmd < 0x80)
			pica_open_frame();
		// without its buffers (no game started, or no memory) nothing is drawn;
		// debug 2048: drawing commands are skipped (the rest of the emulator alone)
		if (skip_draw && cmd >= 0x02 && cmd < 0x80 && (cmd & 0xf8) != 0x48 && (cmd & 0xf8) != 0x58)
			continue;
		switch (cmd) {
		case 0x02:
			fill_rect(list);
			gput_sum(cpu_cycles_sum, cpu_cycles, gput_fill(list[2] & 0x3ff, (list[2] >> 16) & 0x1ff));
			break;
		case 0x20 ... 0x3f: {
			draw_poly(list, cmd);
			int c = (cmd & 0x10) ? ((cmd & 4) ? gput_poly_base_gt() : gput_poly_base_g())
				: ((cmd & 4) ? gput_poly_base_t() : gput_poly_base());
			gput_sum(cpu_cycles_sum, cpu_cycles, c);
		} break;
#ifdef PICA_LAYOUT_SPLIT
  case 0x40 ... 0x5f:
   if (PICA_UNLIKELY(line_cmd(list, list_end, cmd, &len, &cpu_cycles_sum, &cpu_cycles) < 0)) {
    cmd = -1;
    goto breakloop;
   }
   break;
#else
		case 0x40 ... 0x47:
			draw_line(sext11(sext11(list[1]) + ps.ox), sext11(sext11(list[1] >> 16) + ps.oy), list[0] & 0xffffff,
				sext11(sext11(list[2]) + ps.ox), sext11(sext11(list[2] >> 16) + ps.oy), list[0] & 0xffffff, blend_of(cmd));
			gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
			break;
		case 0x48 ... 0x4f: {   // flat polyline: xy words until a 0x5xxx5xxx terminator
			u32 *p = list + 1;
			for (bool first = true;; first = false) {
				if (p + 1 >= list_end) { cmd = -1; goto breakloop; }
				// the second vertex is always a vertex; the terminator can follow it
				if (!first && (p[1] & 0xf000f000) == 0x50005000)
					break;
				draw_line(sext11(sext11(p[0]) + ps.ox), sext11(sext11(p[0] >> 16) + ps.oy), list[0] & 0xffffff,
					sext11(sext11(p[1]) + ps.ox), sext11(sext11(p[1] >> 16) + ps.oy), list[0] & 0xffffff, blend_of(cmd));
				gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
				p++;
			}
			len = (p + 1) - list;   // through the terminator
		} break;
		case 0x50 ... 0x57:
			draw_line(sext11(sext11(list[1]) + ps.ox), sext11(sext11(list[1] >> 16) + ps.oy), list[0] & 0xffffff,
				sext11(sext11(list[3]) + ps.ox), sext11(sext11(list[3] >> 16) + ps.oy), list[2] & 0xffffff, blend_of(cmd));
			gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
			break;
		case 0x58 ... 0x5f: {   // gouraud polyline: colour, xy pairs until the terminator
			u32 *p = list;
			for (bool first = true;; first = false) {
				if (p + 2 >= list_end) { cmd = -1; goto breakloop; }
				// the second colour is always a colour; the terminator can follow it
				if (!first && (p[2] & 0xf000f000) == 0x50005000)
					break;
				if (p + 3 >= list_end) { cmd = -1; goto breakloop; }
				draw_line(sext11(sext11(p[1]) + ps.ox), sext11(sext11(p[1] >> 16) + ps.oy), p[0] & 0xffffff,
					sext11(sext11(p[3]) + ps.ox), sext11(sext11(p[3] >> 16) + ps.oy), p[2] & 0xffffff, blend_of(cmd));
				gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
				p += 2;
			}
			len = (p + 2) - list;
		} break;
#endif
		case 0x60 ... 0x7f: {
			draw_rect(list, cmd);
			int sw = 16, sh = 16;
			switch ((cmd >> 3) & 3) {
			case 0: { u32 wh = list[(cmd & 4) ? 3 : 2]; sw = wh & 0x3ff; sh = (wh >> 16) & 0x1ff; } break;
			case 1: sw = sh = 1; break;
			case 2: sw = sh = 8; break;
			}
			gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(sw, sh));
		} break;
		case 0x1f:
		case 0x80 ... 0xdf:
			goto breakloop;   // gpulib handles transfers and the irq
		case 0xe1 ... 0xe6:
			ex_regs[cmd & 7] = list[0];
			set_ex(list[0]);
			// A pending presentation still needs the previous batch boundary.
			// Only return early while holding an already presented picture.
			if (cmd == 0xe3 && psx3d_skipgate && gpu.frameskip.active &&
				!gpu.frameskip.allow && !gpu.frameskip.frame_ready && !gpu_async_enabled(&gpu)) {
				list += 1 + len;
				goto breakloop;
			}
			break;
		default:
			break;
		}
	}
breakloop:
	ex_regs[1] = (ex_regs[1] & ~0x1ff) | (ps.texpage & 0x1ff);
	*cycles_sum_out += cpu_cycles_sum;
	*cycles_last = cpu_cycles;
	*last_cmd = cmd;
#ifdef PICA_TIMERS
	t_list += svcGetSystemTick() - tl0;
#endif
	n_list++;
	return list - list_start;
}

int PICA_HOT(renderer_do_cmd_list) renderer_do_cmd_list(uint32_t *list, int len, uint32_t *regs,
 int *sum, int *last_cycles, int *last_cmd)
{
 if (pica_pmu) pica_pmu(0);
 u64 started = phase_begin(0);
 int result = renderer_do_cmd_list_impl(list, len, regs, sum, last_cycles, last_cmd);
 phase_end(0, started);
 if (pica_pmu) pica_pmu(1);
 return result;
}

// ------------------------------------------------------------------ frontend interface

bool pica_init(const void *shbin, unsigned size)
{
	if (dvlb)
		return true;
	dvlb = DVLB_ParseFile((u32 *)shbin, size);
	if (!dvlb)
		return false;
	shaderProgramInit(&prog);
	shaderProgramSetVsh(&prog, &dvlb->DVLE[0]);
	u_proj = shaderInstanceGetUniformLocation(prog.vertexShader, "proj");
	u_texscale = shaderInstanceGetUniformLocation(prog.vertexShader, "texscale");
	// the PS1 VRAM render target lives in VRAM, not in the scarce linear memory
	if (!C3D_TexInitVRAM(&vram_tex, VRAM_W, VRAM_H, GPU_RGBA5551))
		goto fail;
	target = C3D_RenderTargetCreateFromTex(&vram_tex, GPU_TEXFACE_2D, 0, -1);
	if (!target)
		goto fail;
	C3D_TexSetFilter(&vram_tex, GPU_NEAREST, GPU_NEAREST);
	return true;
fail:
	pica_exit();
	return false;
}

static void free_game_buffers(void)
{
	disp_used = false;
	if (disp_tex.data)
		C3D_TexDelete(&disp_tex);
	memset(&disp_tex, 0, sizeof(disp_tex));
	if (atlas_target) C3D_RenderTargetDelete(atlas_target);
	atlas_target = NULL;
	if (atlas[0].tex.data)
		C3D_TexDelete(&atlas[0].tex);
	if (stage_tex.data)
		C3D_TexDelete(&stage_tex);
	if (dither_tex.data)
		C3D_TexDelete(&dither_tex);
	if (vbuf)
		linearFree(vbuf_base);
	memset(&atlas[0].tex, 0, sizeof(atlas[0].tex));
	memset(&stage_tex, 0, sizeof(stage_tex));
	memset(&dither_tex, 0, sizeof(dither_tex));
	vbuf = vbuf_base = NULL;
}

// the big buffers only exist while a game runs (the library needs the memory)
bool pica_game_begin(bool small)
{
	texmemo_epoch++;
	if (!dvlb)
		return false;
	free_game_buffers();
	ready = false;
	(void)small;
	if (!C3D_TexInit(&stage_tex, STAGE_W, STAGE_H, GPU_RGBA5551))
		goto fail;
	if (!C3D_TexInit(&disp_tex, VRAM_W, VRAM_H, GPU_RGBA5551))
		goto fail;
	// Clean once: the CPU never writes this GPU-owned texture again.
	GSPGPU_FlushDataCache(disp_tex.data, VRAM_W * VRAM_H * 2);
	C3D_TexSetFilter(&disp_tex, GPU_NEAREST, GPU_NEAREST);
	disp_used = false;
	C3D_TexSetFilter(&stage_tex, GPU_NEAREST, GPU_NEAREST);
	vbuf = vbuf_base = linearAlloc(MAX_VERTS * sizeof(pvtx));
	if (!vbuf)
		goto fail;
	// the atlas, and each entry's fixed place in it
	atlas[0].w = 1024;
	atlas[0].h = 1024;
	if (!C3D_TexInit(&atlas[0].tex, atlas[0].w, atlas[0].h, GPU_RGBA8))
		goto fail;
	atlas_target = C3D_RenderTargetCreateFromTex(&atlas[0].tex, GPU_TEXFACE_2D, 0, -1);
	// Allocation failure retains the precise CPU fallback.
	C3D_TexSetFilter(&atlas[0].tex, GPU_NEAREST, GPU_NEAREST);
	C3D_TexSetWrap(&atlas[0].tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	if (!C3D_TexInit(&dither_tex, 8, 8, GPU_RGBA8))
		goto fail;
	C3D_TexSetFilter(&dither_tex, GPU_NEAREST, GPU_NEAREST);
	C3D_TexSetWrap(&dither_tex, GPU_REPEAT, GPU_REPEAT);
	{
		// ADD_SIGNED subtracts 128 from the sum. Coordinates are generated
		// from absolute VRAM position by the vertex shader, with no extra
		// vertex attribute or per-pixel CPU work. Rows are bottom-up.
		static const int matrix[4][4] = {
			{ -4, 0, -3, 1 }, { 2, -2, 3, -1 },
			{ -3, 1, -4, 0 }, { 3, -1, 2, -2 } };
		u32 *dst = dither_tex.data;
		for (int y = 0; y < 8; y++)
			for (int x = 0; x < 8; x++) {
				u32 c = 128 + matrix[y & 3][x & 3];
				dst[tiled(x, 7 - y, 8)] = (c << 24) | (c << 16) | (c << 8) | 255;
			}
		GSPGPU_FlushDataCache(dst, 8 * 8 * 4);
	}
	// all cells free but the reserved square's (the last one)
	for (int i = 0; i < CELLS * CELLS; i++)
		cell_owner[i] = CELL_FREE;
	memset(cell_rows, 0, sizeof(cell_rows));
	memset(nactive, 0, sizeof(nactive));
	for (nfree = 0; nfree < NENTRIES; nfree++)
		free_list[nfree] = NENTRIES - 1 - nfree;
	memset(cache, 0, sizeof(cache));
	// the reserved square: mid grey, opaque on the left, semi-transparent on the right
	plain = &plain_e;
	memset(plain, 0, sizeof(*plain));   // never valid: never found by lookups
	plain->ax = (CELLS - 1) * 32;
	plain->ay = (CELLS - 1) * 32;
	cells_set(CELLS - 1, CELLS - 1, 1, CELL_PLAIN);
	{
		u32 *dst = atlas[0].tex.data;
		int aw = atlas[0].w, ah = atlas[0].h;
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 32; x++)
				dst[tiled(plain->ax + x, ah - 1 - (plain->ay + y), aw)] = x < 16 ? 0x808080ff : 0x80808080;
		GSPGPU_FlushDataCache(dst, aw * ah * 4);
	}
	C3D_TexSetFilter(&stage_tex, GPU_NEAREST, GPU_NEAREST);
	C3D_TexSetFilter(&vram_tex, GPU_NEAREST, GPU_NEAREST);
	memset(buckets, 0, sizeof(buckets));
	vpos = vstart = 0;
	ready = true;
	return true;
fail:
	free_game_buffers();
	return false;
}

void pica_game_end(void)
{
	ready = false;
	have_last = false;
	frame_open = false;
	pica_drop_fills_local();
	free_game_buffers();
}

void pica_exit(void)
{
	pica_game_end();
	if (!dvlb)
		return;
	if (target) C3D_RenderTargetDelete(target);
	target = NULL;
	if (vram_tex.data) C3D_TexDelete(&vram_tex);
	memset(&vram_tex, 0, sizeof(vram_tex));
	shaderProgramFree(&prog);
	DVLB_Free(dvlb);
	dvlb = NULL;
}

// gpulib, at the flip of a frame that was drawn (vout_update). With
// frameskip, the shown area is copied so skipped frames keep showing it.
static u32 disp_serial;
static void pica_flip_local(void)
{
	psx3d_skip.pictures++;
	// A display flip can arrive on an idle vblank, after the drawing frame
	// was submitted. It still needs a snapshot of the newly displayed bank.
	if (ready && gpu.frameskip.set &&
	    !(gpu.status & PSX_GPU_STATUS_RGB24) && !frame_open && pica_open_frame)
		pica_open_frame();
	disp_used = ready && frame_open && gpu.frameskip.set && !(gpu.status & PSX_GPU_STATUS_RGB24)
		&& gpu.screen.w > 0 && gpu.screen.h > 0;
	disp_x = gpu.screen.src_x;
	disp_y = gpu.screen.src_y;
	disp_w = gpu.screen.w;
	disp_h = gpu.screen.h;
	if (disp_used) {
		flush();
		disp_serial++;
		// Queue after the PS1 draws and before any skipped-frame clears.
		// Copy only the scanout's 8-row tile bands, at the same coordinates
		// in disp_tex. No readback, CPU pixel copy or GPU wait is needed.
		int y0 = (disp_y & (VRAM_H - 1)) & ~7;
		int y1 = (disp_y & (VRAM_H - 1)) + disp_h;
		if (y1 > VRAM_H) { y0 = 0; y1 = VRAM_H; }
		y1 = (y1 + 7) & ~7;
		u32 offset = y0 * VRAM_W * 2, size = (y1 - y0) * VRAM_W * 2;
		C3D_FrameSplit(0);
		st_splits++;
		GX_TextureCopy((u32 *)((u8 *)vram_tex.data + offset), 0,
			(u32 *)((u8 *)disp_tex.data + offset), 0, size, 8);
	}
}

// Finish PS1 work before the frontend prepares Citro2D or binds its target.
// pica_picture is deliberately a query: both stereo eyes can call it safely.
static void pica_prepare_present_local(void)
{
	flush();
	clear_tex_cache();
}

// Conservative: offscreen writes also force submission. Movies always present.
// Called after pica_thread_sync(), on the main thread, with no queued worker work.
static bool pica_display_changed_local(void)
{
	static u32 last[9];
	flush();
	u32 state[9] = { disp_used && gpu.frameskip.set ? disp_serial : seq, ready, gpu.status & (PSX_GPU_STATUS_RGB24 | PSX_GPU_STATUS_BLANKING),
		disp_used && gpu.frameskip.set, disp_x, disp_y, gpu.screen.w, gpu.screen.h,
		(gpu.screen.src_y << 10) | gpu.screen.src_x };
	// When retaining a drawn snapshot, skipped display flips aren't new pictures.
	if (disp_used && gpu.frameskip.set) state[8] = (disp_y << 10) | disp_x;
	bool changed = memcmp(last, state, sizeof(state)) != 0;
	memcpy(last, state, sizeof(state));
	return changed || npend || !ready || (gpu.status & PSX_GPU_STATUS_RGB24);
}

static void display_changed_on_thread(void *data)
{
	*(bool *)data = pica_display_changed_local();
}
bool pica_display_changed(void)
{
	bool changed;
	if (pica_thread_call && pica_thread_call(display_changed_on_thread, &changed)) return changed;
	return pica_display_changed_local();
}

// called by the frontend once per shown frame: the displayed VRAM area.
// PICA_PIC_GPU: draw tex; PICA_PIC_NONE: the display is off (black);
// PICA_PIC_CPU: 24-bit mode (movies), the core's own picture
static unsigned st_pic[3];   // frames shown per PICA_PIC_* result
static int pic_x, pic_y, pic_w, pic_h;   // the last area shown
static C3D_Tex *pic_tex;
struct picture_request {
	C3D_Tex **tex;
	float *l, *t, *r, *b, *w, *h;
	int result;
};
static void picture_on_thread(void *data)
{
	struct picture_request *p = data;
	p->result = pica_picture(p->tex, p->l, p->t, p->r, p->b, p->w, p->h);
}
int pica_picture(C3D_Tex **tex, float *l, float *t, float *r, float *b, float *w, float *h)
{
	if (pica_thread_call) {
		struct picture_request p = { tex, l, t, r, b, w, h, PICA_PIC_NONE };
		if (pica_thread_call(picture_on_thread, &p)) return p.result;
	}
	if (!ready || (gpu.status & PSX_GPU_STATUS_RGB24)) {
		st_pic[PICA_PIC_CPU]++;
		return PICA_PIC_CPU;
	}
	if ((gpu.status & PSX_GPU_STATUS_BLANKING) || gpu.screen.w <= 0 || gpu.screen.h <= 0) {
		st_pic[PICA_PIC_NONE]++;
		return PICA_PIC_NONE;
	}
	st_pic[PICA_PIC_GPU]++;
	int x = gpu.screen.src_x, y = gpu.screen.src_y, sw = gpu.screen.w, sh = gpu.screen.h;
	if (pica_debug & 32) {   // debug: the whole of VRAM
		x = 0; y = 0; sw = VRAM_W; sh = VRAM_H;
	} else if (disp_used && gpu.frameskip.set) {   // the last frame that was drawn
		x = disp_x; y = disp_y; sw = disp_w; sh = disp_h;
	}
	pic_x = x; pic_y = y; pic_w = sw; pic_h = sh;
	*tex = disp_used && gpu.frameskip.set && !(pica_debug & 32) ? &disp_tex : &vram_tex;
	pic_tex = *tex;
	*l = (float)x / VRAM_W;
	*r = (float)(x + sw) / VRAM_W;
	*t = 1.0f - (float)y / VRAM_H;
	*b = 1.0f - (float)(y + sh) / VRAM_H;
	*w = sw;
	*h = sh;
	return PICA_PIC_GPU;
}

static void PICA_COLD pica_stats_local(char *buf, int n)
{
	if (!t_svc) {   // what one timer read costs
		u64 a = svcGetSystemTick();
		for (int i = 0; i < 1000; i++)
			svcGetSystemTick();
		t_svc = (svcGetSystemTick() - a) / 1000;
	}
	int k0 = snprintf(buf, n, "pica: cmdlist %.2fms in %u calls, %u flushes, timer read %.2fus; ",
		t_list / 268111.856, n_list, n_flush, t_svc / 268.111856);
	t_list = 0; n_list = n_flush = 0;
	if (k0 < 0 || k0 >= n) return;
	buf += k0; n -= k0;
	int k = snprintf(buf, n, "state %.2fms draw %.2fms tex %.2fms; ends: atlas %u blend %u area %u rebind %u; evictions by size %u %u %u %u; ",
		t_state / 268111.856, t_draw / 268111.856, t_tex / 268111.856, st_why[0], st_why[1], st_why[2], st_why[3],
		st_evict_lg[0], st_evict_lg[1], st_evict_lg[2], st_evict_lg[3]);
	memset(st_why, 0, sizeof(st_why));
	memset(st_evict_lg, 0, sizeof(st_evict_lg));
	t_state = t_draw = t_tex = 0;
	if (k < 0 || k >= n) return;
	buf += k; n -= k;
	k = snprintf(buf, n, "sync reasons cpu %u tex %u clut %u evict %u inval %u grow %u verts %u stage %u prev %u/%u; ",
		st_sync_reason[0], st_sync_reason[1], st_sync_reason[2], st_sync_reason[3],
		st_sync_reason[4], st_sync_reason[5], st_sync_reason[6], st_sync_reason[7], st_prev_texture, st_prev_upload);
	memset(st_sync_reason, 0, sizeof(st_sync_reason));
	st_prev_texture = st_prev_upload = 0;
	if (k < 0 || k >= n) return;
	buf += k; n -= k;
	snprintf(buf, n, "%u batches %u prims %u tile decodes (%u evictions %u invalidations) %u uploads %u syncs %u readbacks %u splits, rb %u pixels %uK cache gpu tiles %u copies %u; %uK GPU memory free; area %d,%d-%d,%d off %d,%d disp %d,%d %dx%d rtt %s",
		st_batches, st_prims, st_decodes, st_evict, st_inval, st_uploads, st_syncs, st_readbacks, st_splits, st_read_pixels, st_read_bytes/1024, st_gpu_tiles, st_gpu_copies, (unsigned)(linearSpaceFree() / 1024),
		ps.dx1, ps.dy1, ps.dx2, ps.dy2, ps.ox, ps.oy, gpu.screen.src_x, gpu.screen.src_y, gpu.screen.w, gpu.screen.h, rtt_old() ? "old" : "new");
	st_read_pixels = st_read_bytes = st_gpu_tiles = st_gpu_copies = 0;
	st_batches = st_prims = st_decodes = st_uploads = st_syncs = st_evict = st_inval = st_readbacks = st_splits = 0;
	// atlas occupancy: cells owned, entries per size, and cells of entries
	// looked up since the previous frame started (the recent working set)
	unsigned used = 0, recent = 0, win = 0, tiles = 0, rtiles = 0;
	for (int i = 0; i < CELLS * CELLS; i++) used += cell_owner[i] != CELL_FREE;
	for (unsigned i = 0; i < NENTRIES; i++)
		if (cache[i].valid) {
			win += cache[i].win != 0;
			tiles += __builtin_popcountll(cache[i].tiles);
			if (cache[i].lru > lru_prev_frame) {
				recent += cache[i].cw * cache[i].ch;
				rtiles += __builtin_popcountll(cache[i].tiles);
			}
		}
	int len = strlen(buf);
	if (len < n)
		snprintf(buf + len, n - len, "; atlas cells %u (%u decoded) recent %u (%u decoded) entries %u %u %u %u win %u",
			used, tiles, recent, rtiles, nactive[0], nactive[1], nactive[2], nactive[3], win);
#ifdef PSX3D_SIM
	if (getenv("PICA_DUPS")) {   // page tiles held by more than one entry of the same key
		unsigned dup = 0;
		for (unsigned i = 0; i < NENTRIES; i++) {
			const tcache *a = &cache[i];
			if (!a->valid) continue;
			for (unsigned j = 0; j < i; j++) {
				const tcache *b = &cache[j];
				if (!b->valid || a->depth != b->depth || a->page != b->page || a->clut != b->clut || a->win != b->win) continue;
				int x0 = a->rx > b->rx ? a->rx : b->rx, x1 = a->rx + 32 * a->cw < b->rx + 32 * b->cw ? a->rx + 32 * a->cw : b->rx + 32 * b->cw;
				int y0 = a->ry > b->ry ? a->ry : b->ry, y1 = a->ry + 32 * a->ch < b->ry + 32 * b->ch ? a->ry + 32 * a->ch : b->ry + 32 * b->ch;
				if (x1 > x0 && y1 > y0) dup += ((x1 - x0) >> 5) * ((y1 - y0) >> 5);
			}
		}
		len = strlen(buf);
		if (len < n) snprintf(buf + len, n - len, " dup %u", dup);
	}
#endif
}

// test (stats): what was shown in the last second, and how bright the shown
// area of the render target is now (8x8 samples, 0-31; no GPU sync)
static void PICA_COLD pica_probe_local(char *buf, int n)
{
	int lum = -1;
	if (ready && pic_tex && pic_tex->data && pic_w > 0 && pic_h > 0) {
		const u16 *src = pic_tex->data;
		if (pic_tex == &disp_tex) // linear memory written only by the GPU
			GSPGPU_InvalidateDataCache(src, VRAM_W * VRAM_H * 2);
		int sum = 0;
		for (int j = 0; j < 8; j++)
			for (int i = 0; i < 8; i++) {
				int x = (pic_x + pic_w * (2 * i + 1) / 16) & (VRAM_W - 1);
				int y = (pic_y + pic_h * (2 * j + 1) / 16) & (VRAM_H - 1);
				u16 p = src[tiled(x, y, VRAM_W)];
				sum += ((p >> 11) + ((p >> 6) & 31) + ((p >> 1) & 31)) / 3;
			}
		lum = sum / 64;
	}
	snprintf(buf, n, "pic gpu %u none %u cpu %u; status %08x screen %d,%d %dx%d; shown %d,%d %dx%d lum %d",
		st_pic[PICA_PIC_GPU], st_pic[PICA_PIC_NONE], st_pic[PICA_PIC_CPU], (unsigned)gpu.status,
		gpu.screen.src_x, gpu.screen.src_y, gpu.screen.w, gpu.screen.h, pic_x, pic_y, pic_w, pic_h, lum);
	memset(st_pic, 0, sizeof(st_pic));
}

// ------------------------------------------------------------------ PSX3D stereo hooks
// Reserved entry points for GPU stereo integration. Mode 2 currently uses screen depth.
uint16_t *psx3d_vram2;
volatile uint32_t psx3d_ov_seq;
void psx3d_stereo_set(int on) { (void)on; }
void psx3d_set_strength(float px) { (void)px; }
void psx3d_set_half3d(int on) { (void)on; }
void psx3d_depth_frame(void) {}
void psx3d_depth_cmd(const uint32_t *list, uint32_t cmd) { (void)list; (void)cmd; }
const void *psx3d_depth_get(void) { return NULL; }
const uint16_t *psx3d_right_get(int *x, int *y, int *w, int *h, int *rgb24) { (void)x; (void)y; (void)w; (void)h; (void)rgb24; return NULL; }
const uint16_t *psx3d_overlay_get(int *x, int *y, int *w, int *h, const uint32_t **rows) { (void)x; (void)y; (void)w; (void)h; (void)rows; return NULL; }
void psx3d_stats(float *zc, int *n3d, int *n2d, int *dmin, int *dmax, int *zmin, int *zmax)
{
	*zc = 0; *n3d = *n2d = *dmin = *dmax = *zmin = *zmax = 0;
}

// All requests are synchronous; buffers and strings can live on the caller's
// stack. No frontend callback runs concurrently with emulation.
static void thread_execute(void *data)
{
  struct thread_request *r = data;
  switch (r->op) {
  case TH_SETTLE_RECT: pica_settle_rect_local(r->x, r->y, r->w, r->h); break;
  case TH_SETTLE_VRAM: pica_settle_vram_local(); break;
  case TH_DROP_FILLS: pica_drop_fills_local(); break;
  case TH_READ_RECT: pica_readback_rect_local(r->x, r->y, r->w, r->h); break;
  case TH_READ_ALL: pica_readback_all_local(); break;
  case TH_DUMP: pica_dump_vram_local(r->data); break;
  case TH_SYNCE: renderer_sync_ecmds_local(r->data); break;
  case TH_UPDATE: renderer_update_caches_local(r->x, r->y, r->w, r->h, 0); break;
  case TH_FLUSH: renderer_flush_queues_local(); break;
  case TH_FLIP: pica_flip_local(); break;
  case TH_PREPARE_PRESENT: pica_prepare_present_local(); break;
  case TH_COPY: copy_on_thread(r->data); break;
  case TH_STATS: pica_stats_local(r->data, r->x); break;
  case TH_PROBE: pica_probe_local(r->data, r->x); break;
  case TH_FRAME_START: pica_frame_started_local(); break;
  case TH_FRAME_END: pica_frame_ended_local(); break;
  }
}

// External boundaries only. Internal calls above stay direct on both models.
void pica_settle_rect(int x, int y, int w, int h)
{
	if (thread_request(TH_SETTLE_RECT, x, y, w, h, NULL)) return;
	pica_settle_rect_local(x, y, w, h);
}

void pica_settle_vram(void)
{
	if (thread_request(TH_SETTLE_VRAM, 0, 0, 0, 0, NULL)) return;
	pica_settle_vram_local();
}

void pica_readback_rect(int x, int y, int w, int h)
{
	if (thread_request(TH_READ_RECT, x, y, w, h, NULL)) return;
	pica_readback_rect_local(x, y, w, h);
}

void pica_readback_all(void)
{
	if (thread_request(TH_READ_ALL, 0, 0, 0, 0, NULL)) return;
	pica_readback_all_local();
}

void pica_dump_vram(const char *path)
{
	if (thread_request(TH_DUMP, 0, 0, 0, 0, (void *)path)) return;
	pica_dump_vram_local(path);
}

void pica_frame_started(void)
{
	if (thread_request(TH_FRAME_START, 0, 0, 0, 0, NULL)) return;
	pica_frame_started_local();
}

void pica_frame_ended(void)
{
	if (thread_request(TH_FRAME_END, 0, 0, 0, 0, NULL)) return;
	pica_frame_ended_local();
}

void pica_drop_fills(void)
{
	if (thread_request(TH_DROP_FILLS, 0, 0, 0, 0, NULL)) return;
	pica_drop_fills_local();
}

void renderer_sync_ecmds(uint32_t *ecmds)
{
	if (thread_request(TH_SYNCE, 0, 0, 0, 0, ecmds)) return;
	renderer_sync_ecmds_local(ecmds);
}

void renderer_update_caches(int x, int y, int w, int h, int state_changed)
{
	if (thread_request(TH_UPDATE, x, y, w, h, NULL)) return;
	renderer_update_caches_local(x, y, w, h, state_changed);
}

void renderer_flush_queues(void)
{
	if (thread_request(TH_FLUSH, 0, 0, 0, 0, NULL)) return;
	renderer_flush_queues_local();
}

void pica_flip(void)
{
	if (thread_request(TH_FLIP, 0, 0, 0, 0, NULL)) return;
	pica_flip_local();
}

void pica_stats(char *buf, int n)
{
	if (thread_request(TH_STATS, n, 0, 0, 0, buf)) return;
	pica_stats_local(buf, n);
}

void pica_probe(char *buf, int n)
{
	if (thread_request(TH_PROBE, n, 0, 0, 0, buf)) return;
	pica_probe_local(buf, n);
}

void pica_prepare_present(void)
{
	if (thread_request(TH_PREPARE_PRESENT, 0, 0, 0, 0, NULL)) return;
	pica_prepare_present_local();
}

// G1 is correctness-on; the switch is read only on RGB24 scanout.
// The RGB24 frontend reads packed bytes from CPU VRAM, including movie
// borders that may last have been cleared/drawn in 16-bit GPU mode. Settle
// old fills before newer drawing; dirty tracking makes later movie frames cheap.
struct cpu_scanout_request { int x, y, w, h; };
static noinline __attribute__((cold)) void cpu_scanout_on_thread(void *data)
{
	const struct cpu_scanout_request *r = data;
	pica_settle_vram_local();
	pica_readback_rect_local(r->x, r->y, r->w, r->h);
}
noinline __attribute__((cold)) void pica_prepare_cpu_scanout(int offset, int w, int h)
{
	// Packed RGB bytes can begin at an odd byte after left clipping.
	struct cpu_scanout_request r = { ((unsigned)offset >> 1) & 1023, ((unsigned)offset >> 11) & 511,
		((unsigned)w * 3 + (offset & 1) + 1) >> 1, h };
	if (pica_thread_call && pica_thread_call(cpu_scanout_on_thread, &r)) return;
	cpu_scanout_on_thread(&r);
}
