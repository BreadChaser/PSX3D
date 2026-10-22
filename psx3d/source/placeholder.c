// A game without box art (yet): a black disc with the game's title on it.
// The disc is the downloaded photo (discart.h) once it is there; before that,
// or without Wi-Fi, one drawn here: rim, data area with a faint rainbow
// sheen, mirror band, clear hub and hole. No logos in the drawn one.
// The title is word-wrapped (three lines at most) into the band above the hole.
#include <math.h>
#include <string.h>
#include "psx3d.h"
#include "discart.h"

#define LINES 3
#define REF 256.0f    // layouts are made at this size and scaled

// The title's lines for a label of the given shape, worked out once per title
// (the carousel redraws them every frame at changing sizes).
typedef struct {
	const char *title;     // the string it was made for (library strings stay put)
	float band_w, band_h;  // the label it was made for, at REF
	int n;
	char line[LINES][96];
	float scale;           // font scale at REF
} layout_t;
static layout_t cache[16];
static int cache_next;
static C2D_TextBuf buf, measure;
static u32 buf_frame;

static float width_of(C2D_Font font, const char *s, float scale)
{
	C2D_Text t;
	C2D_TextBufClear(measure);
	if (!C2D_TextFontParse(&t, font, measure, s))
		return 1e9f;
	float w = 0, h = 0;
	C2D_TextGetDimensions(&t, scale, scale, &w, &h);
	return w;
}

// greedy word wrap; false: does not fit in LINES lines
static bool wrap(C2D_Font font, const char *title, float scale, float width, layout_t *l)
{
	l->n = 0;
	const char *p = title;
	while (*p == ' ') p++;
	while (*p) {
		if (l->n == LINES)
			return false;
		char *out = l->line[l->n];
		const char *best = NULL;
		size_t best_len = 0;
		// add words while the line fits
		for (const char *q = p;;) {
			const char *e = q;
			while (*e && *e != ' ') e++;
			size_t nl = e - p;
			if (nl >= sizeof(l->line[0]))
				break;
			memcpy(out, p, nl);
			out[nl] = 0;
			if (width_of(font, out, scale) > width)
				break;
			best = e;
			best_len = nl;
			if (!*e)
				break;
			q = e + 1;
		}
		if (!best)
			return false;   // one word wider than the label
		out[best_len] = 0;
		l->n++;
		p = best;
		while (*p == ' ') p++;
	}
	return l->n > 0;
}

static const layout_t *layout(C2D_Font font, const char *title, float band_w, float band_h)
{
	for (int i = 0; i < 16; i++)
		if (cache[i].title == title && cache[i].band_w == band_w && cache[i].band_h == band_h)
			return &cache[i];
	layout_t *l = &cache[cache_next];
	cache_next = (cache_next + 1) % 16;
	l->title = title;
	l->band_w = band_w;
	l->band_h = band_h;
	// as large as fits: the title font is about 30 px tall at scale 1
	float scale = band_h / 30.0f / 1.6f;
	if (scale > 0.95f) scale = 0.95f;
	for (; scale > 0.3f; scale *= 0.9f) {
		int lines_fit = (int)(band_h / (30.0f * scale));
		if (lines_fit >= 1 && wrap(font, title, scale, band_w, l) && l->n <= lines_fit)
			break;
	}
	if (scale <= 0.3f) {   // very long: one line, cut with an ellipsis
		scale = 0.3f;
		l->n = 1;
		snprintf(l->line[0], sizeof(l->line[0]), "%s", ui_fit(title, scale, band_w));
	}
	l->scale = scale;
	return l;
}

static void draw_title(const char *title, float cx, float top, float bottom, float width, float sc, float z, u8 alpha)
{
	C2D_Font font = libui_font();
	if (!buf) {
		buf = C2D_TextBufNew(1024);
		measure = C2D_TextBufNew(128);
		if (!buf || !measure)
			return;
	}
	if (buf_frame != covers_frame()) {
		buf_frame = covers_frame();
		C2D_TextBufClear(buf);
	}
	// keys in whole pixels at REF, so a size that changes every frame still hits
	const layout_t *l = layout(font, title, roundf(width / sc), roundf((bottom - top) / sc));
	float s = l->scale * sc, lh = 30.0f * s;
	float y = (top + bottom) / 2 - l->n * lh / 2;
	for (int i = 0; i < l->n; i++, y += lh) {
		C2D_Text t;
		if (!C2D_TextFontParse(&t, font, buf, l->line[i])) {
			C2D_TextBufClear(buf);   // full (no frame count moved): start over
			if (!C2D_TextFontParse(&t, font, buf, l->line[i]))
				return;
		}
		C2D_TextOptimize(&t);
		C2D_DrawText(&t, C2D_WithColor | C2D_AlignCenter, cx + 0.6f * s, y + 0.8f * s, z, s, s, C2D_Color32(0, 0, 0, alpha / 2));
		C2D_DrawText(&t, C2D_WithColor | C2D_AlignCenter, cx, y, z + 0.0005f, s, s, C2D_Color32(236, 236, 242, alpha));
	}
}

