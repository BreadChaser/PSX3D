// Video: GPU scanout presentation; RGB24 movies retain the CPU staging path.
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "psx3d.h"
#include "widescreen.h"
#include "gpustereo.h"
#include "display.h"
#include "../../core/plugins/gpu_pica/gpu_pica.h"
#include "../../core/plugins/gpulib/gpu.h"
#include "pica_shbin.h"
#include "../../core/libpcsxcore/psx3d_depth.h"


#define TEX_W 1024
#define TEX_H 512

#define TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) | \
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) | \
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

static C3D_RenderTarget *top, *top_r, *bottom;
static bool stereo_on;
// UI screens in 3D: draw_top is called once per eye; ui_eye is -1 (left),
// +1 (right) or 0 (no 3D). A layer at depth d (>0 behind the screen, <0 in
// front) is shifted by ui_eye * d * ui_parallax pixels.
void (*video_pre_draw)(void);   // GPU work to queue at frame start (cover uploads)
int ui_eye;
float ui_parallax;
bool ui_wants_3d;

// 3D slider fully up pushes the game picture this many pixels behind the screen
#define MAX_DEPTH_PX 8.0f
static C3D_Tex tex;
static u16 *fbs[2];          // linear memory, TEX_W pixels per row
static int fb_cur;           // buffer the core renders into next
static u16 *pending;         // finished frame waiting to be copied to the texture
static unsigned frame_w = 320, frame_h = 240;
static int frame_pic_y, frame_pic_h;   // the picture's rows in that 24-bit frame
static bool have_frame;
static bool gpu_frame;
static u64 wait_ticks, xfer_ticks;
// test stats: GPU time per frame, and CPU time from FrameEnd to the next FrameBegin
static float gpu_ms;
static u64 gap_ticks, frame_end_tick;

void video_take_gpu_stats(float *gpu, u64 *gap)
{
	*gpu = gpu_ms; *gap = gap_ticks;
	gpu_ms = 0; gap_ticks = 0;
}
u64 stereo_prep_ticks, stereo_draw_ticks;

u64 video_take_wait_ticks(void)
{
	u64 t = wait_ticks;
	wait_ticks = 0;
	return t;
}

static bool top_aa = true;
// Old 3DS gameplay: the supersampled targets are used at 1x (their first
// 400x240, copied out unscaled). The game picture gains nothing from
// supersampling, and the GPU time it took is time the CPU then spends
// waiting for the previous frame (Crash +3%). No VRAM is left for
// separate targets.
static bool plain_ok, plain_now;
static bool in_menu_call;   // video_present_vsync: menus and the library

static C3D_RenderTarget *aa_target(gfx3dSide_t side)
{
	C3D_RenderTarget *t = C3D_RenderTargetCreate(480, 800, GPU_RB_RGBA8, GPU_RB_DEPTH16);   // 2.3MB each in VRAM
	if (!t)
		return NULL;
	C3D_RenderTargetSetOutput(t, GFX_TOP, side,
		GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
		GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
		GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_XY));
	return t;
}

static void top_set_plain(C3D_RenderTarget *t, gfx3dSide_t side, bool plain)
{
	t->frameBuf.width = plain ? 240 : 480;
	t->frameBuf.height = plain ? 400 : 800;
	C3D_RenderTargetSetOutput(t, GFX_TOP, side,
		GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
		GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
		GX_TRANSFER_SCALING(plain ? GX_TRANSFER_SCALE_NO : GX_TRANSFER_SCALE_XY));
}

