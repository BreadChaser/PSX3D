// Video: the core renders RGB565 straight into a linear-memory buffer we hand
// it (GET_CURRENT_SOFTWARE_FRAMEBUFFER). The GPU's DMA engine converts that
// buffer into a tiled texture, and citro2d draws it scaled on the top screen.
// Two buffers alternate so the DMA of one frame runs on the GPU, queued ahead
// of the draw, while the core is already rendering the next one into the
// other buffer; the CPU never waits for the copy.
#include <string.h>
#include "psx3d.h"
#include "../../core/libpcsxcore/psx3d_depth.h"

#define TEX_W 1024
#define TEX_H 512

#define TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) | \
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) | \
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

static C3D_RenderTarget *top, *top_r, *bottom;
static bool stereo_on;

// 3D slider fully up pushes the game picture this many pixels behind the screen
#define MAX_DEPTH_PX 8.0f
// real 3D: full slider gives this much separation between near and far
#define MAX_REAL_PX 14.0f
static C3D_Tex tex;
static C3D_Tex hud_tex;           // real 3D: the game's HUD layer, RGBA5551
static u16 *hud_lin;               // linear staging for hud_tex
static unsigned hud_w, hud_h;
#define HUD_W 1024
#define HUD_H 256
#define HUD_POP 0.4f               // HUD sits this fraction of max depth in front
static Tex3DS_SubTexture subtex;
static u16 *fbs[2];          // linear memory, TEX_W pixels per row
static int fb_cur;           // buffer the core renders into next
static u16 *pending;         // finished frame waiting to be copied to the texture
static unsigned frame_w = 320, frame_h = 240;
static bool have_frame;
static u64 wait_ticks, xfer_ticks;
u64 stereo_prep_ticks, stereo_draw_ticks;

u64 video_take_wait_ticks(void)
{
	u64 t = wait_ticks;
	wait_ticks = 0;
	return t;
}

bool video_init(void)
{
	gfxInitDefault();
	gfxSet3D(false);  // switched on per frame when the game is shown in 3D
	if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
		return false;
	if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS))
		return false;
	C2D_Prepare();

	top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	top_r = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	for (int i = 0; i < 2; i++) {
		fbs[i] = linearAlloc(TEX_W * TEX_H * 2);
		if (!fbs[i])
			return false;
		memset(fbs[i], 0, TEX_W * TEX_H * 2);
	}
	if (!C3D_TexInit(&tex, TEX_W, TEX_H, GPU_RGB565))
		return false;
	if (!stereo_init())
		log_printf("stereo_init failed: real 3D unavailable\n");
	hud_lin = linearAlloc(HUD_W * HUD_H * 2);
	if (!hud_lin || !C3D_TexInit(&hud_tex, HUD_W, HUD_H, GPU_RGBA5551))
		log_printf("HUD layer texture failed\n");
	C3D_TexSetFilter(&hud_tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	return true;
}

void video_exit(void)
{
	stereo_exit();
	C3D_TexDelete(&hud_tex);
	if (hud_lin)
		linearFree(hud_lin);
	hud_lin = NULL;
	C3D_TexDelete(&tex);
	for (int i = 0; i < 2; i++) {
		if (fbs[i])
			linearFree(fbs[i]);
		fbs[i] = NULL;
	}
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
}

u64 video_take_xfer_ticks(void)
{
	u64 t = xfer_ticks;
	xfer_ticks = 0;
	return t;
}


static void draw_game(float eye_dx)
{
	float dw, dh, x, y;
	C2D_Image img = { &tex, &subtex };

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
	dw = config_widescreen() ? 400 : 320;
	dh = 240;
	x = (400 - dw) / 2 + eye_dx;
	y = (240 - dh) / 2;

	C3D_TexSetFilter(&tex, g_cfg.smooth ? GPU_LINEAR : GPU_NEAREST,
		g_cfg.smooth ? GPU_LINEAR : GPU_NEAREST);
	C2D_DrawImageAt(img, x, y, 0.5f, NULL, dw / frame_w, dh / frame_h);
}

// Convert the core's HUD layer (PS1 BGR555, key colour = nothing drawn) to
// a transparent texture. 480-line modes keep every other line, like the
// game picture.
// 0: no HUD, 1: new HUD in staging (upload it), 2: keep the current texture
static int hud_update(void)
{
	static u8 prev_dirty[HUD_H];   // staging rows that hold HUD from last time
	int x, y, w, h;
	const u32 *rows;
	u32 seq = psx3d_ov_seq;
	if (seq & 1)
		return hud_w > 0 ? 2 : 0;   // GPU thread is swapping layers: keep the last HUD
	__sync_synchronize();
	const u16 *src = psx3d_overlay_get(&x, &y, &w, &h, &rows);
	if (!src || !hud_lin || w <= 0 || h <= 0)
		return 0;
	int step = h > 256 ? 2 : 1, oh = h / step;
	if (w > HUD_W) w = HUD_W;
	if (oh > HUD_H) oh = HUD_H;
	bool any = false;
	// only rows the HUD drew to (or drew to last time and must be emptied)
	for (int r = 0; r < oh; r++) {
		int vy = (y + r * step) & 511;
		bool dirty = rows[vy >> 5] & (1u << (vy & 31));
		u16 *out = hud_lin + r * HUD_W;
		if (!dirty) {
			if (prev_dirty[r]) {
				memset(out, 0, w * 2);
				prev_dirty[r] = 0;
			}
			continue;
		}
		const u16 *in = src + vy * 1024;
		for (int c = 0; c < w; c++) {
			u16 p = in[(x + c) & 1023];
			out[c] = p == PSX3D_OV_KEY ? 0 :
				((p & 31) << 11) | (((p >> 5) & 31) << 6) | (((p >> 10) & 31) << 1) | 1;
		}
		prev_dirty[r] = 1;
		any = true;
	}
	__sync_synchronize();
	if (psx3d_ov_seq != seq) {
		// swapped while we read: rows may be torn. Keep showing the last
		// uploaded HUD; force a full refresh of the staging rows next time.
		memset(prev_dirty, 1, sizeof(prev_dirty));
		return hud_w > 0 ? 2 : 0;
	}
	hud_w = w;
	hud_h = oh;
	if (!any)
		return 0;
	GSPGPU_FlushDataCache(hud_lin, HUD_W * oh * 2);
	return 1;
}

