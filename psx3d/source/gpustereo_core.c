// Real 3D: per-vertex depth by VRAM position and the right eye's disparity.
// Platform independent (also built into the PC replay harness).
#include "gpustereo.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Depth table: four-way, 64K entries of 64 bits, written by the main thread
// and read by the GPU thread. One entry is one atomic store/load: key (VRAM
// x, y, 12 bits each), GTE depth, generation, valid bit.
#define GS_BITS 16
#define GS_SIZE (1u << GS_BITS)
#define GS_WAYS 4
static uint64_t *table;
static uint16_t gen;                 // main thread frame counter
static float strength, scale;        // published to the GPU thread
static float near_z, far_z;          // the scene's depth range (smoothed); 0: unknown
static float aim_n, aim_f;
static float log_near, gain;         // log2(near), 1 / log2(far / near)
// depth samples of this frame: a histogram of log2(z) in 1/16 octaves
#define HBINS 256
static uint32_t hist[HBINS];
static unsigned samples;
static unsigned n_lookups, n_found, n_shifted, n_rejected;
// Backdrops: PS1 games paint far to near, but many draw their sky or
// backdrop as small geometry close to the camera, first. The depth bins of
// this frame's 3D polygons in draw order find such a leading part (nearer
// than what is drawn over it); the next frame puts its polygons that are
// that early and that near at the back.
#define MAXSEQ 16384
static uint8_t *seq;
static unsigned nseq, bd_count, bd_bin, bd_hold, bd_found;

// The right eye's disparity, in PS1 pixels of a 320-wide picture, with the
// slider fully up: the scene's nearest depths (5% of its vertices nearer)
// sit on the screen, its far depths (5% further) MAX_BEHIND pixels behind
// it, with depth in between spread evenly by ratio (log z). A stereo
// camera's 1/z would bunch most of the scene up near the back, flat; log z
// gives every scene, indoors or out, the same visible layering. Anything
// nearer than the near depth comes out of the screen, at most MAX_FRONT of
// the range (popping out further is uncomfortable).
#define MAX_BEHIND 10.0f
#define MAX_FRONT  0.25f   // as a fraction of MAX_BEHIND
#define MIN_OCTAVES 1.5f   // far / near at least 2.8: a shallow scene isn't stretched more
// The disparity of a triangle's corners may change by at most this many
// pixels per pixel along its edges (plus SLACK): a steeper corner took
// another object's depth (two vertices on the same screen pixel), and
// the eyes couldn't fuse such a step anyway.
#define MAX_GRADIENT 0.3f
#define SLACK 1.0f
// Screen depth fallback: a frame whose polygons carry next to no depth
// (logos, movies, menus, 2D games, pre-rendered backgrounds) has nothing to
// give real 3D, so after FLAT_AFTER such shown frames in a row the whole
// picture moves behind the screen instead (as before v0.5.0); real 3D
// returns as soon as depth does. Either way the change fades over FADE
// frames. Games drawing at 30, 20 or 15 fps leave frames without new
// polygons in between: too few in a row to count as flat, and up to
// FADE_ON of them still fade real 3D in; a longer gap holds the fade, so a
// stray depth frame in a 2D scene doesn't dip it.
#define MIN_SAMPLES 48
#define FLAT_AFTER 12
#define FADE_ON 4
#define FADE 24
static unsigned flat_run = FLAT_AFTER, flat_step = FADE;   // flat_step: 0 = real 3D, FADE = flat
static float flat = 1;                                       // eased flat_step / FADE

// log2, within 0.005
static inline float fast_log2(float z)
{
	union { float f; uint32_t u; } b = { .f = z };
	float e = (float)((int)(b.u >> 23) - 127);
	b.u = (b.u & 0x7fffff) | 0x3f800000;
	float m = b.f - 1;
	return e + m * (1.3466f - 0.3466f * m);
}

// once per frame or per game: optimised for size (the frontend's code sits
// before the core's aligned start, which must not move)
#define GS_COLD __attribute__((cold))

