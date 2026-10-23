// Menu icons (ICONS-DESIGN.md of the UI mock): Dreamstale line icons as 8-bit
// alpha masks, coloured by the theme through Citro2D's image tint (blend 1:
// colour from the tint, alpha from the mask), so a theme recolours them for free.
//
// A selected picture draws itself on along its strokes in 0.8 s, then its glow
// breathes (motion style 1, owner's pick). The draw-on is computed on the CPU
// for those 0.8 s only: mask x ramp(stroke order) into a small texture, one
// table lookup per texel. The glow is a pre-blurred mask added on top of the
// screen (additive blend), under the icon. Nothing here runs in a game.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psx3d.h"
#include "devtools.h"
#include "icons.h"

#define ICON_FILE "romfs:/icons/dreamstale.bin"
#define GLYPHS 21                         // UI_ICON_STAR .. UI_ICON_EXIT
#define PIC_BLOCK (12 + 2 * (128 * 128 + 64 * 64 + 32 * 32) + 64 * 64)
#define DRAW_ON 0.8f                      // seconds
#define BREATH 3.6f                       // seconds per glow breath
#define GLOW_SCALE 1.625f                 // the glow image's room around the icon (208/128)

// 8x8 tiles, Morton order inside each tile (one byte per texel)
static inline u32 texel(u32 x, u32 y, u32 w)
{
	return (((y >> 3) * (w >> 3) + (x >> 3)) << 6) |
		(x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

// an n x n image (rows top down) into a w-wide texture or array at (x0, y0)
static void put(u8 *dst, u32 w, u32 x0, u32 y0, const u8 *src, u32 n)
{
	for (u32 y = 0; y < n; y++)
		for (u32 x = 0; x < n; x++)
			dst[texel(x0 + x, y0 + y, w)] = src[y * n + x];
}

static Tex3DS_SubTexture cell(u32 x, u32 y, u32 n, u32 w, u32 h)
{
	return (Tex3DS_SubTexture){ n, n, (float)x / w, 1.0f - (float)y / h, (float)(x + n) / w, 1.0f - (float)(y + n) / h };
}

static bool new_tex(C3D_Tex *t, u32 w, u32 h)
{
	if (!C3D_TexInit(t, w, h, GPU_A8))
		return false;
	memset(t->data, 0, t->size);
	C3D_TexSetFilter(t, GPU_LINEAR, GPU_LINEAR);
	return true;
}

static void tex_done(C3D_Tex *t)
{
	svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)t->data, t->size);   // not the GSP's IPC
}

// ---------------------------------------------------------------- loading

// Pictures: 128 masks one texture each; one 512x256 atlas with the 64 masks
// and 64 glows (rows 0-2) and 32 masks (row 3); the draw-on's stroke order
// (and the 64/32 masks again) tiled like a texture of their own size.
static bool pics_ok, pics_failed;
static C3D_Tex big[ICON_PICTURES], small;
static Tex3DS_SubTexture sub64[ICON_PICTURES], sub32[ICON_PICTURES], subglow[ICON_PICTURES], sub128;
static u8 *cpu;                           // per picture: order128, mask64, order64, mask32, order32
#define CPU_BLOCK (128 * 128 + 2 * 64 * 64 + 2 * 32 * 32)
static bool glyphs_ok, glyphs_failed;
static C3D_Tex glyph_tex;
static Tex3DS_SubTexture sub16[GLYPHS];

// a place on screen whose selected picture moves (ICON_SLOT_*), with its own
// draw-on textures (the size it shows)
typedef struct { int id, size; u64 start, seen; C3D_Tex rev, tip; } slot_t;
static slot_t slots[ICON_SLOTS];

static FILE *open_icons(u16 *pics, u16 *glyphs)
{
	FILE *f = fopen(ICON_FILE, "rb");
	u8 h[12];
	if (f && fread(h, 1, 12, f) == 12 && !memcmp(h, "PSXI", 4) && h[4] == 1 && !h[5]) {
		*pics = h[6] | h[7] << 8;
		*glyphs = h[8] | h[9] << 8;
		return f;
	}
	if (f)
		fclose(f);
	log_printf("icons: %s missing or bad\n", ICON_FILE);
	return NULL;
}

