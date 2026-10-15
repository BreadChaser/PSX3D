// Video: the core renders RGB565 straight into a linear-memory buffer we hand
// it (GET_CURRENT_SOFTWARE_FRAMEBUFFER). The GPU's DMA engine converts that
// buffer into a tiled texture, and citro2d draws it scaled on the top screen.
// Two buffers alternate so the DMA of one frame runs on the GPU, queued ahead
// of the draw, while the core is already rendering the next one into the
// other buffer; the CPU never waits for the copy.
#include <string.h>
#include "psx3d.h"

#define TEX_W 1024
#define TEX_H 512

#define TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) | \
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) | \
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

static C3D_RenderTarget *top, *bottom;
static C3D_Tex tex;
static Tex3DS_SubTexture subtex;
static u16 *fbs[2];          // linear memory, TEX_W pixels per row
static int fb_cur;           // buffer the core renders into next
static u16 *pending;         // finished frame waiting to be copied to the texture
static unsigned frame_w = 320, frame_h = 240;
static bool have_frame;
static u64 wait_ticks, xfer_ticks;

u64 video_take_wait_ticks(void)
{
	u64 t = wait_ticks;
	wait_ticks = 0;
	return t;
}

bool video_init(void)
{
	gfxInitDefault();
	gfxSet3D(false);
	if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
		return false;
	if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS))
		return false;
	C2D_Prepare();

	top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
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
	return true;
}

void video_exit(void)
{
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
	if (width > TEX_W || height > TEX_H)
		return NULL;
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


static void draw_game(void)
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

	switch (config_widescreen() ? 1 : g_cfg.scale_mode) {
	case 1: // stretch to fill (also widescreen: the game already drew a 5:3 view)
		dw = 400; dh = 240;
		break;
	case 2: // fit height, keep the game's pixel aspect
		dh = 240;
		dw = frame_w * 240.0f / frame_h;
		if (frame_h <= 256 && frame_w > 400)
			dw = frame_w / 2.0f; // hi-res 512/640 wide modes
		if (dw > 400)
			dw = 400;
		break;
	default: // 4:3, like a TV
		dw = 320; dh = 240;
		break;
	}
	x = (400 - dw) / 2;
	y = (240 - dh) / 2;

	C3D_TexSetFilter(&tex, g_cfg.smooth ? GPU_LINEAR : GPU_NEAREST,
		g_cfg.smooth ? GPU_LINEAR : GPU_NEAREST);
	C2D_DrawImageAt(img, x, y, 0.5f, NULL, dw / frame_w, dh / frame_h);
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

	C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
	C2D_SceneBegin(top);
	if (show_game)
		draw_game();
	if (draw_top)
		draw_top(ud);

	if (draw_bottom) {
		C2D_TargetClear(bottom, C2D_Color32(16, 16, 24, 255));
		C2D_SceneBegin(bottom);
		draw_bottom(ud);
	}

	C3D_FrameEnd(0);
}