static inline int sext11(uint32_t v) { return ((int32_t)(v << 21)) >> 21; }
static inline uint32_t key_of(int x, int y) { return ((uint32_t)(y & 0xfff) << 12) | (uint32_t)(x & 0xfff); }
static inline uint32_t slot_of(uint32_t key) { return ((key * 2654435761u) >> (32 - GS_BITS)) & ~(GS_WAYS - 1u); }
static inline uint64_t load(unsigned i) { return __atomic_load_n(&table[i], __ATOMIC_RELAXED); }
static inline void store(unsigned i, uint64_t e) { __atomic_store_n(&table[i], e, __ATOMIC_RELAXED); }
#define E_VALID (1ull << 56)
#define E_LEFT  (1ull << 57)   // on the drawing area's left edge
#define E_RIGHT (1ull << 59)   // on its right edge
#define E_FAR   (1ull << 58)   // a backdrop's: at the back
static inline unsigned age(uint64_t e, uint16_t now) { return e & E_VALID ? (uint16_t)(now - (uint16_t)(e >> 40)) : 0xffff; }

GS_COLD bool gs_core_alloc(void)
{
	if (!table)
		table = calloc(GS_SIZE, sizeof(*table));
	if (!seq)
		seq = malloc(MAXSEQ);
	return table && seq;
}

GS_COLD void gs_core_free(void)
{
	free(table);
	free(seq);
	table = NULL;
	seq = NULL;
	strength = near_z = far_z = 0;
}

GS_COLD void gs_core_clear(void)
{
	if (table)
		memset(table, 0, GS_SIZE * sizeof(*table));
	near_z = far_z = aim_n = aim_f = 0;
	memset(hist, 0, sizeof(hist));
	samples = nseq = bd_count = bd_hold = 0;
	flat_run = FLAT_AFTER;
	flat_step = FADE;
	flat = 1;
}

// A vertex on the drawing area's left edge doesn't move right in the right
// eye (on the right edge: left): the scene still reaches the picture's
// edges (an edge moved inwards would leave a strip the right eye never
// draws). Moved outwards, the GPU's scissor cuts it at the edge.
static uint64_t put_flags;   // E_FAR while a backdrop polygon is captured
static void put(int x, int y, float z, uint64_t edge)
{
	uint32_t key = key_of(x, y);
	unsigned s = slot_of(key), victim = s, oldest = 0;
	// the same position again, else the way written longest ago
	for (unsigned w = 0; w < GS_WAYS; w++) {
		uint64_t e = load(s + w);
		if ((e & E_VALID) && (uint32_t)(e & 0xffffff) == key) {
			victim = s + w;
			break;
		}
		unsigned a = age(e, gen);
		if (a > oldest)
			oldest = a, victim = s + w;
	}
	int zi = (int)lrintf(z);
	zi = zi < 1 ? 1 : zi > 65535 ? 65535 : zi;
	store(victim, E_VALID | put_flags | edge | ((uint64_t)gen << 40) | ((uint64_t)zi << 24) | key);
}

// the entry for the position (0: none)
static uint64_t get(int x, int y, uint16_t now)
{
	uint32_t key = key_of(x, y);
	unsigned s = slot_of(key);
	for (unsigned w = 0; w < GS_WAYS; w++) {
		uint64_t e = load(s + w);
		if (age(e, now) <= 1 && (uint32_t)(e & 0xffffff) == key)
			return e;
	}
	return 0;
}