static void free_pics(void)
{
	for (int i = 0; i < ICON_PICTURES; i++)
		if (big[i].data)
			C3D_TexDelete(&big[i]);
	if (small.data)
		C3D_TexDelete(&small);
	for (int i = 0; i < ICON_SLOTS; i++) {
		if (slots[i].rev.data) C3D_TexDelete(&slots[i].rev);
		if (slots[i].tip.data) C3D_TexDelete(&slots[i].tip);
	}
	memset(big, 0, sizeof(big));
	memset(&small, 0, sizeof(small));
	memset(slots, 0, sizeof(slots));
	free(cpu);
	cpu = NULL;
	pics_ok = false;
}

static bool load_pics(void)
{
	if (pics_ok || pics_failed)
		return pics_ok;
	pics_failed = true;   // until it has worked
	u16 npics, nglyphs;
	FILE *f = open_icons(&npics, &nglyphs);
	u8 *block = malloc(PIC_BLOCK);
	cpu = malloc(ICON_PICTURES * CPU_BLOCK);
	bool ok = f && block && cpu && npics == ICON_PICTURES && new_tex(&small, 512, 256);
	for (int i = 0; ok && i < ICON_PICTURES; i++) {
		ok = fread(block, 1, PIC_BLOCK, f) == PIC_BLOCK && new_tex(&big[i], 128, 128);
		if (!ok)
			break;
		const u8 *p = block + 12;
		u8 *c = cpu + i * CPU_BLOCK;
		put(big[i].data, 128, 0, 0, p, 128);                 // mask128
		put(c, 128, 0, 0, p + 128 * 128, 128);                // order128
		p += 2 * 128 * 128;
		u32 x = (i % 8) * 64, y = (i / 8) * 64;               // mask64: cells 0..10
		put(small.data, 512, x, y, p, 64);
		sub64[i] = cell(x, y, 64, 512, 256);
		put(c + 128 * 128, 64, 0, 0, p, 64);
		put(c + 128 * 128 + 64 * 64, 64, 0, 0, p + 64 * 64, 64);
		p += 2 * 64 * 64;
		x = (i % 16) * 32, y = 192 + (i / 16) * 32;          // mask32: row 3
		put(small.data, 512, x, y, p, 32);
		sub32[i] = cell(x, y, 32, 512, 256);
		put(c + 128 * 128 + 2 * 64 * 64, 32, 0, 0, p, 32);
		put(c + 128 * 128 + 2 * 64 * 64 + 32 * 32, 32, 0, 0, p + 32 * 32, 32);
		p += 2 * 32 * 32;
		x = ((i + 12) % 8) * 64, y = ((i + 12) / 8) * 64;    // glow64: cells 12..22
		put(small.data, 512, x, y, p, 64);
		subglow[i] = cell(x, y, 64, 512, 256);
		tex_done(&big[i]);
	}
	sub128 = cell(0, 0, 128, 128, 128);
	if (f)
		fclose(f);
	free(block);
	if (!ok) {
		free_pics();
		return false;
	}
	tex_done(&small);
	pics_ok = true;
	pics_failed = false;
	return true;
}

static bool load_glyphs(void)
{
	if (glyphs_ok || glyphs_failed)
		return glyphs_ok;
	glyphs_failed = true;
	u16 npics, nglyphs;
	FILE *f = open_icons(&npics, &nglyphs);
	u8 g[12 + 16 * 16];
	bool ok = f && nglyphs == GLYPHS && !fseek(f, 12 + (long)npics * PIC_BLOCK, SEEK_SET) && new_tex(&glyph_tex, 128, 64);
	for (int i = 0; ok && i < GLYPHS; i++) {
		ok = fread(g, 1, sizeof(g), f) == sizeof(g);
		u32 x = (i % 8) * 16, y = (i / 8) * 16;
		put(glyph_tex.data, 128, x, y, g + 12, 16);
		sub16[i] = cell(x, y, 16, 128, 64);
	}
	if (f)
		fclose(f);
	if (!ok) {
		if (glyph_tex.data)
			C3D_TexDelete(&glyph_tex);
		return false;
	}
	tex_done(&glyph_tex);
	glyphs_ok = true;
	glyphs_failed = false;
	return true;
}

