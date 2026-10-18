// Video: the core renders RGB565 straight into a linear-memory buffer we hand
// it (GET_CURRENT_SOFTWARE_FRAMEBUFFER). The GPU's DMA engine converts that
// buffer into a tiled texture, and citro2d draws it scaled on the top screen.
// Two buffers alternate so the DMA of one frame runs on the GPU, queued ahead
// of the draw, while the core is already rendering the next one into the
// other buffer; the CPU never waits for the copy.
#include <string.h>
#include "psx3d.h"
#ifdef PSX3D_PICA
#include "../../core/plugins/gpu_pica/gpu_pica.h"
#include "pica_shbin.h"
#endif
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
// real 3D: full slider = this much disparity for the farthest things, in
// PS1 pixels (shown 1.25x wider on the 3DS)
#define MAX_REAL_PX 9.0f
static C3D_Tex tex;
// real 3D: the core draws each eye into its own VRAM; the left eye is the
// normal game picture, the right one is converted here from the second VRAM
static C3D_Tex tex_r;
static u16 *right_lin;               // linear staging, TEX_W pixels per row
static bool right_pending, right_ok;
static Tex3DS_SubTexture subtex;
static u16 *fbs[2];          // linear memory, TEX_W pixels per row
static int fb_cur;           // buffer the core renders into next
static u16 *pending;         // finished frame waiting to be copied to the texture
static unsigned frame_w = 320, frame_h = 240;
static bool have_frame;
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
#ifdef PSX3D_PICA
	if (!C3D_Init(0x100000))   // the hardware renderer issues many small draws
#else
	if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
#endif
		return false;
	// the library's live floor mesh alone is a few thousand primitives
	if (!C2D_Init(16384))
		return false;
	C2D_Prepare();
#ifdef PSX3D_PICA
	log_printf("gpu_pica: %s, %uK GPU memory left\n",
		pica_init(pica_shbin, pica_shbin_size) ? "ready" : "init failed", (unsigned)(linearSpaceFree() / 1024));
	{
		extern int pica_debug;
		pica_debug = sw_gldbg;
		pica_double_buffer = sw_gpu_async;
		pica_wait_frame = video_gpu_wait_previous;
		pica_frame_pending = video_gpu_has_previous;
		pica_open_frame = video_gpu_frame_begin;   // when the core starts drawing
	}
#endif

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
#ifndef PSX3D_PICA   // the hardware renderer has no use for the software stereo buffers
	right_lin = linearAlloc(TEX_W * TEX_H * 2);
	if (!right_lin || !C3D_TexInit(&tex_r, TEX_W, TEX_H, GPU_RGB565))
		log_printf("right eye texture failed: real 3D unavailable\n");