// Real 3D needs a VRAM render target for the right eye: two supersampled
// top targets plus PS1 VRAM leave too little VRAM, so 3D gameplay uses
// native ones (GPU thread, between frames). When the game ends the
// library gets its supersampled targets back: all screen targets are made
// again in video_init's order, the one layout known to fit (the bottom
// target, left where it was, kept the second one from fitting).
static bool stereo_native;
bool video_stereo_targets(bool native)
{
	if (native ? (!top_aa || stereo_native) : !stereo_native)
		return true;
	C3D_RenderTargetDelete(top);
	C3D_RenderTargetDelete(top_r);
	top = top_r = NULL;
	void *pica_reserve = NULL;
	if (!native) {
		C3D_RenderTargetDelete(bottom);
		bottom = NULL;
		pica_reserve = vramAlloc(TEX_W * TEX_H * 2);
	}
	if (pica_reserve) {
		top = aa_target(GFX_LEFT);
		top_r = aa_target(GFX_RIGHT);
		if (!top || !top_r) {
			if (top) C3D_RenderTargetDelete(top);
			if (top_r) C3D_RenderTargetDelete(top_r);
			top = top_r = NULL;
		}
	}
	top_aa = top && top_r;
	if (!top_aa) {
		top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
		top_r = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	}
	if (!bottom)
		bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	if (pica_reserve)
		vramFree(pica_reserve);
	stereo_native = native;
	plain_now = false;
	plain_ok = top_aa && !g_new3ds && !sw_aa;
	log_printf("3D: %s top screen targets, VRAM %uK free\n", top_aa ? "supersampled" : "native",
		(unsigned)(vramSpaceFree() / 1024));
	return top && top_r && bottom;
}

bool video_stereo_on(void) { return stereo_on; }
bool video_top_aa(void) { return top_aa; }

// begin drawing on a top target in the usual 400x240 coordinates
static void top_begin(C3D_RenderTarget *t)
{
	C2D_SceneBegin(t);
	if (top_aa && !plain_now)
		C2D_SceneSize(240, 400, true);
}

