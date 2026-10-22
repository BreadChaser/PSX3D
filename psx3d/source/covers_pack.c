// Box art -> GPU texture: RGB565 in the PICA200's tiled order. Done on a worker thread when a cover is first fetched; the cache on
// the SD card holds the result, so showing a cover costs the UI only copies.
#include "covers_pack.h"
#include <math.h>

uint16_t covers_lin[256];
uint8_t covers_srgb[4096];
static volatile bool gamma_ready;

void covers_gamma_init(void)
{
	if (gamma_ready) return;
	// Two workers may both get here: they write identical values.
	for (int i = 0; i < 256; i++) {
		float c = i / 255.0f;
		float l = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
		covers_lin[i] = (uint16_t)(l * 4095.0f + 0.5f);
	}
	for (int j = 0; j < 4096; j++) {
		float l = j / 4095.0f;
		float c = l <= 0.0031308f ? l * 12.92f : 1.055f * powf(l, 1.0f / 2.4f) - 0.055f;
		covers_srgb[j] = (uint8_t)(c * 255.0f + 0.5f);
	}
	__sync_synchronize();
	gamma_ready = true;
}

void covers_fit(int w, int h, int max, int *ow, int *oh)
{
	if (w <= max && h <= max) { *ow = w; *oh = h; return; }
	if (w >= h) {
		*ow = max;
		*oh = (int)(((long long)h * max + w / 2) / w);
	} else {
		*oh = max;
		*ow = (int)(((long long)w * max + h / 2) / h);
	}
	if (*ow < 1) *ow = 1;
	if (*oh < 1) *oh = 1;
}

// the picture, read with its edge pixels repeated beyond it
typedef struct { const uint32_t *p; int w, h; } img_t;

static inline uint32_t texel(const img_t *m, int x, int y)
{
	if (x >= m->w) x = m->w - 1;
	if (y >= m->h) y = m->h - 1;
	return m->p[y * m->w + x];
}

// 4x4 ordered dither thresholds, 0..15
static const uint8_t bayer[4][4] = {
	{ 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
};

static inline unsigned quant(unsigned v, unsigned top, unsigned dither)
{
	unsigned q = v * top * 16 / 255;   // 4 fractional bits
	q = (q + dither) >> 4;
	return q > top ? top : q;
}

// the picture into the tiled layout: memory row 0 is the picture's first
// row (texture coordinate v = 1, as citro2d's subtextures expect; checked in
// Azahar, a flipped layout shows the covers upside down)
static void emit(const img_t *m, int size, uint8_t *out)
{
	uint16_t *o = (uint16_t *)out;
	int tiles = size / 8;
	for (int mr = 0; mr < size; mr++) {
		int y = mr;
		for (int x = 0; x < size; x++) {
			uint32_t c = texel(m, x, y);
			unsigned d = bayer[y & 3][x & 3];
			unsigned r = quant(c >> 24, 31, d), g = quant(c >> 16 & 255, 63, d), b = quant(c >> 8 & 255, 31, d);
			unsigned tx = x & 7, ty = mr & 7;
			unsigned z = (tx & 1) | (ty & 1) << 1 | (tx & 2) << 1 | (ty & 2) << 2 | (tx & 4) << 2 | (ty & 4) << 3;
			o[((mr >> 3) * tiles + (x >> 3)) * 64 + z] = (uint16_t)(r << 11 | g << 5 | b);
		}
	}
}

void covers_pack(const uint32_t *rgba, int w, int h, uint8_t *out)
{
	covers_gamma_init();
	img_t pic = { rgba, w, h };
	emit(&pic, COVER_TEX, out);
}

// the texel at (x, y) of a packed COVER_TEX cover
static inline uint16_t packed_texel(const uint16_t *p, int x, int y)
{
	unsigned tx = x & 7, ty = y & 7;
	unsigned z = (tx & 1) | (ty & 1) << 1 | (tx & 2) << 1 | (ty & 2) << 2 | (tx & 4) << 2 | (ty & 4) << 3;
	return p[((y >> 3) * (COVER_TEX / 8) + (x >> 3)) * 64 + z];
}

void covers_thumb(const uint8_t *packed, int w, int h, uint8_t *out, int *tw, int *th)
{
	enum { F = COVER_TEX / COVER_THUMB };
	static const uint8_t five[32] = {   // 5 and 6 bit channels -> 8 bit
		0, 8, 16, 25, 33, 41, 49, 58, 66, 74, 82, 90, 99, 107, 115, 123,
		132, 140, 148, 156, 165, 173, 181, 189, 197, 206, 214, 222, 230, 239, 247, 255 };
	covers_gamma_init();
	const uint16_t *p = (const uint16_t *)packed;
	uint32_t px[COVER_THUMB * COVER_THUMB];
	for (int y = 0; y < COVER_THUMB; y++)
		for (int x = 0; x < COVER_THUMB; x++) {
			unsigned r = 0, g = 0, b = 0;
			for (int yy = 0; yy < F; yy++)
				for (int xx = 0; xx < F; xx++) {
					uint16_t c = packed_texel(p, x * F + xx, y * F + yy);
					unsigned g6 = c >> 5 & 63;
					r += covers_lin[five[c >> 11]];
					g += covers_lin[(g6 << 2) | (g6 >> 4)];
					b += covers_lin[five[c & 31]];
				}
			r /= F * F; g /= F * F; b /= F * F;
			px[y * COVER_THUMB + x] = (uint32_t)covers_srgb[r] << 24 | (uint32_t)covers_srgb[g] << 16 |
			                          (uint32_t)covers_srgb[b] << 8 | 255;
		}
	*tw = (w + F - 1) / F;
	*th = (h + F - 1) / F;
	img_t pic = { px, COVER_THUMB, COVER_THUMB };
	emit(&pic, COVER_THUMB, out);
}
