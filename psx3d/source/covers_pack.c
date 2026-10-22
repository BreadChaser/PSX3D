// Box art -> GPU texture: RGB565 with a mip chain, in the PICA200's tiled
// order. Done on a worker thread when a cover is first fetched; the cache on
// the SD card holds the result, so showing a cover costs the UI only copies.
#include "covers_pack.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

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

// 2x2 average in linear light
static void halve(const img_t *src, uint32_t *dst, int size)
{
	for (int y = 0; y < size; y++)
		for (int x = 0; x < size; x++) {
			uint32_t a = texel(src, 2 * x, 2 * y), b = texel(src, 2 * x + 1, 2 * y);
			uint32_t c = texel(src, 2 * x, 2 * y + 1), d = texel(src, 2 * x + 1, 2 * y + 1);
			uint32_t out = 0xff;
			for (int s = 24; s >= 8; s -= 8) {
				unsigned sum = covers_lin[a >> s & 255] + covers_lin[b >> s & 255] +
				               covers_lin[c >> s & 255] + covers_lin[d >> s & 255];
				out |= (uint32_t)covers_srgb[(sum + 2) >> 2] << s;
			}
			dst[y * size + x] = out;
		}
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

// one mip level into the tiled layout: memory row 0 is the picture's first
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

bool covers_pack(const uint32_t *rgba, int w, int h, uint8_t *out)
{
	covers_gamma_init();
	img_t level = { rgba, w, h };
	uint32_t *buf[2] = { NULL, NULL };   // the two newest levels we made
	bool ok = true;
	int size = COVER_TEX;
	for (int l = 0; l < COVER_LEVELS; l++, size >>= 1) {
		emit(&level, size, out + covers_level_offset(l));
		if (l == COVER_LEVELS - 1) break;
		uint32_t *next = malloc((size / 2) * (size / 2) * 4);
		if (!next) {
			ok = false;
			break;
		}
		halve(&level, next, size / 2);
		free(buf[0]);
		buf[0] = buf[1];
		buf[1] = next;
		level = (img_t){ next, size / 2, size / 2 };
	}
	free(buf[0]);
	free(buf[1]);
	return ok;
}