bool video_init(void)
{
	gfxInitDefault();
	gfxSet3D(false);  // switched on per frame when the game is shown in 3D
	if (!C3D_Init(VIDEO_CMDBUF_BYTES))   // reserve room for GPU command lists
		return false;
	if (c3d_abi_failed)   // renderqueue.c: the linked citro3d is not the pinned one
		return false;
	// the library's live floor mesh alone is a few thousand primitives
	if (!C2D_Init(16384))
		return false;
	C2D_Prepare();
	// Keep a contiguous VRAM hole for the lazy PICA target. Allocating the
	// supersampled screens first fragments the two VRAM banks too far.
	void *pica_reserve = vramAlloc(1024 * 512 * 2);
	if (!pica_reserve)
		return false;

	// Top screen: render at 2x2 the resolution and let the display transfer
	// average it down (4x supersampling) so lines and edges are smooth.
	top = sw_noaa ? NULL : aa_target(GFX_LEFT);
	top_r = sw_noaa ? NULL : aa_target(GFX_RIGHT);
	if (!top || !top_r) {
		if (top) C3D_RenderTargetDelete(top);
		if (top_r) C3D_RenderTargetDelete(top_r);
		log_printf("supersampled top screen unavailable, using 1x\n");
		top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
		top_r = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
		top_aa = false;
	} else
		plain_ok = !g_new3ds && !sw_aa;
	bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	for (int i = 0; i < 2; i++) {
		fbs[i] = linearAlloc(TEX_W * TEX_H * 2);
		if (!fbs[i])
			return false;
		memset(fbs[i], 0, TEX_W * TEX_H * 2);
	}
	if (!C3D_TexInit(&tex, TEX_W, TEX_H, GPU_RGB565))
		return false;
	C3D_TexSetWrap(&tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	vramFree(pica_reserve);
	return true;
}

static bool hardware_active;
bool renderer_is_gpu(void) { return hardware_active; }

bool video_game_begin(void)
{
	have_frame = false;
	pending = NULL;
	fb_cur = 0;
	music_unload();
	video_gpu_idle();
	covers_release_gpu();
	bool initialized = pica_init(pica_shbin, pica_shbin_size);
	bool ok = initialized && pica_game_begin(!g_new3ds);
	if (!ok) {
		log_printf("gpu_pica: %s failed, VRAM %uK linear %uK free\n",
			initialized ? "game buffers" : "init", (unsigned)(vramSpaceFree() / 1024),
			(unsigned)(linearSpaceFree() / 1024));
		pica_exit();
		music_reload();
		return false;
	}
	extern int pica_debug;
	pica_debug = sw_gldbg;
	pica_double_buffer = sw_gpu_async;
	pica_wait_frame = video_gpu_wait_previous;
	pica_frame_pending = video_gpu_has_previous;
	pica_open_frame = video_gpu_frame_begin;
	hardware_active = true;
	extern int pica_vertices;
	log_printf("gpu_pica: game buffers ready, %uK GPU memory left, %d vertices\n",
		(unsigned)(linearSpaceFree() / 1024), pica_vertices);
	return true;
}

void video_game_end(void)
{
	// A pause-menu exit can abandon an open offscreen frame; a held frame
	// can leave PICA's vertex layout/program bound without any C2D pass.
	// Submit and retire all readers before releasing their game buffers.
	if (hardware_active && gpu_frame) {
		pica_prepare_present();
		video_hold();
	}
	video_gpu_idle();
	gpustereo_game_end();
	if (hardware_active) {
		// Bind the UI while PICA's shader is still alive: citro3d compares
		// the old program's DVLP when binding another shader. Library frames
		// no longer enter the renderer_is_gpu() restore path below.
		C2D_Prepare();
		C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
		C3D_AlphaTest(false, GPU_ALWAYS, 0);
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
			GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
		pica_exit();
		hardware_active = false;
		video_stereo_targets(false);
		music_reload();
	}
	have_frame = false;
	pending = NULL;
}

void video_exit(void)
{
	// Closed from the HOME Menu: the GPU stays with it, and a game's
	// renderer may still own GPU work that can never finish. Its teardown
	// would wait forever; the process ends anyway. gfxExit checks the GPU.
	if (!gspHasGpuRight()) {
		gfxExit();
		return;
	}
	C3D_TexDelete(&tex);
	for (int i = 0; i < 2; i++) {
		if (fbs[i])
			linearFree(fbs[i]);
		fbs[i] = NULL;
	}
	pica_exit();
	C2D_Fini();
	C3D_Fini();
	gfxExit();
}

C3D_RenderTarget *video_bottom_target(void) { return bottom; }

void *video_framebuffer(unsigned width, unsigned height, size_t *pitch)
{
	static unsigned last_w, last_h;
	if (width > TEX_W || height > TEX_H)
		return NULL;
	// the core only clears the buffer it's drawing to when the video mode
	// changes; clear both, or the other one keeps stale rows (e.g. a white
	// strip from a taller logo screen)
	if (width != last_w || height != last_h) {
		for (int i = 0; i < 2; i++)
			memset(fbs[i], 0, TEX_W * TEX_H * 2);
		last_w = width;
		last_h = height;
	}
	*pitch = TEX_W * 2;
	return fbs[fb_cur];
}

void video_frame(const void *data, unsigned width, unsigned height, size_t pitch)
{
	if (!data || width == 0 || height == 0)
		return; // duplicate frame: keep showing the current texture
	if (width > TEX_W)
		width = TEX_W;
	if (height > TEX_H)
		height = TEX_H;

	// the core normally draws straight into our buffer; copy only if it didn't
	u16 *fb = fbs[fb_cur];
	if (data != fbs[0] && data != fbs[1]) {
		const u8 *src = data;
		for (unsigned y = 0; y < height; y++)
			memcpy(fb + y * TEX_W, src + y * pitch, width * 2);
	} else {
		fb = (u16 *)data;
	}

	frame_w = width;
	frame_h = height;
	// vout_flip put the picture at gpulib's screen.y (cropped above the
	// field), every second line of a 480i picture into a halved frame
	int y = gpu.screen.y, h = gpu.screen.h;
	if (y < 0) { h += y; y = 0; }
	if (h >= (int)height * 3 / 2) h /= 2;
	frame_pic_y = y;
	frame_pic_h = h;
	have_frame = true;

	u64 t0 = svcGetSystemTick();
	GSPGPU_FlushDataCache(fb, TEX_W * height * 2);
	xfer_ticks += svcGetSystemTick() - t0;
	pending = fb;
	fb_cur = (fb == fbs[0]) ? 1 : 0;

}

// Copy the same texture/scanout draw_game presents, including the held
// frameskip picture. Never read VRAM directly from the CPU. A raw GPU copy
// retains its Morton layout; only the thumbnail's samples need untile/convert.
static bool snapshot_gpu(u16 *out, int w, int h, C3D_Tex *tex,
                         float left, float top, float width, float height)
{
	// A physical pause can arrive between retro_run and video_present.
	// Submit that frame before reading its texture or querying GPU completion.
	if (gpu_frame) video_present(true, NULL, NULL, NULL);
	video_gpu_idle();
	u16 *pixels = linearAlloc(tex->size);
	if (!pixels) return false;
	GSPGPU_FlushDataCache(pixels, tex->size);
	gpustereo_copy_bypass = true;   // not the renderer's held picture
	C3D_SyncTextureCopy(tex->data, 0, (u32 *)pixels, 0, tex->size, 8);
	gpustereo_copy_bypass = false;
	GSPGPU_InvalidateDataCache(pixels, tex->size);
	int x0 = (int)(left * tex->width + 0.5f);
	int y0 = (int)((1.0f - top) * tex->height + 0.5f);
	// Texel offsets of the two sample columns of each x and rows of each y,
	// worked out once (per sample this was float math and a division).
	unsigned *col = malloc((w + h) * 2 * sizeof(unsigned)), *row = col + w * 2;
	if (!col) {
		linearFree(pixels);
		return false;
	}
	for (int x = 0; x < w; x++)
		for (int i = 0; i < 2; i++) {
			unsigned sx = (x0 + (int)((x + 0.25f + 0.5f * i) * width / w)) % tex->width;
			col[x * 2 + i] = ((sx >> 3) << 6) | (sx & 1) | ((sx & 2) << 1) | ((sx & 4) << 2);
		}
	for (int y = 0; y < h; y++)
		for (int j = 0; j < 2; j++) {
			unsigned sy = (y0 + (int)((y + 0.25f + 0.5f * j) * height / h)) % tex->height;
			row[y * 2 + j] = (((sy >> 3) * (tex->width >> 3)) << 6) | ((sy & 1) << 1) | ((sy & 2) << 2) | ((sy & 4) << 3);
		}
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			unsigned r = 0, g = 0, b = 0;
			for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
				u16 p = pixels[row[y * 2 + j] + col[x * 2 + i]]; // GPU_RGBA5551; alpha is the PS1 mask bit
				r += p >> 11; g += (p >> 6) & 31; b += (p >> 1) & 31;
			}
			unsigned green = g / 4;
			out[y * w + x] = ((r / 4) << 11) | ((green * 2 + (green >> 4)) << 5) | (b / 4);
		}
	}
	free(col);
	linearFree(pixels);
	return true;
}