// --- capture: the same triangles and clipping the renderer makes ----------
typedef struct { int x, y; } pt;
static float tri_a[3][3];   // triangle corners: x, y, 1/z
static bool tri_known[3];   // the corner's own depth is known (not the polygon's mean)

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
		bool corner_unknown = false;
		for (int k = 0; k < 3; k++)
			corner_unknown |= !tri_known[k] && a[i].x == t[k].x && a[i].y == t[k].y;
		float z = depth_at(a[i].x, a[i].y);
		if (z > 0 && !corner_unknown)
			put(a[i].x, a[i].y, z, a[i].x == x0 ? E_LEFT : a[i].x == x1 ? E_RIGHT : 0);
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
	inv /= found;
	int ox = sext11(e5), oy = sext11(e5 >> 11);
	pt v[4];
	float iz[4];
	int i = 1, minx = 9999, maxx = -9999, miny = 9999, maxy = -9999;
	for (int k = 0; k < n; k++) {
		if (k && (cmd & 0x10))
			i++;   // gouraud colour
		uint32_t xy = w[i++];
		if (cmd & 4)
			i++;   // texture coordinates
		v[k].x = sext11(xy) + ox;
		v[k].y = sext11(xy >> 16) + oy;
		iz[k] = z[k] ? 1.0f / z[k] : inv;
		minx = v[k].x < minx ? v[k].x : minx; maxx = v[k].x > maxx ? v[k].x : maxx;
		miny = v[k].y < miny ? v[k].y : miny; maxy = v[k].y > maxy ? v[k].y : maxy;
	}
	int x0 = e3 & 0x3ff, y0 = (e3 >> 10) & 0x1ff;
	int x1 = (e4 & 0x3ff) + 1, y1 = ((e4 >> 10) & 0x1ff) + 1;
	put_flags = 0;
	// The scene's depth range and the draw order count only what is drawn:
	// geometry outside the drawing area (behind or beside the camera) would
	// pull the near depth in and push the visible scene back.
	if (maxx > x0 && minx < x1 && maxy > y0 && miny < y1) {
		union { float f; uint32_t u; } bits = { .f = 1 / inv };
		unsigned b = (bits.u - 0x3f800000) >> 19;
		b = b < HBINS ? b : HBINS - 1;
		if (bd_count && nseq < bd_count && b < bd_bin) {
			put_flags = E_FAR;
			bd_found++;
		} else {
			hist[b] += found;
			samples += found;
		}
		if (nseq < MAXSEQ)
			seq[nseq] = b;
		nseq++;
	}
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
			tri_known[k] = z[idx[t][k]] != 0;
		}
		lx += dx; hx += dx; ly += dy; hy += dy;
		if (hx <= x0 || lx >= x1 || hy <= y0 || ly >= y1)
			continue;
		if (lx >= x0 && hx <= x1 && ly >= y0 && hy <= y1) {
			// unknown corners stay out: the GPU thread gives them the
			// triangle's depth, and the position may be another polygon's
			for (int k = 0; k < 3; k++)
				if (tri_known[k])
					put(tri[k].x, tri[k].y, 1 / tri_a[k][2], tri[k].x == x0 ? E_LEFT : tri[k].x == x1 ? E_RIGHT : 0);
		} else
			clip_put(tri, x0, y0, x1, y1);
	}
}

// z at the given share of this frame's depth samples (bin centre)
GS_COLD static float percentile(unsigned share)
{
	unsigned want = samples * share / 100, sum = 0;
	int b = 0;
	while (b < HBINS - 1 && (sum += hist[b]) <= want)
		b++;
	union { float f; uint32_t u; } bits = { .u = 0x3f800000 + ((uint32_t)b << 19) + (1u << 18) };
	return bits.f;
}

// Hysteresis: small changes don't move the range; a new one (a camera cut)
// is reached over about half a second.
GS_COLD static void follow(float *now, float *aim, float z)
{
	if (*now <= 0)
		*now = *aim = z;
	else if (z < *aim * 0.87f || z > *aim * 1.15f)
		*aim = z;
	*now += (*aim - *now) * 0.04f;
}

GS_COLD static unsigned median_of(const uint16_t *h, unsigned n)
{
	unsigned sum = 0, b = 0;
	while (b < HBINS - 1 && (sum += h[b]) <= n / 2)
		b++;
	return b;
}

// the leading part of the frame's polygons nearer than the rest (see seq)
GS_COLD static void find_backdrop(void)
{
	static uint16_t all[HBINS], pre[HBINS];
	unsigned n = nseq < MAXSEQ ? nseq : MAXSEQ, best = 0, best_k = 0, best_bin = 0;
	memset(all, 0, sizeof(all));
	memset(pre, 0, sizeof(pre));
	for (unsigned i = 0; i < n; i++)
		all[seq[i]]++;
	for (unsigned j = 1, i = 0; j <= 24 && n >= 64; j++) {
		unsigned k = n * j / 32;   // candidate: the first k polygons (up to 3/4)
		for (; i < k; i++) {
			pre[seq[i]]++;
			all[seq[i]]--;
		}
		unsigned a = median_of(pre, k), b = median_of(all, n - k);
		if (b > a + 12 && b - a > best)   // 3/4 octave nearer at least
			best = b - a, best_k = k, best_bin = b - 4;
	}
	bd_found = 0;
	if (best) {
		// a little more than found: the count varies as the camera turns
		bd_count = best_k + best_k / 4 + 8;
		bd_bin = best_bin;
		bd_hold = 30;
	} else if (bd_hold && !--bd_hold)
		bd_count = 0;
	nseq = 0;
}

