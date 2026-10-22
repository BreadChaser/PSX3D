// Minimal QR code encoder: byte mode, error correction level M, versions
// 1-10 (up to 213 bytes). Follows ISO/IEC 18004 as laid out in Project
// Nayuki's reference implementation. Plain C, also built on the PC for tests.
#include "qrcode.h"
#include <string.h>

static const uint8_t ecc_per_block[11] = {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};
static const uint8_t num_blocks[11] = {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};

static int raw_modules(int v)
{
	int r = (16 * v + 128) * v + 64;
	if (v >= 2) {
		int n = v / 7 + 2;
		r -= (25 * n - 10) * n - 55;
		if (v >= 7)
			r -= 36;
	}
	return r;
}

static uint8_t gf_mul(uint8_t x, uint8_t y)
{
	int z = 0;
	for (int i = 7; i >= 0; i--) {
		z = (z << 1) ^ ((z >> 7) * 0x11d);
		z ^= ((y >> i) & 1) * x;
	}
	return (uint8_t)z;
}

static void rs_remainder(const uint8_t *data, int n, int degree, uint8_t *out)
{
	uint8_t gen[32] = {0};
	gen[degree - 1] = 1;
	uint8_t root = 1;
	for (int i = 0; i < degree; i++) {
		for (int j = 0; j < degree; j++) {
			gen[j] = gf_mul(gen[j], root);
			if (j + 1 < degree)
				gen[j] ^= gen[j + 1];
		}
		root = gf_mul(root, 2);
	}
	memset(out, 0, degree);
	for (int i = 0; i < n; i++) {
		uint8_t f = data[i] ^ out[0];
		memmove(out, out + 1, degree - 1);
		out[degree - 1] = 0;
		for (int j = 0; j < degree; j++)
			out[j] ^= gf_mul(gen[j], f);
	}
}

typedef struct {
	int size;
	uint8_t *m, isf[QR_MAX_SIZE * QR_MAX_SIZE];
} grid;
static void put(grid *g, int x, int y, int dark, int function)
{
	g->m[y * g->size + x] = (uint8_t)dark;
	if (function)
		g->isf[y * g->size + x] = 1;
}

static void format_bits(grid *g, int mask)
{
	int data = 0 << 3 | mask;   // level M = 0b00
	int rem = data;
	for (int i = 0; i < 10; i++)
		rem = (rem << 1) ^ ((rem >> 9) * 0x537);
	int bits = (data << 10 | rem) ^ 0x5412, s = g->size;
#define B(i) ((bits >> (i)) & 1)
	for (int i = 0; i <= 5; i++) put(g, 8, i, B(i), 1);
	put(g, 8, 7, B(6), 1);
	put(g, 8, 8, B(7), 1);
	put(g, 7, 8, B(8), 1);
	for (int i = 9; i < 15; i++) put(g, 14 - i, 8, B(i), 1);
	for (int i = 0; i < 8; i++) put(g, s - 1 - i, 8, B(i), 1);
	for (int i = 8; i < 15; i++) put(g, 8, s - 15 + i, B(i), 1);
	put(g, 8, s - 8, 1, 1);
#undef B
}

static void function_patterns(grid *g, int v)
{
	int s = g->size;
	for (int i = 0; i < s; i++) {
		put(g, 6, i, i % 2 == 0, 1);
		put(g, i, 6, i % 2 == 0, 1);
	}
	int fx[3] = {3, s - 4, 3}, fy[3] = {3, 3, s - 4};
	for (int f = 0; f < 3; f++)
		for (int dy = -4; dy <= 4; dy++)
			for (int dx = -4; dx <= 4; dx++) {
				int x = fx[f] + dx, y = fy[f] + dy;
				int d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy;
				if (e > d) d = e;
				if (x >= 0 && x < s && y >= 0 && y < s)
					put(g, x, y, d != 2 && d != 4, 1);
			}
	if (v >= 2) {
		int n = v / 7 + 2, pos[7];
		int step = (v * 8 + n * 3 + 5) / (n * 4 - 4) * 2;
		pos[0] = 6;
		for (int i = n - 1, p = s - 7; i >= 1; i--, p -= step)
			pos[i] = p;
		for (int i = 0; i < n; i++)
			for (int j = 0; j < n; j++) {
				if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))
					continue;
				for (int dy = -2; dy <= 2; dy++)
					for (int dx = -2; dx <= 2; dx++) {
						int d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy;
						put(g, pos[i] + dx, pos[j] + dy, (d > e ? d : e) != 1, 1);
					}
			}
	}
	format_bits(g, 0);   // reserves the area; drawn again with the mask
	if (v >= 7) {
		int rem = v;
		for (int i = 0; i < 12; i++)
			rem = (rem << 1) ^ ((rem >> 11) * 0x1f25);
		long bits = (long)v << 12 | rem;
		for (int i = 0; i < 18; i++) {
			int bit = (bits >> i) & 1, a = s - 11 + i % 3, b = i / 3;
			put(g, a, b, bit, 1);
			put(g, b, a, bit, 1);
		}
	}
}

static int mask_bit(int mask, int x, int y)
{
	switch (mask) {
	case 0: return (x + y) % 2 == 0;
	case 1: return y % 2 == 0;
	case 2: return x % 3 == 0;
	case 3: return (x + y) % 3 == 0;
	case 4: return (x / 3 + y / 2) % 2 == 0;
	case 5: return x * y % 2 + x * y % 3 == 0;
	case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
	default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
	}
}

