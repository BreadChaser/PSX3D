// Host check of the disc stand-in (discart.c).
//   disc make <photo.jpg> <wordmark.png> <out.pam>   put the disc together, check it, write it untiled
//   disc decode <a.jpg> <a.ppm> ...                  the small JPEG decoder against a reference
//                                                    decode (PSNR at least 40 dB)
//   disc fail <a.jpg> ...                            files it must refuse (progressive...)
//   disc broken <a.jpg>                              cut and damaged copies: no crash (ASan/UBSan)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../source/discart.h"

uint8_t *discart_test_jpeg(const char *path, int *w, int *h);

static uint8_t *read_ppm(const char *path, int *w, int *h)
{
	FILE *f = fopen(path, "rb");
	int max;
	if (!f || fscanf(f, "P6 %d %d %d", w, h, &max) != 3 || max != 255 || fgetc(f) < 0) return NULL;
	uint8_t *px = malloc((size_t)*w * *h * 3);
	if (fread(px, 3, (size_t)*w * *h, f) != (size_t)*w * *h) return NULL;
	fclose(f);
	return px;
}

static int check_decoder(const char *jpg, const char *ppm)
{
	int w, h, w2, h2;
	uint8_t *a = discart_test_jpeg(jpg, &w, &h), *b = read_ppm(ppm, &w2, &h2);
	if (!a || !b || w != w2 || h != h2) { fprintf(stderr, "%s: decode failed or size differs\n", jpg); return 1; }
	double se = 0;
	for (size_t i = 0; i < (size_t)w * h * 3; i++) se += (double)(a[i] - b[i]) * (a[i] - b[i]);
	double psnr = se ? 10 * log10(255.0 * 255 * w * h * 3 / se) : 99;
	printf("%s: %dx%d, PSNR %.1f dB against the reference\n", jpg, w, h, psnr);
	free(a);
	free(b);
	return psnr < 40;
}

static int check_broken(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return 1;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *d = malloc(n);
	if (fread(d, 1, n, f) != (size_t)n) return 1;
	fclose(f);
	srand(1);
	for (int t = 0; t < 200; t++) {
		uint8_t *c = malloc(n);
		memcpy(c, d, n);
		long len = t < 40 ? n * t / 40 : n;
		for (int k = 0; t >= 40 && k < 1 + t % 9; k++) c[rand() % n] = rand();
		FILE *o = fopen("/tmp/discart-broken.jpg", "wb");
		fwrite(c, 1, len, o);
		fclose(o);
		int w, h;
		free(discart_test_jpeg("/tmp/discart-broken.jpg", &w, &h));
		free(c);
	}
	free(d);
	remove("/tmp/discart-broken.jpg");
	printf("%s: 200 cut/damaged copies, no crash\n", path);
	return 0;
}

static int check_make(const char *jpg, const char *wordmark, const char *out)
{
	static uint8_t tex[DISC_BYTES];
	disc_head_t h;
	if (!discart_make(jpg, wordmark[0] ? wordmark : NULL, &h, tex)) { fprintf(stderr, "discart_make failed\n"); return 1; }
	if (h.magic != DISC_MAGIC || h.left >= h.right || h.top >= h.bottom || h.hub_x < h.left || h.hub_x > h.right ||
	    h.hub_y < h.top || h.hub_y > h.bottom || h.label_top >= h.label_bottom) { fprintf(stderr, "bad head\n"); return 1; }
	printf("disc %d,%d-%d,%d hole %d,%d label %d-%d\n", h.left, h.top, h.right, h.bottom, h.hub_x, h.hub_y, h.label_top, h.label_bottom);
	FILE *f = fopen(out, "wb");
	fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", DISC_TEX, DISC_TEX);
	unsigned corner = 0, hole = 0, body = 0, ink = 0;
	for (int y = 0; y < DISC_TEX; y++)
		for (int x = 0; x < DISC_TEX; x++) {
			unsigned tx = x & 7, ty = y & 7;
			unsigned z = (tx & 1) | (ty & 1) << 1 | (tx & 2) << 1 | (ty & 2) << 2 | (tx & 4) << 2 | (ty & 4) << 3;
			uint32_t c = ((uint32_t *)tex)[((y >> 3) * (DISC_TEX / 8) + (x >> 3)) * 64 + z];
			unsigned char p[4] = { c >> 24, c >> 16, c >> 8, c };
			fwrite(p, 4, 1, f);
			if (x < 4 && y < 4) corner += p[3];
			if (abs(x - h.hub_x) <= 1 && abs(y - h.hub_y) <= 1) hole += p[3];
			if (p[3] == 255) body++;
			if (p[3] == 255 && p[0] > 150 && y > h.hub_y) ink++;   // the wordmark
		}
	fclose(f);
	printf("opaque %u texels, wordmark %u texels\n", body, ink);
	// the white around the disc and in its hole must be gone, the disc and the wordmark there
	if (corner || hole || body < DISC_TEX * DISC_TEX / 3 || (wordmark[0] && ink < 100)) {
		fprintf(stderr, "white left (corner %u, hole %u) or disc/wordmark missing\n", corner, hole);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 5 && !strcmp(argv[1], "make")) return check_make(argv[2], argv[3], argv[4]);
	if (argc >= 3 && !strcmp(argv[1], "broken")) return check_broken(argv[2]);
	if (argc >= 3 && !strcmp(argv[1], "fail")) {
		for (int i = 2; i < argc; i++) {
			int w, h;
			uint8_t *px = discart_test_jpeg(argv[i], &w, &h);
			if (px) { fprintf(stderr, "%s: decoded, should have been refused\n", argv[i]); return 1; }
			printf("%s: refused\n", argv[i]);
		}
		return 0;
	}
	if (argc >= 4 && !strcmp(argv[1], "decode")) {
		for (int i = 2; i + 1 < argc; i += 2)
			if (check_decoder(argv[i], argv[i + 1])) return 1;
		return 0;
	}
	return 2;
}