// Shrink the last finished game frame to w x h (RGB565) for a save state
// thumbnail. Each output pixel averages 2x2 samples of its source area.
bool video_snapshot(u16 *out, int w, int h)
{
	pica_thread_sync();
	if (!out || w <= 0 || h <= 0) return false;
	if (renderer_is_gpu()) {
		C3D_Tex *picture;
		float l, t, r, b, pw, ph;
		int pic = pica_picture(&picture, &l, &t, &r, &b, &pw, &ph);
		if (pic == PICA_PIC_GPU)
			return snapshot_gpu(out, w, h, picture, l, t, pw, ph);
		if (pic == PICA_PIC_NONE) return false;
		// RGB24 movies use the software framebuffer even in GPU mode.
	}
	if (!have_frame)
		return false;
	const u16 *src = fbs[fb_cur ^ 1];   // fb_cur is the one being drawn next
	for (int y = 0; y < h; y++) {
		int sy0 = (int)((y + 0.25f) * frame_h / h), sy1 = (int)((y + 0.75f) * frame_h / h);
		for (int x = 0; x < w; x++) {
			int sx0 = (int)((x + 0.25f) * frame_w / w), sx1 = (int)((x + 0.75f) * frame_w / w);
			u16 p[4] = { src[sy0 * TEX_W + sx0], src[sy0 * TEX_W + sx1],
			             src[sy1 * TEX_W + sx0], src[sy1 * TEX_W + sx1] };
			u32 r = 0, g = 0, b = 0;
			for (int i = 0; i < 4; i++) {
				r += p[i] >> 11;
				g += (p[i] >> 5) & 63;
				b += p[i] & 31;
			}
			out[y * w + x] = (u16)(((r / 4) << 11) | ((g / 4) << 5) | (b / 4));
		}
	}
	return true;
}