#endif
	C3D_TexSetWrap(&tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	C3D_TexSetWrap(&tex_r, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	return true;
}

void video_exit(void)
{
	if (tex_r.data)
		C3D_TexDelete(&tex_r);
	if (right_lin)
		linearFree(right_lin);
	right_lin = NULL;
	C3D_TexDelete(&tex);
	for (int i = 0; i < 2; i++) {
		if (fbs[i])
			linearFree(fbs[i]);
		fbs[i] = NULL;
	}
#ifdef PSX3D_PICA
	pica_exit();
#endif
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
	have_frame = true;

	u64 t0 = svcGetSystemTick();
	GSPGPU_FlushDataCache(fb, TEX_W * height * 2);
	xfer_ticks += svcGetSystemTick() - t0;
	pending = fb;
	fb_cur = (fb == fbs[0]) ? 1 : 0;

	// real 3D: the right eye's picture is in the core's second VRAM, PS1
	// BGR555; convert it the same way (when it's the same size as the left)
	right_ok = false;
	u64 rt0 = svcGetSystemTick();
	if (g_cfg.stereo == 2 && right_lin) {
		int rx, ry, rw, rh, rgb24;
		const u16 *v2 = psx3d_right_get(&rx, &ry, &rw, &rh, &rgb24);
		if (v2 && !rgb24 && (unsigned)rw == width && ((unsigned)rh == height || (unsigned)rh == height * 2)) {
			int step = (unsigned)rh == height * 2 ? 2 : 1;   // 480-line modes: every other line, like the left
			for (unsigned yy = 0; yy < height; yy++) {
				const u16 *src = v2 + ((ry + yy * step) & 511) * 1024;
				u16 *dst = right_lin + yy * TEX_W;
				if (!(rx & 1) && rx + width <= 1024) {
					// two pixels per word: BGR555 -> RGB565 on both halves at once
					const u32 *s2 = (const u32 *)(src + rx);
					u32 *d2 = (u32 *)dst;
					for (unsigned xx = 0; xx < width / 2; xx++) {
						u32 p = s2[xx];
						u32 r = p & 0x001f001f, g = (p >> 5) & 0x001f001f, b = (p >> 10) & 0x001f001f;
						d2[xx] = (r << 11) | (g << 6) | ((g & 0x00100010) << 1) | b;
					}
				} else {
					for (unsigned xx = 0; xx < width; xx++) {
						u16 p = src[(rx + xx) & 1023];
						u16 r = p & 31, g = (p >> 5) & 31, b = (p >> 10) & 31;
						dst[xx] = (u16)((r << 11) | (g << 6) | ((g >> 4) << 5) | b);
					}
				}
			}
			GSPGPU_FlushDataCache(right_lin, TEX_W * height * 2);
			right_pending = right_ok = true;
		}
	}
	if (g_cfg.stereo == 2) {
		static u64 acc; static int n;
		acc += svcGetSystemTick() - rt0;
		if (++n == 60) {
			float zc; int n3, n2, dmin, dmax, zmin, zmax;
			psx3d_stats(&zc, &n3, &n2, &dmin, &dmax, &zmin, &zmax);
			log_printf("  3d: convert %.2fms zc %.0f z %d..%d disp %d..%d polys3d %d 2d %d\n",
				acc / 60.0 / CPU_TICKS_PER_MSEC, zc, zmin, zmax, dmin, dmax, n3, n2);
			acc = 0; n = 0;
		}
	}
}

// Shrink the last finished game frame to w x h (RGB565) for a save state
// thumbnail. Each output pixel averages 2x2 samples of its source area.
bool video_snapshot(u16 *out, int w, int h)
{
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


static void draw_game_tex(C3D_Tex *t, float eye_dx)
{
	float dw, dh, x, y;
	C2D_Image img = { t, &subtex };

	if (!have_frame)
		return;

	subtex.width = frame_w;
	subtex.height = frame_h;
	subtex.left = 0.0f;
	subtex.right = (float)frame_w / TEX_W;
	subtex.top = 1.0f;
	subtex.bottom = 1.0f - (float)frame_h / TEX_H;

	// 4:3 like a TV, or the full 400x240 when the widescreen hack makes
	// 3D games draw a 5:3 view
	dw = config_screen_mode() ? 400 : 320;   // stretch and widescreen fill the screen
	dh = 240;
	x = (400 - dw) / 2 + eye_dx;
	y = (240 - dh) / 2;

	C3D_TexSetFilter(t, g_cfg.smooth ? GPU_LINEAR : GPU_NEAREST,
		g_cfg.smooth ? GPU_LINEAR : GPU_NEAREST);
	C2D_DrawImageAt(img, x, y, 0.5f, NULL, dw / frame_w, dh / frame_h);
}

static void draw_game(float eye_dx)
{
#ifdef PSX3D_PICA
	// hardware renderer: the picture is already a GPU texture
	C3D_Tex *t;
	float l, tp, r, b, w, h;
	int pic = pica_picture(&t, &l, &tp, &r, &b, &w, &h);
	if (pic == PICA_PIC_CPU)
		draw_game_tex(&tex, eye_dx);   // 24-bit (movies): the core's own picture
	if (pic != PICA_PIC_GPU)
		return;   // (PICA_PIC_NONE: the display is off, the screen stays black)
	Tex3DS_SubTexture st = { (u16)w, (u16)h, l, tp, r, b };
	C2D_Image img = { t, &st };
	float dw = config_screen_mode() ? 400 : 320, dh = 240;
	C3D_TexSetFilter(t, g_cfg.smooth ? GPU_LINEAR : GPU_NEAREST, g_cfg.smooth ? GPU_LINEAR : GPU_NEAREST);
	// the picture's alpha is the PS1 mask bit (0 for most pixels): copy it
	// unblended and untested. The renderer leaves its own blending and alpha
	// test behind, which blacked the picture out whenever the game's last
	// draw of a frame used them.
	C2D_Flush();
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
	C2D_DrawImageAt(img, (400 - dw) / 2 + eye_dx, (240 - dh) / 2, 0.5f, NULL, dw / w, dh / h);
	C2D_Flush();
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
		GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);   // citro2d's own, for the overlays
#else
	draw_game_tex(&tex, eye_dx);
#endif
}

// One GPU frame per shown frame. The hardware renderer draws into it
// while the core runs, so emu.c opens it before retro_run; video_present
// uses the same frame.
static bool gpu_frame;

void video_gpu_frame_begin(void)
{
	if (gpu_frame)
		return;
	video_cache_targeted(sw_gpu_flush && !in_menu_call);
	u64 t0 = svcGetSystemTick();
	C3D_FrameBegin(sw_gpu_async && !in_menu_call ? VIDEO_FRAME_OVERLAP : 0);
	wait_ticks += svcGetSystemTick() - t0;
	if (!sw_gpu_async) gpu_ms += C3D_GetDrawingTime();   // the frame we just waited for
	gap_ticks += t0 - frame_end_tick;
	gpu_frame = true;
#ifdef PSX3D_PICA
	pica_async = sw_gpu_async && video_gpu_has_previous();
	pica_frame_started();
#endif
}

// wait until the GPU has finished everything queued (before freeing memory it reads)
void video_gpu_idle(void)
{
	if (gpu_frame)
		return;   // inside a frame: nothing has been sent yet
	C3D_FrameBegin(0);   // waits for the previous frame
	C3D_FrameEnd(0);
	C3D_FrameBegin(0);   // ...and for this empty one
	C3D_FrameEnd(0);
}

void video_present(bool show_game, void (*draw_top)(void *),
	void (*draw_bottom)(void *), void *ud)
{
	// no vsync wait here: emu.c paces frames; this only waits for the GPU
	video_gpu_frame_begin();
	// Citro2d reuses its own vertex/index buffers: finish the previous frame
	// after emulation, before writing this frame's UI.
	video_gpu_wait_previous();
	if (sw_gpu_async) gpu_ms += C3D_GetDrawingTime();
#ifdef PSX3D_PICA
	pica_prepare_present(); // a final batch can rebind PS1 target/shader/vertices
	C2D_Prepare();   // the hardware renderer changed the GPU state
	// ...including some C2D_Prepare leaves alone: back to citro3d's defaults
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
		GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
#endif
#ifdef PSX3D_PICA
	pica_state_lost();
#endif

	if (video_pre_draw && !show_game)
		video_pre_draw();

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
	bool stereo = (show_game && g_cfg.stereo && slider > 0.01f) || ui3d;
	// layers span depth -1.2..3: keep the whole range within a few pixels
	ui_parallax = ui3d ? slider * 1.5f : 0.0f;
	if (stereo != stereo_on) {
		gfxSet3D(stereo);
		stereo_on = stereo;
	}
	float depth = stereo ? slider * MAX_DEPTH_PX : 0.0f;
	// real 3D: the core drew each eye separately; tell it how strong
	psx3d_set_strength(show_game && stereo && g_cfg.stereo == 2 ? slider * MAX_REAL_PX : 0.0f);
	if (right_pending) {
		GX_DisplayTransfer((u32 *)right_lin, GX_BUFFER_DIM(TEX_W, rows),
			(u32 *)tex_r.data, GX_BUFFER_DIM(TEX_W, rows), TRANSFER_FLAGS);
		right_pending = false;
	}
	bool real = show_game && stereo && g_cfg.stereo == 2 && have_frame && right_ok;

	// gameplay at 1x on Old 3DS, menus supersampled
	bool plain = plain_ok && show_game && !in_menu_call;
	if (plain != plain_now) {
		top_set_plain(top, GFX_LEFT, plain);
		top_set_plain(top_r, GFX_RIGHT, plain);
		plain_now = plain;
	}
	C3D_RenderTarget *tl = top, *tr = top_r;
	C2D_TargetClear(tl, C2D_Color32(0, 0, 0, 255));
	top_begin(tl);
	if (real && sw_sbs) {
		// test: both eyes side by side (left | right), each squeezed to half width
		C2D_Image l = { &tex, &subtex }, r = { &tex_r, &subtex };
		draw_game(0.0f);   // sets up subtex
		C2D_DrawRectSolid(0, 0, 0.55f, 400, 240, C2D_Color32(0, 0, 0, 255));
		C2D_DrawImageAt(l, 0, 0, 0.6f, NULL, 200.0f / frame_w, 240.0f / frame_h);
		C2D_DrawImageAt(r, 200, 0, 0.6f, NULL, 200.0f / frame_w, 240.0f / frame_h);
	} else if (real)
		draw_game_tex(sw_showright ? &tex_r : &tex, 0.0f);   // left eye: the core already moved everything
	else if (show_game)
		draw_game(-depth);
	ui_eye = ui3d ? -1 : 0;
	if (draw_top)
		draw_top(ud);

	if (stereo) {
		C2D_TargetClear(tr, C2D_Color32(0, 0, 0, 255));
		top_begin(tr);
		if (real)
			draw_game_tex(&tex_r, 0.0f);
		else if (show_game)
			draw_game(depth);
		ui_eye = ui3d ? 1 : 0;
		if (draw_top)
			draw_top(ud);
		ui_eye = 0;
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
#ifdef PSX3D_PICA
	pica_frame_ended();
#endif
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
