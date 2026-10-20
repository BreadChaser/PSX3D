// Real 3D: per-vertex depth by VRAM position and the right eye's disparity.
// Platform independent (also built into the PC replay harness).
#include "gpustereo.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Depth table: two-way, 64K entries of 64 bits, written by the main thread
// and read by the GPU thread. One entry is one atomic store/load: key (VRAM
// x, y, 12 bits each), GTE depth, generation, valid bit.
#define GS_BITS 16
#define GS_SIZE (1u << GS_BITS)
static uint64_t *table;
static uint16_t gen;                 // main thread frame counter
static float strength, focus, aim;   // published to the GPU thread
static double logsum;
static unsigned samples;
static unsigned n_lookups, n_found, n_shifted;

// Disparity in PS1 pixels for the slider fully up: things far away appear
// up to MAX_BEHIND pixels behind the screen, near ones at most MAX_FRONT
// in front of it (popping out further is uncomfortable).
#define MAX_BEHIND 8.0f
#define MAX_FRONT  0.35f   // as a fraction of MAX_BEHIND

static inline int sext11(uint32_t v) { return ((int32_t)(v << 21)) >> 21; }
static inline uint32_t key_of(int x, int y) { return ((uint32_t)(y & 0xfff) << 12) | (uint32_t)(x & 0xfff); }
static inline uint32_t slot_of(uint32_t key) { return ((key * 2654435761u) >> (32 - GS_BITS)) & ~1u; }
static inline uint64_t load(unsigned i) { return __atomic_load_n(&table[i], __ATOMIC_RELAXED); }
static inline void store(unsigned i, uint64_t e) { __atomic_store_n(&table[i], e, __ATOMIC_RELAXED); }
#define E_VALID (1ull << 56)
static inline bool fresh(uint64_t e, uint16_t now) { return (e & E_VALID) && (uint16_t)(now - (uint16_t)(e >> 40)) <= 1; }

bool gs_core_alloc(void)
{
	if (!table)
		table = calloc(GS_SIZE, sizeof(*table));
	return table != NULL;
}

void gs_core_free(void)
{
	free(table);
	table = NULL;
	strength = focus = aim = 0;
}

void gs_core_clear(void)
{
	if (table)
		memset(table, 0, GS_SIZE * sizeof(*table));
	focus = aim = 0;
	logsum = 0;
	samples = 0;
}

static void put(int x, int y, float z)
{
	uint32_t key = key_of(x, y);
	unsigned s = slot_of(key);
	uint64_t e = load(s);
	// keep a different position written this frame: use the other way
	if (fresh(e, gen) && (uint32_t)(e & 0xffffff) != key)
		s ^= 1;
	int zi = (int)lrintf(z);
	zi = zi < 1 ? 1 : zi > 65535 ? 65535 : zi;
	store(s, E_VALID | ((uint64_t)gen << 40) | ((uint64_t)zi << 24) | key);
}

static float get(int x, int y, uint16_t now)
{
	uint32_t key = key_of(x, y);
	unsigned s = slot_of(key);
	for (int w = 0; w < 2; w++) {
		uint64_t e = load(s ^ w);
		if (fresh(e, now) && (uint32_t)(e & 0xffffff) == key)
			return (float)((e >> 24) & 0xffff);
	}
	return 0;
}

// --- capture: the same triangles and clipping the renderer makes ----------
typedef struct { int x, y; } pt;
static float tri_a[3][3];   // triangle corners: x, y, 1/z

// perspective-correct depth at p on the triangle being clipped
static float depth_at(int x, int y)
{
	float (*t)[3] = tri_a;
	float d = (t[1][0] - t[0][0]) * (t[2][1] - t[0][1]) - (t[2][0] - t[0][0]) * (t[1][1] - t[0][1]);
	if (d == 0)
		return 1 / ((t[0][2] + t[1][2] + t[2][2]) / 3);
	float b1 = ((x - t[0][0]) * (t[2][1] - t[0][1]) - (t[2][0] - t[0][0]) * (y - t[0][1])) / d;
	float b2 = ((t[1][0] - t[0][0]) * (y - t[0][1]) - (x - t[0][0]) * (t[1][1] - t[0][1])) / d;
	float inv = t[0][2] + b1 * (t[1][2] - t[0][2]) + b2 * (t[2][2] - t[0][2]);
	return inv > 0 ? 1 / inv : 0;
}

