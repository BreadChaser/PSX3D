// covers_pack: the packed texture must unpack (with an independent reader of
// the PICA200 layout) to the picture and the repeated edge.
// With a directory argument it also writes the levels as PNGs to look at.
#include <assert.h>
#include <math.h>
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../source/covers_pack.h"
#include "../../source/covers_png.h"

// texel (x, image row y) of level `lv`, as 8 bit RGB. The first memory row is
// the first image row (v = 1); tiles are 8x8, row by row; inside a tile texels
// run in Z order.
static void unpack(const uint8_t *pack, int lv, int x, int y, int rgb[3])
{
	int size = COVER_TEX >> lv;
	int mr = y;
	int tile = (mr / 8) * (size / 8) + x / 8;
	int z = 0;
	for (int b = 0; b < 3; b++) {
		z |= ((x >> b & 1) << (2 * b)) | ((mr >> b & 1) << (2 * b + 1));
	}
	const uint16_t *p = (const uint16_t *)pack;
	uint16_t v = p[tile * 64 + z];
	int r = v >> 11, g = v >> 5 & 63, b = v & 31;
	rgb[0] = (r * 255 + 15) / 31;
	rgb[1] = (g * 255 + 31) / 63;
	rgb[2] = (b * 255 + 15) / 31;
}

static double lin(int c) { double v = c / 255.0; return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
static int srgb(double l) { double v = l <= 0.0031308 ? l * 12.92 : 1.055 * pow(l, 1 / 2.4) - 0.055; return (int)(v * 255 + 0.5); }

static void check(const uint32_t *src, int w, int h, const char *dump, const char *name)
{
	uint8_t *pack = malloc(COVER_PACK_BYTES);
	assert(pack);
	covers_pack(src, w, h, pack);
	for (int lv = 0; lv < COVER_LEVELS; lv++) {
		int size = COVER_TEX >> lv, span = 1 << lv;
		int worst = 0;
		for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
			int got[3];
			unpack(pack, lv, x, y, got);
			// expected: average of the source over this texel's footprint, edge repeated
			double sum[3] = { 0 };
			for (int dy = 0; dy < span; dy++) for (int dx = 0; dx < span; dx++) {
				int sx = x * span + dx, sy = y * span + dy;
				if (sx >= w) sx = w - 1;
				if (sy >= h) sy = h - 1;
				uint32_t c = src[sy * w + sx];
				for (int k = 0; k < 3; k++) sum[k] += lin(c >> (24 - 8 * k) & 255);
			}
			for (int k = 0; k < 3; k++) {
				int want = srgb(sum[k] / (span * span));
				// 5/6 bit quantisation + dither + repeated rounding of the chain
				int d = abs(got[k] - want), tol = 11 + 2 * lv;
				if (d > worst) worst = d;
				if (d > tol) {
					fprintf(stderr, "%s level %d (%d,%d) channel %d: got %d want %d\n", name, lv, x, y, k, got[k], want);
					exit(1);
				}
			}
		}
		if (dump) {
			char path[512];
			snprintf(path, sizeof(path), "%s/%s-l%d.png", dump, name, lv);
			png_image img = { .version = PNG_IMAGE_VERSION, .width = size, .height = size, .format = PNG_FORMAT_RGB };
			uint8_t *rgb = malloc(size * size * 3);
			for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
				int px[3];
				unpack(pack, lv, x, y, px);
				for (int k = 0; k < 3; k++) rgb[(y * size + x) * 3 + k] = px[k];
			}
			assert(png_image_write_to_file(&img, path, 0, rgb, 0, NULL));
			free(rgb);
		}
	}
	free(pack);
	printf("PASS pack %s %dx%d\n", name, w, h);
}

int main(int argc, char **argv)
{
	const char *dump = argc > 1 && argv[1][0] ? argv[1] : NULL;
	covers_gamma_init();
	// a gradient with a sharp top left corner marker: catches flipped rows or columns
	for (int s = 0; s < 3; s++) {
		static const int dims[3][2] = { { 256, 256 }, { 200, 150 }, { 128, 128 } };
		int w = dims[s][0], h = dims[s][1];
		uint32_t *src = malloc(w * h * 4);
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
			int r = x * 255 / (w - 1), g = y * 255 / (h - 1), b = (x ^ y) & 64 ? 200 : 40;
			if (x < 16 && y < 16) { r = 255; g = 0; b = 0; }
			src[y * w + x] = (uint32_t)r << 24 | (uint32_t)g << 16 | (uint32_t)b << 8 | 255;
		}
		char name[32];
		snprintf(name, sizeof(name), "synthetic%d", s);
		check(src, w, h, dump, name);
		free(src);
	}
	// pictures given on the command line, shrunk the way the app does
	for (int i = 2; i < argc; i++) {
		int w, h;
		uint32_t *src = covers_png_decode(NULL, 0, argv[i], COVER_TEX, &w, &h);
		assert(src);
		const char *base = strrchr(argv[i], '/');
		char name[128];
		snprintf(name, sizeof(name), "%s", base ? base + 1 : argv[i]);
		name[strcspn(name, ".")] = 0;
		check(src, w, h, dump, name);
		free(src);
	}
	return 0;
}
