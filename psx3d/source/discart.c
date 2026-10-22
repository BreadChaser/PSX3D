// The disc behind a game without box art (see discart.h). Runs once on a
// cover worker after the two downloads; the result is cached on the SD card.
// The photo is a black disc lying on white: the white around it and in its
// centre hole becomes transparent, the edge pixels lose the white they mixed
// with, and the disc is cropped, shrunk to fit the texture and tiled.
#include "discart.h"
#include "covers_pack.h"
#include "covers_png.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIT 248              // the disc's longer side in the texture
#define BRIGHT 196           // brighter than this and connected: the white around it

// ---------------------------------------------------------------- JPEG
// A small decoder for baseline JPEGs (8 bit, Huffman, 1-3 components,
// subsampled chroma, restart markers): enough for the one photo, a few KB of
// code instead of libjpeg's 250 KB. Anything else (progressive...) fails and
// the drawn disc stays.

typedef struct {
	uint8_t count[17];      // codes of each length
	uint8_t val[256];
	int mincode[17], maxcode[18], valptr[17];
} huff_t;

typedef struct {
	const uint8_t *p, *end;
	uint32_t bits;
	int nbits;
	bool marker;            // hit a marker: only zeros from here
} bits_t;

typedef struct {
	int id, h, v, tq, td, ta, pred;
	int bw, bh;             // its size in blocks (whole MCUs)
	uint8_t *plane;         // bw * 8 x bh * 8
} comp_t;