// test (stats): brightness of the top framebuffer (8x8 samples, 0-255)
int video_probe_top(void)
{
	u16 w, h;
	u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &w, &h);   // w = 240 rows of the rotated screen
	if (!fb)
		return -1;
	GSPGPU_InvalidateDataCache(fb, w * h * 3);
	int sum = 0;
	for (int j = 0; j < 8; j++)
		for (int i = 0; i < 8; i++) {
			const u8 *p = fb + ((h * (2 * j + 1) / 16) * w + w * (2 * i + 1) / 16) * 3;
			sum += (p[0] + p[1] + p[2]) / 3;
		}
	return sum / 64;
}

u64 video_take_xfer_ticks(void)
{
	u64 t = xfer_ticks;
	xfer_ticks = 0;
	return t;
}


// The screen mode's box: 4:3 like a TV, full-screen Stretch/GTE fallback, or
// the native bundle aspect fitted into the 400x240 display.
static void screen_box(float *w, float *h)
{
	*w = config_screen_mode() == 2 ? fminf(400, 240 * ws_aspect()) : config_screen_mode() ? 400 : 320;
	*h = config_screen_mode() == 2 ? *w / ws_aspect() : 240;
}

// Draw a picture whose top left is texel (px, py) of t, placed and cropped by d.
static void draw_picture(C3D_Tex *t, float px, float py, const display_rect *d, float eye_dx)
{
	// Without Smooth, sample a little past the texel centre: a 2:1 picture
	// (480i, 640 wide) samples exactly between two texels, where rounding
	// would pick either one from pixel to pixel. This keeps one field.
	float bias = g_cfg.smooth ? 0.0f : 0.125f;
	float x0 = px + d->sx + bias, y0 = py + d->sy + bias;
	Tex3DS_SubTexture st = { (u16)d->sw, (u16)d->sh,
		x0 / t->width, 1.0f - y0 / t->height,
		(x0 + d->sw) / t->width, 1.0f - (y0 + d->sh) / t->height };
	C2D_Image img = { t, &st };
	C3D_TexSetFilter(t, g_cfg.smooth ? GPU_LINEAR : GPU_NEAREST, g_cfg.smooth ? GPU_LINEAR : GPU_NEAREST);
	C2D_DrawImageAt(img, d->dx + eye_dx, d->dy, 0.5f, NULL, d->dw / d->sw, d->dh / d->sh);
}

static void draw_game_tex(C3D_Tex *t, float eye_dx)
{
	if (!have_frame)
		return;
	// The core's frame is the whole field with the picture already placed
	// in it horizontally; place its picture rows like the GPU path does.
	display_geom g = { frame_w, frame_h, 0, frame_pic_y, frame_w, frame_pic_h };
	display_rect d;
	float bw, bh;
	screen_box(&bw, &bh);
	if (display_fit(&g, bw, bh, &d))
		draw_picture(t, 0, 0, &d, eye_dx);
}

static void draw_game(float eye_dx, bool right)
{
	// hardware renderer: the picture is already a GPU texture
	C3D_Tex *t;
	float l, tp, r, b, w, h;
	int pic = pica_picture(&t, &l, &tp, &r, &b, &w, &h);
	if (pic == PICA_PIC_CPU)
		draw_game_tex(&tex, eye_dx);   // 24-bit (movies): the core's own picture
	if (pic != PICA_PIC_GPU)
		return;   // (PICA_PIC_NONE: the display is off, the screen stays black)
	if (right && gpustereo_mode() == 2) {   // real 3D: the right eye's own picture
		C3D_Tex *eye = gpustereo_right_for(t);
		if (eye)
			t = eye;
	}
	extern int pica_debug;
	display_geom g = { gpu.screen.hres, gpu.screen.vres, gpu.screen.x, gpu.screen.y, (int)w, (int)h };
	if (pica_debug & 32)   // debug: all of VRAM
		g = (display_geom){ (int)w, (int)h, 0, 0, (int)w, (int)h };
	display_rect d;
	float bw, bh;
	screen_box(&bw, &bh);
	if (!display_fit(&g, bw, bh, &d))
		return;
	// the picture's alpha is the PS1 mask bit (0 for most pixels): copy it
	// unblended and untested. The renderer leaves its own blending and alpha
	// test behind, which blacked the picture out whenever the game's last
	// draw of a frame used them.
	C2D_Flush();
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	draw_picture(t, l * t->width, (1.0f - tp) * t->height, &d, eye_dx);
	C2D_Flush();
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
		GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);   // citro2d's own, for the overlays
}

