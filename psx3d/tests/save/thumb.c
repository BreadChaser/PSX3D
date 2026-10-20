// Host test: the table-driven thumbnail downsample in video.c snapshot_gpu()
// gives exactly the pixels of the original per-sample loop. Both loops are
// copied here (keep in sync with video.c).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned short u16;

static void original(u16 *out, int w, int h, const u16 *pixels, int tw, int th,
                     float left, float top, float width, float height)
{
	int x0 = (int)(left * tw + 0.5f);
	int y0 = (int)((1.0f - top) * th + 0.5f);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			unsigned r = 0, g = 0, b = 0;
			for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
				unsigned sx = (x0 + (int)((x + 0.25f + 0.5f * i) * width / w)) % tw;
				unsigned sy = (y0 + (int)((y + 0.25f + 0.5f * j) * height / h)) % th;
				unsigned tile = (((sy >> 3) * (tw >> 3) + (sx >> 3)) << 6) |
					(sx & 1) | ((sy & 1) << 1) | ((sx & 2) << 1) |
					((sy & 2) << 2) | ((sx & 4) << 2) | ((sy & 4) << 3);
				u16 p = pixels[tile];
				r += p >> 11; g += (p >> 6) & 31; b += (p >> 1) & 31;
			}
			unsigned green = g / 4;
			out[y * w + x] = ((r / 4) << 11) | ((green * 2 + (green >> 4)) << 5) | (b / 4);
		}
}

static void tables(u16 *out, int w, int h, const u16 *pixels, int tw, int th,
                   float left, float top, float width, float height)
{
	int x0 = (int)(left * tw + 0.5f);
	int y0 = (int)((1.0f - top) * th + 0.5f);
	unsigned *col = malloc((w + h) * 2 * sizeof(unsigned)), *row = col + w * 2;
	for (int x = 0; x < w; x++)
		for (int i = 0; i < 2; i++) {
			unsigned sx = (x0 + (int)((x + 0.25f + 0.5f * i) * width / w)) % tw;
			col[x * 2 + i] = ((sx >> 3) << 6) | (sx & 1) | ((sx & 2) << 1) | ((sx & 4) << 2);
		}
	for (int y = 0; y < h; y++)
		for (int j = 0; j < 2; j++) {
			unsigned sy = (y0 + (int)((y + 0.25f + 0.5f * j) * height / h)) % th;
			row[y * 2 + j] = (((sy >> 3) * (tw >> 3)) << 6) | ((sy & 1) << 1) | ((sy & 2) << 2) | ((sy & 4) << 3);
		}
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			unsigned r = 0, g = 0, b = 0;
			for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
				u16 p = pixels[row[y * 2 + j] + col[x * 2 + i]];
				r += p >> 11; g += (p >> 6) & 31; b += (p >> 1) & 31;
			}
			unsigned green = g / 4;
			out[y * w + x] = ((r / 4) << 11) | ((green * 2 + (green >> 4)) << 5) | (b / 4);
		}
	free(col);
}

int main(void)
{
	static const int sizes[][2] = {{1024, 512}, {512, 256}, {256, 256}, {2048, 1024}};
	static u16 a[256 * 192], b[256 * 192];
	int cases = 0, bad = 0;
	srand(1);
	for (unsigned k = 0; k < sizeof(sizes) / sizeof(*sizes); k++) {
		int tw = sizes[k][0], th = sizes[k][1];
		u16 *tex = malloc(tw * th * 2);
		for (int i = 0; i < tw * th; i++) tex[i] = rand();
		for (int t = 0; t < 200; t++) {
			// picture rectangles like pica_picture: anywhere, any size, wrapping
			float left = rand() / (float)RAND_MAX, top = rand() / (float)RAND_MAX;
			float width = 16 + rand() % (tw * 2), height = 16 + rand() % (th * 2);
			if (t < 4) { left = 0; top = 1; width = t ? 320 : 640; height = t ? 240 : 480; }
			original(a, 256, 192, tex, tw, th, left, top, width, height);
			tables(b, 256, 192, tex, tw, th, left, top, width, height);
			cases++;
			if (memcmp(a, b, sizeof(a))) bad++;
		}
		free(tex);
	}
	printf("thumbnail tables: %d cases, %d differ\n", cases, bad);
	return bad != 0;
}