static void draw_hud(float x0, float dw, float dx)
{
	Tex3DS_SubTexture st = {
		(u16)hud_w, (u16)hud_h, 0.0f, 1.0f,
		(float)hud_w / HUD_W, 1.0f - (float)hud_h / HUD_H,
	};
	C2D_Image img = { &hud_tex, &st };
	C2D_DrawImageAt(img, x0 + dx, 0.0f, 0.6f, NULL, dw / hud_w, 240.0f / hud_h);
}

void video_present(bool show_game, void (*draw_top)(void *),
	void (*draw_bottom)(void *), void *ud)
{
	u64 t0 = svcGetSystemTick();
	// no vsync wait here: emu.c paces frames; this only waits for the GPU
	C3D_FrameBegin(0);
	wait_ticks += svcGetSystemTick() - t0;

	// queued on the GPU ahead of the draw below, so it never stalls the CPU
	if (pending) {
		GX_DisplayTransfer((u32 *)pending, GX_BUFFER_DIM(TEX_W, TEX_H),
			(u32 *)tex.data, GX_BUFFER_DIM(TEX_W, TEX_H), TRANSFER_FLAGS);
		pending = NULL;
	}

	// Stereo 3D "screen depth": the game picture sits behind the screen,
	// PSX3D's overlays (FPS, messages) stay on the screen surface. Each eye
	// sees the picture shifted the opposite way.
	float slider = osGet3DSliderState();
	bool stereo = show_game && g_cfg.stereo && slider > 0.01f;
	if (stereo != stereo_on) {
		gfxSet3D(stereo);
		stereo_on = stereo;
	}
	float depth = stereo ? slider * MAX_DEPTH_PX : 0.0f;
	// real 3D: warp the picture by the game's own depth, when it has any
	u64 tp = svcGetSystemTick();
	bool real = stereo && g_cfg.stereo == 2 && have_frame && stereo_prepare(slider * MAX_REAL_PX);
	int hud_state = real ? hud_update() : 0;
	bool hud = hud_state != 0;
	if (hud_state == 1)
		GX_DisplayTransfer((u32 *)hud_lin, GX_BUFFER_DIM(HUD_W, HUD_H), (u32 *)hud_tex.data,
			GX_BUFFER_DIM(HUD_W, HUD_H),
			GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
			GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB5A1) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB5A1) |
			GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
	float hud_dx = 0.5f * HUD_POP * slider * MAX_REAL_PX;   // in front: left eye shifts right
	stereo_prep_ticks += svcGetSystemTick() - tp;
	float gdw = config_widescreen() ? 400.0f : 320.0f, gx0 = (400.0f - gdw) / 2;
	if (real)
		C3D_TexSetFilter(&tex, g_cfg.smooth ? GPU_LINEAR : GPU_NEAREST,
			g_cfg.smooth ? GPU_LINEAR : GPU_NEAREST);

	C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
	C2D_SceneBegin(top);
	if (real) {
		draw_game(0.0f);   // underlay: fills the hairline gaps between shifted tiles
		u64 td = svcGetSystemTick();
		stereo_draw(&tex, gx0, gdw, frame_w, frame_h, -1, slider * MAX_REAL_PX);
		stereo_draw_ticks += svcGetSystemTick() - td;
		if (hud)
			draw_hud(gx0, gdw, hud_dx);
	}
	else if (show_game)
		draw_game(-depth);
	if (draw_top)
		draw_top(ud);

	if (stereo) {
		C2D_TargetClear(top_r, C2D_Color32(0, 0, 0, 255));
		C2D_SceneBegin(top_r);
		if (real) {
			draw_game(0.0f);
			stereo_draw(&tex, gx0, gdw, frame_w, frame_h, 1, slider * MAX_REAL_PX);
			if (hud)
				draw_hud(gx0, gdw, -hud_dx);
		}
		else
			draw_game(depth);
		if (draw_top)
			draw_top(ud);
	}

	if (draw_bottom) {
		// in game the bottom screen is black; menus keep their dark blue
		C2D_TargetClear(bottom, show_game && !draw_top ? C2D_Color32(0, 0, 0, 255)
			: C2D_Color32(16, 16, 24, 255));
		C2D_SceneBegin(bottom);
		draw_bottom(ud);
	}

	C3D_FrameEnd(0);
}

// UI screens (browser, menus): one frame per refresh. In game, emu.c paces
// frames by the clock instead.
void video_present_vsync(bool show_game, void (*draw_top)(void *),
	void (*draw_bottom)(void *), void *ud)
{
	video_present(show_game, draw_top, draw_bottom, ud);
	gspWaitForVBlank();
}