static void apply_mask(grid *g, int mask)
{
	for (int y = 0; y < g->size; y++)
		for (int x = 0; x < g->size; x++)
			if (!g->isf[y * g->size + x] && mask_bit(mask, x, y))
				g->m[y * g->size + x] ^= 1;
}

static long penalty(const grid *g)
{
	int s = g->size;
	long p = 0, dark = 0;
#define AT(x, y) g->m[(y) * s + (x)]
	for (int pass = 0; pass < 2; pass++)
		for (int a = 0; a < s; a++) {
			int run = 0, prev = -1;
			for (int b = 0; b < s; b++) {
				int c = pass ? AT(a, b) : AT(b, a);
				if (c == prev)
					run++;
				else {
					if (run >= 5) p += 3 + run - 5;
					run = 1;
					prev = c;
				}
				// finder-like 1011101 with four light modules on one side
				if (b >= 10) {
					static const uint8_t pat[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
					int f = 1, r = 1;
					for (int k = 0; k < 11; k++) {
						int v = pass ? AT(a, b - 10 + k) : AT(b - 10 + k, a);
						f &= v == pat[k];
						r &= v == pat[10 - k];
					}
					p += 40 * (f + r);
				}
			}
			if (run >= 5) p += 3 + run - 5;
		}
	for (int y = 0; y < s - 1; y++)
		for (int x = 0; x < s - 1; x++) {
			int c = AT(x, y);
			if (c == AT(x + 1, y) && c == AT(x, y + 1) && c == AT(x + 1, y + 1))
				p += 3;
		}
	for (int i = 0; i < s * s; i++)
		dark += g->m[i];
	long total = (long)s * s;
	long k = ((dark * 20 - total * 10 < 0 ? total * 10 - dark * 20 : dark * 20 - total * 10) + total - 1) / total - 1;
	if (k > 0) p += k * 10;
#undef AT
	return p;
}

int qr_encode(const char *text, uint8_t *modules, int *size_out)
{
	size_t len = strlen(text);
	int v;
	int data_cw = 0;
	for (v = 1; v <= 10; v++) {
		data_cw = raw_modules(v) / 8 - ecc_per_block[v] * num_blocks[v];
		int bits = 4 + (v < 10 ? 8 : 16) + 8 * (int)len;
		if (bits <= data_cw * 8)
			break;
	}
	if (v > 10)
		return 0;
	uint8_t data[400] = {0}, all[400];
	int bit = 0;
#define PUSH(val, n)                                                                   \
	for (int i_ = (n) - 1; i_ >= 0; i_--, bit++)                                          \
		data[bit >> 3] |= (uint8_t)((((val) >> i_) & 1) << (7 - (bit & 7)));
	PUSH(4, 4);
	PUSH((int)len, v < 10 ? 8 : 16);
	for (size_t i = 0; i < len; i++)
		PUSH((uint8_t)text[i], 8);
	int term = data_cw * 8 - bit < 4 ? data_cw * 8 - bit : 4;
	bit += term;
	bit = (bit + 7) & ~7;
	for (int pad = 0xec; bit < data_cw * 8; pad ^= 0xec ^ 0x11)
		PUSH(pad, 8);
#undef PUSH
	// split into blocks, add error correction, interleave
	int nb = num_blocks[v], ecl = ecc_per_block[v], raw = raw_modules(v) / 8;
	int nshort = nb - raw % nb, short_len = raw / nb;
	uint8_t blocks[8][160];
	int k = 0;
	for (int i = 0; i < nb; i++) {
		int dl = short_len - ecl + (i < nshort ? 0 : 1);
		memcpy(blocks[i], data + k, dl);
		k += dl;
		uint8_t ecc[32];
		rs_remainder(blocks[i], dl, ecl, ecc);
		if (i < nshort) {
			memcpy(blocks[i] + dl + 1, ecc, ecl);
			blocks[i][dl] = 0;
		} else
			memcpy(blocks[i] + dl, ecc, ecl);
	}
	int n = 0;
	for (int i = 0; i < short_len + 1; i++)
		for (int j = 0; j < nb; j++)
			if (i != short_len - ecl || j >= nshort)
				all[n++] = blocks[j][i];
	// draw
	static grid g;
	g.size = v * 4 + 17;
	g.m = modules;
	memset(modules, 0, g.size * g.size);
	memset(g.isf, 0, sizeof(g.isf));
	function_patterns(&g, v);
	int i = 0, s = g.size;
	for (int right = s - 1; right >= 1; right -= 2) {
		if (right == 6)
			right = 5;
		for (int vert = 0; vert < s; vert++)
			for (int j = 0; j < 2; j++) {
				int x = right - j, up = ((right + 1) & 2) == 0, y = up ? s - 1 - vert : vert;
				if (!g.isf[y * s + x] && i < n * 8) {
					g.m[y * s + x] = (all[i >> 3] >> (7 - (i & 7))) & 1;
					i++;
				}
			}
	}
	int best = 0;
	long best_p = -1;
	for (int mask = 0; mask < 8; mask++) {
		apply_mask(&g, mask);
		format_bits(&g, mask);
		long p = penalty(&g);
		if (best_p < 0 || p < best_p)
			best_p = p, best = mask;
		apply_mask(&g, mask);
	}
	apply_mask(&g, best);
	format_bits(&g, best);
	*size_out = s;
	return v;
}