// gpu_pica's clip_area, positions only (its rounding: lrintf)
static void clip_put(const pt *t, int x0, int y0, int x1, int y1)
{
	pt a[9], b[9];
	int n = 3, m;
	memcpy(a, t, 3 * sizeof(pt));
	for (int side = 0; side < 4; side++) {
		m = 0;
		for (int i = 0; i < n; i++) {
			const pt *p = &a[i], *q = &a[(i + 1) % n];
			int pc = side == 0 ? p->x - x0 : side == 1 ? x1 - p->x : side == 2 ? p->y - y0 : y1 - p->y;
			int qc = side == 0 ? q->x - x0 : side == 1 ? x1 - q->x : side == 2 ? q->y - y0 : y1 - q->y;
			if (pc >= 0)
				b[m++] = *p;
			if ((pc >= 0) != (qc >= 0)) {
				float f = (float)pc / (pc - qc);
				pt *o = &b[m++];
				o->x = p->x + (int)lrintf((q->x - p->x) * f);
				o->y = p->y + (int)lrintf((q->y - p->y) * f);
				if (side < 2) o->x = side == 0 ? x0 : x1;
				else o->y = side == 2 ? y0 : y1;
			}
		}
		if (m < 3)
			return;
		memcpy(a, b, m * sizeof(pt));
		n = m;
	}
	for (int i = 0; i < n; i++) {
		float z = depth_at(a[i].x, a[i].y);
		if (z > 0)
			put(a[i].x, a[i].y, z);
	}
}

void gs_capture(const uint32_t *w, uint32_t cmd, uint32_t e3, uint32_t e4, uint32_t e5, const uint16_t z[4])
{
	if (!table || cmd < 0x20 || cmd >= 0x40)
		return;
	int n = (cmd & 8) ? 4 : 3, found = 0;
	float inv = 0;
	for (int k = 0; k < n; k++)
		if (z[k]) { inv += 1.0f / z[k]; found++; }
	n_lookups += n; n_found += found;
	// Two known corners at least: a 2D polygon can share one position
	// with a projected point by chance.
	if (found < 2)
		return;
	for (int k = 0; k < n; k++)
		if (z[k]) {
			union { float f; uint32_t u; } bits = { .f = z[k] };
			logsum += (int)(bits.u - 0x3f800000) * (1.0 / (1 << 23));
			samples++;
		}
	inv /= found;
	int ox = sext11(e5), oy = sext11(e5 >> 11);
	pt v[4];
	float iz[4];
	int i = 1;
	for (int k = 0; k < n; k++) {
		if (k && (cmd & 0x10))
			i++;   // gouraud colour
		uint32_t xy = w[i++];
		if (cmd & 4)
			i++;   // texture coordinates
		v[k].x = sext11(xy) + ox;
		v[k].y = sext11(xy >> 16) + oy;
		iz[k] = z[k] ? 1.0f / z[k] : inv;
	}
	int x0 = e3 & 0x3ff, y0 = (e3 >> 10) & 0x1ff;
	int x1 = (e4 & 0x3ff) + 1, y1 = ((e4 >> 10) & 0x1ff) + 1;
	static const int idx[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };
	for (int t = 0; t < n - 2; t++) {
		pt tri[3];
		for (int k = 0; k < 3; k++)
			tri[k] = v[idx[t][k]];
		int lx = tri[0].x, hx = lx, ly = tri[0].y, hy = ly;
		for (int k = 1; k < 3; k++) {
			lx = tri[k].x < lx ? tri[k].x : lx; hx = tri[k].x > hx ? tri[k].x : hx;
			ly = tri[k].y < ly ? tri[k].y : ly; hy = tri[k].y > hy ? tri[k].y : hy;
		}
		if (hx - lx > 1023 || hy - ly > 511)
			continue;   // the PS1 drops it
		// draw_poly_slow: one wrap for the whole triangle
		int dx = sext11(lx) - lx, dy = sext11(ly) - ly;
		for (int k = 0; k < 3; k++) {
			tri[k].x += dx; tri[k].y += dy;
			tri_a[k][0] = tri[k].x; tri_a[k][1] = tri[k].y; tri_a[k][2] = iz[idx[t][k]];
		}
		lx += dx; hx += dx; ly += dy; hy += dy;
		if (hx <= x0 || lx >= x1 || hy <= y0 || ly >= y1)
			continue;
		if (lx >= x0 && hx <= x1 && ly >= y0 && hy <= y1) {
			for (int k = 0; k < 3; k++)
				put(tri[k].x, tri[k].y, 1 / tri_a[k][2]);
		} else
			clip_put(tri, x0, y0, x1, y1);
	}
}