static const uint8_t zigzag[64] = {
	0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

static void huff_build(huff_t *t)
{
	int code = 0, k = 0;
	for (int l = 1; l <= 16; l++) {
		t->valptr[l] = k;
		t->mincode[l] = code;
		code += t->count[l];
		k += t->count[l];
		t->maxcode[l] = t->count[l] ? code - 1 : -1;
		code <<= 1;
	}
	t->maxcode[17] = 0x7fffffff;
}

static int get_bit(bits_t *b)
{
	if (!b->nbits) {
		int c = 0;
		if (!b->marker && b->p < b->end) {
			c = *b->p++;
			if (c == 0xff) {
				int n = b->p < b->end ? *b->p : 0;
				if (n == 0) b->p++;   // stuffed zero
				else { b->marker = true; c = 0; b->p--; }
			}
		}
		b->bits = c;
		b->nbits = 8;
	}
	b->nbits--;
	return b->bits >> b->nbits & 1;
}

static int get_bits(bits_t *b, int n)
{
	int v = 0;
	while (n--) v = v << 1 | get_bit(b);
	return v;
}

static int decode_huff(bits_t *b, const huff_t *t)
{
	int code = 0;
	for (int l = 1; l <= 16; l++) {
		code = code << 1 | get_bit(b);
		if (t->maxcode[l] >= 0 && code <= t->maxcode[l] && code >= t->mincode[l])
			return t->val[(t->valptr[l] + code - t->mincode[l]) & 255];
	}
	return -1;
}

static int extend(int v, int n)
{
	return n && v < 1 << (n - 1) ? v - (1 << n) + 1 : v;
}

// one 8x8 block: decode, dequantize, inverse DCT, into out (stride)
static bool decode_block(bits_t *b, comp_t *c, const huff_t *dc, const huff_t *ac, const uint16_t *q,
                         const float (*cosu)[8], uint8_t *out, int stride)
{
	float f[64] = { 0 };
	int s = decode_huff(b, dc);
	if (s < 0 || s > 11) return false;
	c->pred += extend(get_bits(b, s), s);
	f[0] = (float)(c->pred * q[0]);
	for (int k = 1; k < 64;) {
		int rs = decode_huff(b, ac);
		if (rs < 0) return false;
		int r = rs >> 4, n = rs & 15;
		if (!n) {
			if (r != 15) break;   // end of block
			k += 16;
			continue;
		}
		k += r;
		if (k > 63) return false;
		f[zigzag[k]] = (float)(extend(get_bits(b, n), n) * q[k]);
		k++;
	}
	float t[64];
	for (int y = 0; y < 8; y++)          // rows: frequency u -> x
		for (int x = 0; x < 8; x++) {
			float a = 0;
			for (int u = 0; u < 8; u++) a += cosu[x][u] * f[y * 8 + u];
			t[y * 8 + x] = a;
		}
	for (int x = 0; x < 8; x++)          // columns: frequency v -> y
		for (int y = 0; y < 8; y++) {
			float a = 128.0f;
			for (int v = 0; v < 8; v++) a += cosu[y][v] * t[v * 8 + x];
			int i = (int)(a + 0.5f);
			out[y * stride + x] = i < 0 ? 0 : i > 255 ? 255 : i;
		}
	return true;
}

static uint8_t *decode_jpeg(const uint8_t *d, size_t len, int *pw, int *ph)
{
	uint16_t qt[4][64] = { { 0 } };
	huff_t *ht = calloc(8, sizeof(huff_t));   // DC 0-3, AC 4-7
	comp_t comp[3];
	int nc = 0, w = 0, h = 0, hmax = 1, vmax = 1, restart = 0;
	bool frame = false, ok = false;
	uint8_t *rgb = NULL;
	memset(comp, 0, sizeof(comp));
	size_t i = 2;
	if (!ht || len < 4 || d[0] != 0xff || d[1] != 0xd8)
		goto done;
	while (i + 4 <= len) {
		if (d[i] != 0xff) goto done;
		int m = d[i + 1];
		if (m == 0xff) { i++; continue; }
		size_t seg = (size_t)d[i + 2] << 8 | d[i + 3];
		const uint8_t *p = d + i + 4, *pe = d + i + 2 + seg;
		if (seg < 2 || i + 2 + seg > len) goto done;
		if (m == 0xdb) {                                  // quantization tables
			while (p < pe) {
				int pq = *p >> 4, tq = *p & 3;
				p++;
				if (pq || p + 64 > pe) goto done;     // 16 bit tables: not baseline
				for (int k = 0; k < 64; k++) qt[tq][k] = *p++;
			}
		} else if (m == 0xc4) {                           // Huffman tables
			while (p < pe) {
				int tc = *p >> 4, th = *p & 3, n = 0;
				huff_t *t = &ht[(tc ? 4 : 0) + th];
				p++;
				if (tc > 1 || p + 16 > pe) goto done;
				for (int l = 1; l <= 16; l++) n += t->count[l] = *p++;
				if (n > 256 || p + n > pe) goto done;
				memcpy(t->val, p, n);
				p += n;
				huff_build(t);
			}
		} else if (m == 0xdd) {                           // restart interval
			restart = p[0] << 8 | p[1];
		} else if (m == 0xc0 || m == 0xc1) {              // baseline frame
			if (p[0] != 8) goto done;
			h = p[1] << 8 | p[2];
			w = p[3] << 8 | p[4];
			nc = p[5];
			if (!w || !h || w > 4096 || h > 4096 || (nc != 1 && nc != 3)) goto done;
			for (int c = 0; c < nc; c++) {
				comp[c].id = p[6 + c * 3];
				comp[c].h = p[7 + c * 3] >> 4;
				comp[c].v = p[7 + c * 3] & 15;
				comp[c].tq = p[8 + c * 3] & 3;
				if (comp[c].h < 1 || comp[c].h > 2 || comp[c].v < 1 || comp[c].v > 2) goto done;
				if (comp[c].h > hmax) hmax = comp[c].h;
				if (comp[c].v > vmax) vmax = comp[c].v;
			}
			frame = true;
		} else if (m >= 0xc2 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) {
			goto done;                                    // progressive, lossless, arithmetic
		} else if (m == 0xda) {                           // the picture
			if (!frame || p[0] != nc) goto done;
			for (int c = 0; c < nc; c++) {
				comp[c].td = p[2 + c * 2] >> 4 & 3;
				comp[c].ta = p[2 + c * 2] & 3;
			}
			int mw = (w + 8 * hmax - 1) / (8 * hmax), mh = (h + 8 * vmax - 1) / (8 * vmax);
			for (int c = 0; c < nc; c++) {
				comp[c].bw = mw * comp[c].h;
				comp[c].bh = mh * comp[c].v;
				if (!(comp[c].plane = malloc((size_t)comp[c].bw * 8 * comp[c].bh * 8))) goto done;
			}
			float cosu[8][8];
			for (int x = 0; x < 8; x++)
				for (int u = 0; u < 8; u++)
					cosu[x][u] = (u ? 0.5f : 0.35355339f) * cosf((2 * x + 1) * u * 3.14159265f / 16);
			bits_t b = { pe, d + len, 0, 0, false };
			int left = restart;
			for (int my = 0; my < mh; my++)
				for (int mx = 0; mx < mw; mx++) {
					if (restart && !left--) {     // RSTn: byte aligned, predictions start over
						if (b.p + 1 < b.end && b.p[0] == 0xff && (b.p[1] & 0xf8) == 0xd0) b.p += 2;
						b.nbits = 0;
						b.marker = false;
						for (int c = 0; c < nc; c++) comp[c].pred = 0;
						left = restart - 1;
					}
					for (int c = 0; c < nc; c++) {
						comp_t *k = &comp[c];
						int stride = k->bw * 8;
						for (int by = 0; by < k->v; by++)
							for (int bx = 0; bx < k->h; bx++) {
								uint8_t *o = k->plane + ((size_t)(my * k->v + by) * 8) * stride + (mx * k->h + bx) * 8;
								if (!decode_block(&b, k, &ht[k->td], &ht[4 + k->ta], qt[k->tq], (const float (*)[8])cosu, o, stride))
									goto done;
							}
					}
				}
			// to RGB (chroma repeated over its subsampled area)
			if (!(rgb = malloc((size_t)w * h * 3))) goto done;
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++) {
					int yy = comp[0].plane[(size_t)(y * comp[0].v / vmax) * comp[0].bw * 8 + x * comp[0].h / hmax];
					uint8_t *o = rgb + ((size_t)y * w + x) * 3;
					if (nc == 1) { o[0] = o[1] = o[2] = yy; continue; }
					int cb = comp[1].plane[(size_t)(y * comp[1].v / vmax) * comp[1].bw * 8 + x * comp[1].h / hmax] - 128;
					int cr = comp[2].plane[(size_t)(y * comp[2].v / vmax) * comp[2].bw * 8 + x * comp[2].h / hmax] - 128;
					int r = yy + ((91881 * cr) >> 16), g = yy - ((22554 * cb + 46802 * cr) >> 16), bl = yy + ((116130 * cb) >> 16);
					o[0] = r < 0 ? 0 : r > 255 ? 255 : r;
					o[1] = g < 0 ? 0 : g > 255 ? 255 : g;
					o[2] = bl < 0 ? 0 : bl > 255 ? 255 : bl;
				}
			ok = true;
			break;
		} else if (m == 0xd9)
			break;
		i += 2 + seg;
	}
done:
	for (int c = 0; c < 3; c++) free(comp[c].plane);
	free(ht);
	if (!ok) {
		free(rgb);
		return NULL;
	}
	*pw = w;
	*ph = h;
	return rgb;
}

