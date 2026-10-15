// Video: the core renders RGB565 straight into a linear-memory buffer we hand
// it (GET_CURRENT_SOFTWARE_FRAMEBUFFER). Each frame the GPU's DMA engine
// converts that buffer into a tiled texture, and citro2d draws it scaled on
// the top screen. No per-pixel work happens on the CPU.
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
static u16 *fb;              // linear memory, TEX_W pixels per row
static unsigned frame_w = 320, frame_h = 240;
static bool have_frame;

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

	fb = linearAlloc(TEX_W * TEX_H * 2);
	if (!fb || !C3D_TexInit(&tex, TEX_W, TEX_H, GPU_RGB565))
		return false;
	memset(fb, 0, TEX_W * TEX_H * 2);
	C3D_TexSetWrap(&tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	return true;
}

void video_exit(void)
{
	C3D_TexDelete(&tex);
	if (fb)
		linearFree(fb);
	fb = NULL;
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
	return fb;
}

void video_frame(const void *data, unsigned width, unsigned height, size_t pitch)
{
	if (!data || width == 0 || height == 0)
		return; // duplicate frame: keep showing the current texture
	if (width > TEX_W)
		width = TEX_W;
	if (height > TEX_H)
		height = TEX_H;

	// the core normally draws straight into fb; copy only if it didn't
	if (data != fb) {
		const u8 *src = data;
		for (unsigned y = 0; y < height; y++)
			memcpy(fb + y * TEX_W, src + y * pitch, width * 2);
	}

	frame_w = width;
	frame_h = height;
	have_frame = true;

	GSPGPU_FlushDataCache(fb, TEX_W * TEX_H * 2);
	C3D_SyncDisplayTransfer((u32 *)fb, GX_BUFFER_DIM(TEX_W, TEX_H),
		(u32 *)tex.data, GX_BUFFER_DIM(TEX_W, TEX_H), TRANSFER_FLAGS);
}

void video_set_fps(double fps)
{
	C3D_FrameRate(fps > 0 ? (float)fps : 60.0f);
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

	switch (g_cfg.scale_mode) {
	case 1: // stretch to fill
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
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

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