// One GPU frame per shown frame. The hardware renderer draws into it
// while the core runs, opening it lazily for required draws/uploads.
// video_present uses that same frame; idle retained pictures open none.

void video_gpu_frame_begin(void)
{
	if (gpu_frame)
		return;
	video_cache_targeted(renderer_is_gpu() && sw_gpu_flush && !in_menu_call);
	u64 t0 = svcGetSystemTick();
	C3D_FrameBegin(renderer_is_gpu() && sw_gpu_async && !in_menu_call ? VIDEO_FRAME_OVERLAP : 0);
	wait_ticks += svcGetSystemTick() - t0;
	if (!sw_gpu_async) gpu_ms += C3D_GetDrawingTime();   // the frame we just waited for
	gap_ticks += t0 - frame_end_tick;
	gpu_frame = true;
	if (renderer_is_gpu()) {
		pica_async = sw_gpu_async && video_gpu_has_previous();
		pica_frame_started();
	}
}

// wait until the GPU has finished everything queued (before freeing memory it reads)
static void idle_on_gpu_thread(void *data) { (void)data; video_gpu_idle(); }
void video_gpu_idle(void)
{
	if (pica_thread_call && pica_thread_call(idle_on_gpu_thread, NULL)) return;
	if (gpu_frame)
		return;   // inside a frame: nothing has been sent yet
	C3D_FrameBegin(0);   // waits for the previous frame
	C3D_FrameEnd(0);
	C3D_FrameBegin(0);   // ...and for this empty one
	C3D_FrameEnd(0);
}

struct present_request {
	bool show_game, repaint_top;
	void (*draw_top)(void *), (*draw_bottom)(void *);
	void *ud;
};
static void present_on_gpu_thread(void *data)
{
	struct present_request *r = data;
	video_present_screens(r->show_game, r->repaint_top, r->draw_top, r->draw_bottom, r->ud);
}