// a filled ring between radii r0 < r1, one colour at each edge
static void ring(float cx, float cy, float z, float r0, float r1, u32 c0, u32 c1, int seg)
{
	for (int i = 0; i < seg; i++) {
		float a0 = i * 2 * (float)M_PI / seg, a1 = (i + 1) * 2 * (float)M_PI / seg;
		float x00 = cx + cosf(a0) * r0, y00 = cy + sinf(a0) * r0, x01 = cx + cosf(a1) * r0, y01 = cy + sinf(a1) * r0;
		float x10 = cx + cosf(a0) * r1, y10 = cy + sinf(a0) * r1, x11 = cx + cosf(a1) * r1, y11 = cy + sinf(a1) * r1;
		C2D_DrawTriangle(x00, y00, c0, x10, y10, c1, x11, y11, c1, z);
		C2D_DrawTriangle(x00, y00, c0, x11, y11, c1, x01, y01, c0, z);
	}
}

static u32 rgba(int r, int g, int b, int a)
{
	return C2D_Color32((u8)r, (u8)g, (u8)b, (u8)(a < 0 ? 0 : a > 255 ? 255 : a));
}

// the disc drawn in code, radius r; dim darkens it like the covers
static void drawn_disc(float cx, float cy, float r, float z, u32 accent, float dim)
{
	int a = 255, seg = r > 40 ? 48 : 32;
	// shadow, rim (a little of the theme's colour), data area
	C2D_DrawCircleSolid(cx + r * 0.05f, cy + r * 0.07f, z - 0.002f, r, rgba(0, 0, 0, a * 45 / 100));
	C2D_DrawCircleSolid(cx, cy, z, r, rgba(((accent & 255) + 3 * 40) / 4, ((accent >> 8 & 255) + 3 * 40) / 4,
		((accent >> 16 & 255) + 3 * 48) / 4, a));
	ring(cx, cy, z + 0.0002f, r * 0.30f, r * 0.975f, rgba(26, 24, 34, a), rgba(10, 9, 14, a), seg);
	// the rainbow a disc throws back: two thin opposite fans, clear at the centre
	static const u8 hue[6][3] = { { 255, 60, 90 }, { 255, 170, 40 }, { 220, 255, 60 }, { 50, 230, 170 }, { 60, 140, 255 }, { 170, 80, 255 } };
	for (int side = 0; side < 2; side++)
		for (int k = 0; k < 6; k++) {
			float a0 = (side ? 3.55f : 0.41f) + k * 0.075f, a1 = a0 + 0.075f;
			u32 in = rgba(hue[k][0], hue[k][1], hue[k][2], 0);
			u32 out = rgba(hue[k][0], hue[k][1], hue[k][2], a * 34 / 255);
			C2D_DrawTriangle(cx + cosf(a0) * r * 0.32f, cy + sinf(a0) * r * 0.32f, in,
			                 cx + cosf(a0) * r * 0.96f, cy + sinf(a0) * r * 0.96f, out,
			                 cx + cosf(a1) * r * 0.96f, cy + sinf(a1) * r * 0.96f, out, z + 0.0004f);
			C2D_DrawTriangle(cx + cosf(a0) * r * 0.32f, cy + sinf(a0) * r * 0.32f, in,
			                 cx + cosf(a1) * r * 0.96f, cy + sinf(a1) * r * 0.96f, out,
			                 cx + cosf(a1) * r * 0.32f, cy + sinf(a1) * r * 0.32f, in, z + 0.0004f);
		}
	// mirror band, clear hub, hole
	ring(cx, cy, z + 0.0006f, r * 0.24f, r * 0.30f, rgba(70, 72, 84, a), rgba(30, 30, 38, a), seg);
	ring(cx, cy, z + 0.0006f, r * 0.12f, r * 0.24f, rgba(120, 124, 136, a * 70 / 100), rgba(60, 62, 72, a * 70 / 100), seg);
	C2D_DrawCircleSolid(cx, cy, z + 0.0008f, r * 0.12f, rgba(6, 6, 10, a));
	if (dim > 0)
		C2D_DrawCircleSolid(cx, cy, z + 0.001f, r, rgba(0, 0, 0, (int)(dim * 255)));
}

void cover_placeholder(float cx, float cy, float size, float dim, float z, const char *title, u32 accent)
{
	float fade = 1 - dim;
	disc_head_t h;
	C3D_Tex *t = covers_disc(&h);
	if (t) {
		// the photo: the disc's longer side is `size`
		float dw = h.right - h.left, dh = h.bottom - h.top;
		float sc = size / (dw > dh ? dw : dh);
		float x = cx - (h.left + dw / 2) * sc, y = cy - (h.top + dh / 2) * sc;
		Tex3DS_SubTexture st = { DISC_TEX, DISC_TEX, 0.0f, 1.0f, 1.0f, 0.0f };
		C2D_ImageTint tint;
		C2D_PlainImageTint(&tint, C2D_Color32(0, 0, 0, 255), dim);
		C2D_DrawImageAt((C2D_Image){ t, &st }, x, y, z, dim > 0 ? &tint : NULL, sc, sc);
		float w = dw * 0.66f * sc;
		draw_title(title, x + h.hub_x * sc, y + h.label_top * sc, y + h.label_bottom * sc, w, sc * (DISC_TEX / REF), z + 0.003f,
			(u8)(255 * fade));
		return;
	}
	float r = size * 0.48f;
	drawn_disc(cx, cy, r, z, accent, dim);
	draw_title(title, cx, cy - r * 0.86f, cy - r * 0.33f, r * 1.25f, size / REF, z + 0.003f, (u8)(255 * fade));
}