void icons_release(void)
{
	video_gpu_idle();
	free_pics();
	if (glyph_tex.data) C3D_TexDelete(&glyph_tex);
	memset(&glyph_tex, 0, sizeof(glyph_tex));
	glyphs_ok = glyphs_failed = false;
	pics_failed = false;   // the next menu loads them again
}

// With the developer remote on, the log gets this file's CPU time per menu
// frame every 5 s (a frame = calls less than 8 ms apart), average and worst.
static u64 last_call, frame_sum, frame_max, window_start, window_sum;
static int window_frames;

static void spent(u64 t0)
{
	u64 t1 = svcGetSystemTick(), ms8 = SYSCLOCK_ARM11 / 125;
	if (t0 - last_call > ms8) {
		if (frame_sum > frame_max)
			frame_max = frame_sum;
		frame_sum = 0;
		window_frames++;
	}
	frame_sum += t1 - t0;
	window_sum += t1 - t0;
	last_call = t1;
	if (t1 - window_start < 5ull * SYSCLOCK_ARM11)
		return;
	if (dev_enabled() && window_frames > 1)
		log_printf("icons: %.3f ms per frame, worst %.3f ms (%d frames)\n",
			window_sum * 1000.0 / SYSCLOCK_ARM11 / window_frames, frame_max * 1000.0 / SYSCLOCK_ARM11, window_frames);
	window_start = t1;
	window_sum = frame_max = 0;
	window_frames = 0;
}

// ---------------------------------------------------------------- motion


static float seconds(u64 from, u64 to) { return (float)(u32)((to - from) >> 8) * (256.0f / SYSCLOCK_ARM11); }

// The draw-on of a picture at p (0..1, eased): texels whose stroke order the
// pen has passed are on (a soft 5 % edge), and a bright pen tip runs just
// behind the front (the mock's revealed()).
static void draw_on(int n, const u8 *mask, const u8 *order, float p, C3D_Tex *r, C3D_Tex *t)
{
	u8 on[256], pen[256];
	float front = p * 1.12f;
	for (int o = 0; o < 256; o++) {
		float of = o / 254.0f, v = (front - of) / 0.05f, d = (of - front) / 0.035f, x = d * d * 0.25f;
		v = v < 0 ? 0 : v > 1 ? 1 : v;
		float bump = x < 1 ? (1 - x) * (1 - x) * (1 - x) * (1 - x) : 0;   // about exp(-d^2)
		on[o] = (u8)(v * 255);
		pen[o] = (u8)(bump * v * 255);
	}
	u8 *rd = r->data, *td = t->data;
	for (int i = 0; i < n * n; i++) {
		u32 m = mask[i] + 1, o = order[i];
		rd[i] = (u8)((m * on[o]) >> 8);
		td[i] = (u8)((m * pen[o]) >> 8);
	}
	tex_done(r);
	tex_done(t);
}

static void tint4(C2D_ImageTint *t, u32 tl, u32 br, int a)
{
	u32 mid = TH_RGB(((tl & 255) + (br & 255)) / 2, ((tl >> 8 & 255) + (br >> 8 & 255)) / 2,
		((tl >> 16 & 255) + (br >> 16 & 255)) / 2);
	C2D_SetImageTint(t, C2D_TopLeft, theme_alpha(tl, a), 1);
	C2D_SetImageTint(t, C2D_TopRight, theme_alpha(mid, a), 1);
	C2D_SetImageTint(t, C2D_BotLeft, theme_alpha(mid, a), 1);
	C2D_SetImageTint(t, C2D_BotRight, theme_alpha(br, a), 1);
}

static void image(C3D_Tex *tex, const Tex3DS_SubTexture *st, float cx, float cy, float size, float z, const C2D_ImageTint *t)
{
	float s = size / st->width;
	C2D_DrawImageAt((C2D_Image){ tex, st }, cx - size / 2, cy - size / 2, z, t, s, s);
}

static void blend_add(bool add)
{
	C2D_Flush();
	if (add)
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
	else
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
			GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}