// RGB, w x h, halved while its shorter side stays at least max. Caller frees.
static uint8_t *read_jpeg(const char *path, int max, int *pw, int *ph)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *data = len > 0 && len < 4 * 1024 * 1024 ? malloc(len) : NULL;
	bool ok = data && fread(data, 1, len, f) == (size_t)len;
	fclose(f);
	int w, h;
	uint8_t *px = ok ? decode_jpeg(data, len, &w, &h) : NULL;
	free(data);
	while (px && (w < h ? w : h) / 2 >= max) {   // halve: 2x2 averages
		int hw = w / 2, hh = h / 2;
		for (int y = 0; y < hh; y++)
			for (int x = 0; x < hw; x++)
				for (int c = 0; c < 3; c++) {
					const uint8_t *s = px + ((size_t)y * 2 * w + x * 2) * 3 + c;
					px[((size_t)y * hw + x) * 3 + c] = (s[0] + s[3] + s[w * 3] + s[w * 3 + 3] + 2) / 4;
				}
		w = hw;
		h = hh;
	}
	*pw = w;
	*ph = h;
	return px;
}

#ifdef DISCART_TEST
uint8_t *discart_test_jpeg(const char *path, int *w, int *h);
uint8_t *discart_test_jpeg(const char *path, int *w, int *h)
{
	return read_jpeg(path, 1 << 30, w, h);
}
#endif