void video_present_screens(bool show_game, bool repaint_top, void (*draw_top)(void *),
	void (*draw_bottom)(void *), void *ud)
{
	if (pica_thread_call) {
		struct present_request r = { show_game, repaint_top, draw_top, draw_bottom, ud };
		if (pica_thread_call(present_on_gpu_thread, &r)) return;
	}
	// no vsync wait here: emu.c paces frames; this only waits for the GPU
	video_gpu_frame_begin();
	// Citro2d shares its vertex/index buffers across frames.
	video_gpu_wait_previous();
	if (sw_gpu_async) gpu_ms += C3D_GetDrawingTime();
	if (renderer_is_gpu()) {
		pica_prepare_present(); // finish PS1 draws before binding frontend state
		C2D_Prepare();
		C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
		C3D_AlphaTest(false, GPU_ALWAYS, 0);
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
			GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
		pica_state_lost();
	}

	if (video_pre_draw && !show_game)
		video_pre_draw();

	if (repaint_top) {
		// queued on the GPU ahead of the draw below, so it never stalls the CPU
		// only the rows the frame uses (a multiple of 8, for the tiling): a
		// full 1024x512 copy every frame took GPU time and memory bandwidth
		u32 rows = (frame_h + 7) & ~7u;
		if (rows > TEX_H)
			rows = TEX_H;
		if (pending) {
			GX_DisplayTransfer((u32 *)pending, GX_BUFFER_DIM(TEX_W, rows),
				(u32 *)tex.data, GX_BUFFER_DIM(TEX_W, rows), TRANSFER_FLAGS);
			pending = NULL;
		}

		// Stereo 3D "screen depth": the game picture sits behind the screen,
		// PSX3D's overlays (FPS, messages) stay on the screen surface. Each eye
		// sees the picture shifted the opposite way.
		float slider = slider_get();
		bool ui3d = !show_game && ui_wants_3d && g_cfg.stereo && slider > 0.01f;
		bool stereo = (show_game && gpustereo_mode() && slider > 0.01f) || ui3d;
		// layers span depth -1.2..3: keep the whole range within a few pixels
		ui_parallax = ui3d ? slider * 1.5f : 0.0f;
		if (stereo != stereo_on) {
			gfxSet3D(stereo);
			stereo_on = stereo;
		}
		float depth = stereo ? slider * MAX_DEPTH_PX : 0.0f;
		// Real 3D: the eyes differ by themselves. Frames without polygon
		// depth (logos, movies, 2D games) and the time until the right eye
		// is ready get screen depth instead, faded in and out.
		if (show_game && gpustereo_mode() == 2)
			depth *= gpustereo_flat();
		// gameplay at 1x on Old 3DS, menus supersampled
		bool plain = renderer_is_gpu() && plain_ok && show_game && !in_menu_call;
		if (plain != plain_now) {
			top_set_plain(top, GFX_LEFT, plain);
			top_set_plain(top_r, GFX_RIGHT, plain);
			plain_now = plain;
		}
		// whole target pixels: a picture moved by part of a pixel is
		// resampled (blurred) differently in each eye
		if (depth != 0) {
			float px = top_aa && !plain_now ? 0.5f : 1.0f;
			depth = roundf(depth / px) * px;
		}
		C3D_RenderTarget *tl = top, *tr = top_r;
		C2D_TargetClear(tl, C2D_Color32(0, 0, 0, 255));
		top_begin(tl);
		if (show_game)
			draw_game(-depth, false);
		ui_eye = ui3d ? -1 : 0;
		if (draw_top)
			draw_top(ud);

		if (stereo) {
			C2D_TargetClear(tr, C2D_Color32(0, 0, 0, 255));
			top_begin(tr);
			if (show_game)
				draw_game(depth, true);
			ui_eye = ui3d ? 1 : 0;
			if (draw_top)
				draw_top(ud);
			ui_eye = 0;
		}
	}

	if (draw_bottom) {
		// in game the bottom screen is black; menus keep their dark blue
		C2D_TargetClear(bottom, show_game && !draw_top ? C2D_Color32(0, 0, 0, 255)
			: C2D_Color32(16, 16, 24, 255));
		C2D_SceneBegin(bottom);
		draw_bottom(ud);
	}

	C3D_FrameEnd(0);
	frame_end_tick = svcGetSystemTick();
	gpu_frame = false;
	if (renderer_is_gpu()) {
		pica_frame_ended();
		gpustereo_after_present();
	}
}

// Menus and legacy callers repaint both requested screens.
void video_present(bool show_game, void (*draw_top)(void *),
	void (*draw_bottom)(void *), void *ud)
{
	video_present_screens(show_game, true, draw_top, draw_bottom, ud);
}

// Called after the GPU thread is synchronized. RGB24 movie callbacks leave
// a pending upload; GPU 16-bit callbacks are NULL and use renderer serials.
bool video_display_changed(void)
{
	bool gpu_changed = renderer_is_gpu() && pica_display_changed();
	return pending || gpu_changed;
}

// Submit required offscreen work without composing or swapping LCD targets.
static void hold_on_gpu_thread(void *unused) { (void)unused; video_hold(); }
void video_hold(void)
{
	if (pica_thread_call && pica_thread_call(hold_on_gpu_thread, NULL)) return;
	if (!gpu_frame) return;
	C3D_FrameEnd(0);
	frame_end_tick = svcGetSystemTick();
	gpu_frame = false;
	pica_frame_ended();
}

// UI screens (browser, menus): one frame per refresh. In game, emu.c paces
// frames by the clock instead.
void video_present_vsync(bool show_game, void (*draw_top)(void *),
	void (*draw_bottom)(void *), void *ud)
{
	in_menu_call = true;
	video_present(show_game, draw_top, draw_bottom, ud);
	in_menu_call = false;
	gspWaitForVBlank();
}