bool icons_draw(int slot, int id, float cx, float cy, float size, bool lit, float alpha, float z)
{
	if (id < 0 || id >= ICON_PICTURES)
		return false;
	u64 t0 = svcGetSystemTick();
	if (!load_pics()) {
		spent(t0);
		return false;
	}
	const theme_t *th = theme();
	int a = (int)(255 * alpha), k = size > 72 ? 0 : size > 36 ? 1 : 2, n = 128 >> k;
	C3D_Tex *tex = k ? &small : &big[id];
	const Tex3DS_SubTexture *st = k == 0 ? &sub128 : k == 1 ? &sub64[id] : &sub32[id];
	C2D_ImageTint tint;
	if (!lit) {
		C2D_PlainImageTint(&tint, theme_icon_dim(th, a * 9 / 10), 1);
		image(tex, st, cx, cy, size, z, &tint);
		spent(t0);
		return true;
	}
	// motion: a slot that starts showing this icon (or was not drawn for a
	// moment) draws it on again
	float p = 1, clock = seconds(0, t0);
	slot_t *s = slot >= 0 && slot < ICON_SLOTS ? &slots[slot] : NULL;
	if (s) {
		if (s->id != id || s->size != n || seconds(s->seen, t0) > 0.15f) {
			if (s->rev.data && s->rev.width != n) {
				C3D_TexDelete(&s->rev);
				C3D_TexDelete(&s->tip);
				memset(&s->rev, 0, sizeof(s->rev));
				memset(&s->tip, 0, sizeof(s->tip));
			}
			s->id = id, s->size = n, s->start = t0;
		}
		s->seen = t0;
		if (!s->rev.data && !(new_tex(&s->rev, n, n) && new_tex(&s->tip, n, n)))
			s = NULL;   // no memory for the draw-on: shown at once
		float q = seconds(s ? s->start : t0, t0) / DRAW_ON;
		p = !s || q >= 1 ? 1 : 1 - (1 - q) * (1 - q) * (1 - q);
	}
	float glow = ((th->icon_glow >> 24) / 255.0f) * (0.5f + 0.18f * sinf(clock * 2 * (float)M_PI / BREATH) + 0.5f * (1 - p)) * alpha;
	if (glow > 0.01f && size >= 14) {
		C2D_PlainImageTint(&tint, theme_alpha(th->icon_glow, (int)(255 * fminf(glow, 1))), 1);
		blend_add(true);
		image(&small, &subglow[id], cx, cy, size * GLOW_SCALE, z - 0.002f, &tint);
		blend_add(false);
	}
	if (p < 0.999f) {
		const u8 *c = cpu + id * CPU_BLOCK;
		const u8 *mask = k == 0 ? big[id].data : k == 1 ? c + 128 * 128 : c + 128 * 128 + 2 * 64 * 64;
		const u8 *order = k == 0 ? c : mask + n * n;
		draw_on(n, mask, order, p, &s->rev, &s->tip);
		static Tex3DS_SubTexture whole[3];
		whole[k] = cell(0, 0, n, n, n);
		tint4(&tint, th->icon_a, th->icon_b, a);
		image(&s->rev, &whole[k], cx, cy, size, z, &tint);
		C2D_PlainImageTint(&tint, TH_RGBA(255, 255, 255, a * 9 / 10), 1);
		image(&s->tip, &whole[k], cx, cy, size, z + 0.001f, &tint);
	} else {
		tint4(&tint, th->icon_a, th->icon_b, a);
		image(tex, st, cx, cy, size, z, &tint);
	}
	spent(t0);
	return true;
}

bool icons_glyph(int ui_icon, float cx, float cy, u32 ink, float z)
{
	if (config_in_game() || ui_icon < UI_ICON_STAR || ui_icon > UI_ICON_EXIT)
		return false;
	u64 t0 = svcGetSystemTick();
	bool ok = load_glyphs();
	if (ok) {
		C2D_ImageTint tint;
		C2D_PlainImageTint(&tint, ink, 1);
		// whole pixels: the glyphs are made for 1:1
		C2D_DrawImageAt((C2D_Image){ &glyph_tex, &sub16[ui_icon - UI_ICON_STAR] }, (float)(int)(cx - 7.5f), (float)(int)(cy - 7.5f), z, &tint, 1, 1);
	}
	spent(t0);
	return ok;
}