static inline int luma(const uint8_t *p)
{
	return (p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8;
}

// Marks (1) the bright pixels that form the white around the disc and its
// hole: bright regions touching the picture's edge, and bright regions of at
// least min_area pixels (the hole). Small highlights on the disc stay.
// Returns the hole's centre (the biggest inner region) in *hx, *hy, or -1.
static bool find_white(const uint8_t *px, int w, int h, uint8_t *mark, int min_area, int *hx, int *hy)
{
	int *queue = malloc((size_t)w * h * sizeof(int));
	if (!queue)
		return false;
	memset(mark, 0, (size_t)w * h);   // 0 unseen, 1 white, 2 bright but kept, 3 dark/seen
	int best = 0;
	*hx = *hy = -1;
	for (int start = 0; start < w * h; start++) {
		if (mark[start])
			continue;
		if (luma(px + start * 3) <= BRIGHT) {
			mark[start] = 3;
			continue;
		}
		// one bright region
		int n = 0, head = 0;
		bool edge = false;
		long sx = 0, sy = 0;
		queue[n++] = start;
		mark[start] = 2;
		while (head < n) {
			int i = queue[head++], x = i % w, y = i / w;
			sx += x;
			sy += y;
			edge |= x == 0 || y == 0 || x == w - 1 || y == h - 1;
			const int nb[4] = { x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1, y > 0 ? i - w : -1, y < h - 1 ? i + w : -1 };
			for (int k = 0; k < 4; k++)
				if (nb[k] >= 0 && !mark[nb[k]] && luma(px + nb[k] * 3) > BRIGHT) {
					mark[nb[k]] = 2;
					queue[n++] = nb[k];
				}
		}
		bool white = edge || n >= min_area;
		if (!edge && n >= min_area && n > best) {
			best = n;
			*hx = (int)(sx / n);
			*hy = (int)(sy / n);
		}
		if (white)
			for (int k = 0; k < n; k++)
				mark[queue[k]] = 1;
	}
	free(queue);
	return true;
}

// how much of a pixel is disc: 0 in the white, a ramp at its border (the
// photo's soft edge), 255 elsewhere
static int coverage(const uint8_t *px, const uint8_t *mark, int w, int h, int i)
{
	if (mark[i] == 1)
		return 0;
	int x = i % w, y = i / w;
	bool border = (x > 0 && mark[i - 1] == 1) || (x < w - 1 && mark[i + 1] == 1) ||
	              (y > 0 && mark[i - w] == 1) || (y < h - 1 && mark[i + w] == 1);
	if (!border)
		return 255;
	int l = luma(px + i * 3), a = (BRIGHT + 30 - l) * 255 / 120;
	return a < 0 ? 0 : a > 255 ? 255 : a;
}

static inline void put(uint8_t *out, int x, int y, uint32_t rgba)
{
	unsigned tx = x & 7, ty = y & 7;
	unsigned z = (tx & 1) | (ty & 1) << 1 | (tx & 2) << 1 | (ty & 2) << 2 | (tx & 4) << 2 | (ty & 4) << 3;
	((uint32_t *)out)[((y >> 3) * (DISC_TEX / 8) + (x >> 3)) * 64 + z] = rgba;
}

bool discart_make(const char *jpg, const char *wordmark, disc_head_t *head, uint8_t *out)
{
	int w, h;
	uint8_t *px = read_jpeg(jpg, DISC_TEX, &w, &h);
	if (!px)
		return false;
	uint8_t *mark = malloc((size_t)w * h);
	int hx, hy;
	if (!mark || !find_white(px, w, h, mark, w * h / 400, &hx, &hy)) {
		free(mark);
		free(px);
		return false;
	}
	// the disc's box
	int x0 = w, y0 = h, x1 = -1, y1 = -1;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			if (mark[y * w + x] != 1) {
				if (x < x0) x0 = x;
				if (x > x1) x1 = x;
				if (y < y0) y0 = y;
				if (y > y1) y1 = y;
			}
	if (x1 - x0 < w / 4 || y1 - y0 < h / 4) {   // not a disc on white
		free(mark);
		free(px);
		return false;
	}
	int bw = x1 - x0 + 1, bh = y1 - y0 + 1, ow, oh;
	covers_fit(bw, bh, FIT, &ow, &oh);
	if (bw <= FIT && bh <= FIT) {   // enlarge a small photo to fit
		ow = bw >= bh ? FIT : bw * FIT / bh;
		oh = bw >= bh ? bh * FIT / bw : FIT;
	}
	int ox = (DISC_TEX - ow) / 2, oy = (DISC_TEX - oh) / 2;
	covers_gamma_init();
	memset(out, 0, DISC_BYTES);
	// area average in linear light, premultiplied by coverage; the edge pixels'
	// white is taken out again (un-mixed) before averaging
	for (int y = 0; y < oh; y++)
		for (int x = 0; x < ow; x++) {
			int sx0 = x0 + x * bw / ow, sx1 = x0 + (x + 1) * bw / ow;
			int sy0 = y0 + y * bh / oh, sy1 = y0 + (y + 1) * bh / oh;
			if (sx1 <= sx0) sx1 = sx0 + 1;
			if (sy1 <= sy0) sy1 = sy0 + 1;
			unsigned long r = 0, g = 0, b = 0, a = 0, n = 0;
			for (int yy = sy0; yy < sy1 && yy < h; yy++)
				for (int xx = sx0; xx < sx1 && xx < w; xx++) {
					int i = yy * w + xx, c = coverage(px, mark, w, h, i);
					n++;
					if (!c)
						continue;
					const uint8_t *p = px + i * 3;
					unsigned lr = covers_lin[p[0]], lg = covers_lin[p[1]], lb = covers_lin[p[2]];
					if (c < 255) {   // p = c * disc + (1 - c) * white
						long k = 4095L * (255 - c);
						lr = lr * 255 > (unsigned long)k ? (lr * 255 - k) / c : 0;
						lg = lg * 255 > (unsigned long)k ? (lg * 255 - k) / c : 0;
						lb = lb * 255 > (unsigned long)k ? (lb * 255 - k) / c : 0;
						if (lr > 4095) lr = 4095;
						if (lg > 4095) lg = 4095;
						if (lb > 4095) lb = 4095;
					}
					r += lr * c; g += lg * c; b += lb * c; a += c;
				}
			if (!a)
				continue;
			unsigned cr = covers_srgb[r / a], cg = covers_srgb[g / a], cb = covers_srgb[b / a], ca = (unsigned)(a / n);
			put(out, ox + x, oy + y, cr << 24 | cg << 16 | cb << 8 | ca);
		}
	free(mark);
	free(px);
	// where the hole is in the texture (or the middle)
	int thx = hx >= 0 ? ox + (hx - x0) * ow / bw : DISC_TEX / 2;
	int thy = hy >= 0 ? oy + (hy - y0) * oh / bh : DISC_TEX / 2;
	*head = (disc_head_t){ DISC_MAGIC, DISC_VERSION, ox, oy, ox + ow, oy + oh, thx, thy, oy + oh / 10, thy - oh / 9 };

	// the wordmark, light grey, across the disc below the hole (the decoder
	// flattens it onto the card colour: its black ink comes back as coverage)
	int lw, lh;
	uint32_t *logo = wordmark ? covers_png_decode(NULL, 0, wordmark, COVER_TEX, &lw, &lh) : NULL;
	if (logo) {
		int dw = ow * 44 / 100, dh = lh * dw / lw;
		int lx = thx - dw / 2, ly = thy + (oy + oh - thy) * 46 / 100 - dh / 2;
		for (int y = 0; y < dh; y++)
			for (int x = 0; x < dw; x++) {
				int tx = lx + x, ty = ly + y;
				if (tx < 0 || ty < 0 || tx >= DISC_TEX || ty >= DISC_TEX)
					continue;
				int sx0 = x * lw / dw, sx1 = (x + 1) * lw / dw, sy0 = y * lh / dh, sy1 = (y + 1) * lh / dh;
				if (sx1 <= sx0) sx1 = sx0 + 1;
				if (sy1 <= sy0) sy1 = sy0 + 1;
				unsigned ink = 0, n = 0;
				for (int yy = sy0; yy < sy1; yy++)
					for (int xx = sx0; xx < sx1; xx++, n++) {
						int bl = logo[yy * lw + xx] >> 8 & 255;
						ink += bl >= COVER_BG_B ? 0 : (COVER_BG_B - bl) * 255 / COVER_BG_B;
					}
				ink = ink / n * 85 / 100;
				if (!ink)
					continue;
				unsigned tx8 = tx & 7, ty8 = ty & 7;
				unsigned z = (tx8 & 1) | (ty8 & 1) << 1 | (tx8 & 2) << 1 | (ty8 & 2) << 2 | (tx8 & 4) << 2 | (ty8 & 4) << 3;
				uint32_t *d = (uint32_t *)out + ((ty >> 3) * (DISC_TEX / 8) + (tx >> 3)) * 64 + z;
				if (!(*d & 255))
					continue;   // only on the disc
				int r = *d >> 24, g = *d >> 16 & 255, b = *d >> 8 & 255, k = (int)ink;
				r += (214 - r) * k / 255;
				g += (214 - g) * k / 255;
				b += (220 - b) * k / 255;
				*d = (uint32_t)r << 24 | (uint32_t)g << 16 | (uint32_t)b << 8 | (*d & 255);
			}
		free(logo);
	}
	return true;
}