GS_COLD void gs_core_frame(float s, int hres)
{
	find_backdrop();
	flat_run = samples >= MIN_SAMPLES ? 0 : flat_run < FLAT_AFTER ? flat_run + 1 : FLAT_AFTER;
	if (flat_run == FLAT_AFTER && flat_step < FADE)
		flat_step++;
	else if (flat_run < FADE_ON && flat_step > 0)
		flat_step--;
	float f = (float)flat_step / FADE;
	flat = f * f * (3 - 2 * f);
	if (samples >= 6) {
		follow(&near_z, &aim_n, percentile(5));
		follow(&far_z, &aim_f, percentile(95));
		float octaves = fast_log2(far_z / near_z);
		log_near = fast_log2(near_z);
		gain = 1 / (octaves > MIN_OCTAVES ? octaves : MIN_OCTAVES);
	}
	memset(hist, 0, sizeof(hist));
	samples = 0;
	// a wider picture has narrower pixels on the screen
	scale = MAX_BEHIND * (hres > 0 ? hres : 320) / 320.0f;
	// the right eye's real disparity fades out as the flat picture's comes in
	strength = s * (1 - flat);
	gen++;
}

float gs_focus(void) { return near_z; }
float gs_flat(void) { return flat; }

GS_COLD void gs_core_counts(unsigned *lookups, unsigned *found, unsigned *shifted, unsigned *rejected)
{
	*lookups = n_lookups; *found = n_found; *shifted = n_shifted; *rejected = n_rejected;
	n_lookups = n_found = n_shifted = n_rejected = 0;
}

// Polygon vertices sit at pixel * 16 + (8, 7) (POLY_SHIFT_X/Y in gpu_pica);
// sprites, rectangles, lines and uploads don't, and stay on the screen plane.
int gs_shift(const gs_vtx *src, gs_vtx *dst, int n)
{
	float zn = near_z, ln = log_near, g = gain, k = 16 * scale * strength;
	uint16_t now = __atomic_load_n(&gen, __ATOMIC_RELAXED);
	int moved = 0;
	memcpy(dst, src, n * sizeof(*dst));
	if (!table || zn <= 0 || k <= 0)
		return 0;
	for (int t = 0; t + 3 <= n; t += 3) {
		float d[3];
		uint64_t edge[3];
		bool known[3];
		int found = 0;
		for (int i = 0; i < 3; i++) {
			const gs_vtx *v = &src[t + i];
			uint64_t e = 0;
			if ((v->x & 15) == 8 && (v->y & 15) == 7) {
				int x = v->x >> 4, y = v->y >> 4;
				e = get(x, y, now);
				// a clipped vertex can round differently: look around it
				for (int j = 0; !e && j < 8; j++) {
					static const int8_t ax[8] = { 1, -1, 0, 0, 1, -1, 1, -1 }, ay[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
					e = get(x + ax[j], y + ay[j], now);
				}
			}
			edge[i] = e & (E_LEFT | E_RIGHT);
			known[i] = e != 0;
			// disparity with the slider fully up, in units of MAX_BEHIND
			d[i] = !known[i] ? 0 : e & E_FAR ? 1 : (fast_log2((float)((e >> 24) & 0xffff)) - ln) * g;
			d[i] = d[i] < -MAX_FRONT ? -MAX_FRONT : d[i] > 1 ? 1 : d[i];
			found += known[i];
		}
		if (!found)
			continue;
		// A corner whose disparity jumps against both other corners took
		// another object's depth: it gets theirs.
		if (found == 3) {
			bool bad[3];
			for (int i = 0; i < 3; i++) {
				const gs_vtx *a = &src[t + i], *b = &src[t + (i + 1) % 3];
				float len = (fabsf((float)(a->x - b->x)) + fabsf((float)(a->y - b->y))) * (1 / 16.0f);
				bad[i] = fabsf(d[i] - d[(i + 1) % 3]) * scale > MAX_GRADIENT * len + SLACK;
			}
			for (int i = 0; i < 3; i++)
				if (bad[i] && bad[(i + 2) % 3] && !bad[(i + 1) % 3]) {
					known[i] = false;
					found--;
					n_rejected++;
				}
		}
		float mean = 0;
		for (int i = 0; i < 3; i++)
			mean += known[i] ? d[i] : 0;
		mean /= found;
		for (int i = 0; i < 3; i++) {
			float dd = known[i] ? d[i] : mean;
			if ((edge[i] & E_LEFT && dd > 0) || (edge[i] & E_RIGHT && dd < 0))
				dd = 0;
			int x = dst[t + i].x + (int)lrintf(k * dd);
			dst[t + i].x = x < 0 ? 0 : x > 1024 * 16 - 1 ? 1024 * 16 - 1 : x;
			moved++;
		}
	}
	n_shifted += moved;
	return moved;
}