void gs_core_frame(float s)
{
	if (samples >= 6) {
		// back from the bit-pattern logarithm (the same approximation)
		union { float f; uint32_t u; } bits = { .u = (uint32_t)(0x3f800000 + (int)(logsum / samples * (1 << 23))) };
		float z = bits.f * 0.8f;
		if (focus <= 0)
			focus = aim = z;
		else if (z < aim * 0.87f || z > aim * 1.15f)
			aim = z;   // hysteresis: small changes don't move the focus
		focus += (aim - focus) * 0.04f;
	}
	logsum = 0;
	samples = 0;
	strength = s;
	gen++;
}

float gs_focus(void) { return focus; }

void gs_core_counts(unsigned *lookups, unsigned *found, unsigned *shifted)
{
	*lookups = n_lookups; *found = n_found; *shifted = n_shifted;
	n_lookups = n_found = n_shifted = 0;
}

// Polygon vertices sit at pixel * 16 + (8, 7) (POLY_SHIFT_X/Y in gpu_pica);
// sprites, rectangles, lines and uploads don't, and stay on the screen plane.
int gs_shift(const gs_vtx *src, gs_vtx *dst, int n)
{
	float f = focus, k = 16 * MAX_BEHIND * strength;
	uint16_t now = __atomic_load_n(&gen, __ATOMIC_RELAXED);
	int moved = 0;
	memcpy(dst, src, n * sizeof(*dst));
	if (!table || f <= 0 || k <= 0)
		return 0;
	for (int t = 0; t + 3 <= n; t += 3) {
		float z[3], sum = 0;
		int found = 0;
		for (int i = 0; i < 3; i++) {
			const gs_vtx *v = &src[t + i];
			z[i] = 0;
			if ((v->x & 15) == 8 && (v->y & 15) == 7) {
				int x = v->x >> 4, y = v->y >> 4;
				z[i] = get(x, y, now);
				// a clipped vertex can round differently: look around it
				for (int j = 0; !z[i] && j < 8; j++) {
					static const int8_t ax[8] = { 1, -1, 0, 0, 1, -1, 1, -1 }, ay[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
					z[i] = get(x + ax[j], y + ay[j], now);
				}
			}
			if (z[i]) { sum += 1 / z[i]; found++; }
		}
		if (!found)
			continue;
		for (int i = 0; i < 3; i++) {
			float zz = z[i] ? z[i] : found / sum;
			float d = 1 - f / zz;
			d = d < -MAX_FRONT ? -MAX_FRONT : d > 1 ? 1 : d;
			int x = dst[t + i].x + (int)lrintf(k * d);
			dst[t + i].x = x < 0 ? 0 : x > 1024 * 16 - 1 ? 1024 * 16 - 1 : x;
			moved++;
		}
	}
	n_shifted += moved;
	return moved;
}
