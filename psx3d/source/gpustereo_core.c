// Real 3D: per-vertex depth by VRAM position and the right eye's disparity.
// Platform independent (also built into the PC replay harness).
#include "gpustereo.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Depth tables, written by gs_capture and read by gs_shift, both on one
// thread: the GPU thread on the 3DS (the main thread only queues each
// polygon's corners and GTE depths, see gs_capture; the GPU thread captures
// what is queued before it shifts a batch, see gs_drain).
//
// Triangles: each triangle the renderer will draw (a polygon's, or a piece
// clipping cuts from it) has an entry of its own, found by its three corners'
// positions: their depths (0: none) and flags. The GPU thread finds exactly
// the triangle it draws, never another polygon's corner that happens to sit
// on the same pixel (a 2D logo over a 3D scene took its depths from the scene
// that way and jumped about). Triangles of polygons without depth (2D
// polygons) are entered too, with no depths. Vertices: an entry per position
// holds the depth of a 3D vertex for corners that have none of their own:
// every triangle sharing the vertex then moves it alike (taking each
// triangle's mean opened cracks between them).
// Triangles: four-way, 32K entries of 16 bytes. The corners in the order of
// their keys (VRAM x, y, 12 bits each): the smallest key with the generation
// in bits 24-31, a hash of all three, each corner's depth and flags.
#define TT_BITS 15
#define TT_WAYS 4
typedef struct { uint32_t h, k0; uint16_t z[3], f; } tent;
#define T_EST   (1u << 9)    // a 2D polygon's (no depth, and none to borrow)
#define T_VALID (1u << 15)
#define T_L 1u               // per corner (3 bits each, by key order): on the drawing area's left edge,
#define T_R 2u               // on its right edge,
#define T_FAR 4u             // on a backdrop's vertex
static tent *ttab;
// Vertices: four-way, 32K entries of 8 bytes: key (bits 0-23) and
// generation (24-31), depth (0: none), flags (E_*).
#define VT_BITS 15
#define VT_WAYS 4
typedef union { struct { uint32_t kg; uint16_t z, f; }; uint64_t u; } vent;   // (u: one atomic store/load)
static vent *vtab;
// The polygons queued for the capture (main thread -> GPU thread), in order.
// FIFO_N covers a frame's polygons: the main thread drains what is left at
// the end of each frame (gs_core_frame), with the GPU thread idle.
#define FIFO_N 16384u
static gs_rec *fifo;
static unsigned fifo_head, fifo_tail;   // written by the main thread / by the capture
static unsigned n_dropped;
int gs_defer = 1;
int gs_debug;                           // test switch st3d=N: 1 no capture, 2 no shift
uint64_t (*gs_clock)(void);             // the capture's time (gs_core_ticks), if set
static uint64_t cap_ticks;

// The right eye's vertices (GPU thread): one shift per mono vertex position
// and frame. The first triangle drawn at a position decides it; every later
// triangle on that pixel moves the vertex alike, so polygons that share it
// (and clip points cut from a shared edge) stay sealed, whatever small
// differences their depths have. Only a vertex whose own shift differs by
// more than SNAP (another object on the same pixel) keeps its own, except
// on a backdrop's vertex: what is drawn later on its pixels hangs on to it
// (a sky dome partly found to be the backdrop stays one sheet).
#define VS_BITS 15
#define VS_WAYS 4
#define SNAP (2 * 16)
typedef struct { uint32_t kg; int16_t shift; uint16_t f; } vsent;   // key 0-23, generation 24-31; shift 1/16 px; f: 1 valid, 2 backdrop
static vsent *vshift;
static uint16_t gen;                 // main thread frame counter
static float strength, scale;        // published to the GPU thread
static float near_z;                 // the scene's near depth (smoothed); 0: unknown
static float aim_n;
// depth samples of this frame: a histogram of log2(z) in 1/16 octaves
#define HBINS 256
static uint32_t hist[HBINS];
static unsigned samples;
static unsigned n_lookups, n_found, n_shifted, n_rejected;
#ifdef PSX3D_SIM
unsigned gs_missed;   // triangles drawn without an entry of their own (simulator diagnostics)
#endif
// Backdrops: PS1 games paint far to near, but many draw their sky or
// backdrop as small geometry close to the camera, first. The depth bins of
// this frame's 3D polygons in draw order find such a leading part (nearer
// than what is drawn over it); the next frame puts its polygons that are
// that early and that near at the back.
#define MAXSEQ 16384
static uint8_t *seq;
static unsigned nseq, bd_count, bd_bin, bd_hold, bd_found;

// The right eye's disparity, in PS1 pixels of a 320-wide picture, with the
// slider fully up: 1 - near / z of MAX_BEHIND. The scene's nearest depths
// (5% of its vertices nearer) sit on the screen, infinity MAX_BEHIND pixels
// behind it, depth in between as a stereo camera sees it. Only a function
// of 1/z is linear across a polygon on the screen, as the GPU interpolates
// the shifted corners, and only without a clamp at the back: then a vertex
// in the middle of a neighbour's edge (T-junction) moves exactly with that
// edge and big polygons stay flat (log z with a far clamp, used before,
// opened gap lines there). Anything nearer than the near depth comes out
// of the screen, at most MAX_FRONT of the range (popping out further is
// uncomfortable).
#define MAX_BEHIND 10.0f
#define MAX_FRONT  0.25f   // as a fraction of MAX_BEHIND
// 2D backdrops (painted skies and scenery drawn before the 3D scene, as
// sprites or polygons without depth) go all the way back, to infinity.
// The disparity of a triangle's corners may change by at most this many
// pixels per pixel along its edges (plus SLACK): see gs_capture.
#ifndef MAX_GRADIENT
#define MAX_GRADIENT 0.15f
#endif
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
// passes into the shown buffers (GPU thread, see pass_at); the last: drawing elsewhere
#define PASS_SLOTS 4
static struct pass { int x, y, w, h; unsigned shown, start, depth; bool live, had; } pass[PASS_SLOTS + 1];
static unsigned pass_gen = 1;

// disparity of depth z with the slider fully up, in units of MAX_BEHIND
// (before clamping)
static inline float disp(float z, float zn) { return 1 - zn / z; }

// once per frame or per game: optimised for size (the frontend's code sits
// before the core's aligned start, which must not move)
#define GS_COLD __attribute__((cold))
#define UNROLL _Pragma("GCC unroll 4")

static inline int sext11(uint32_t v) { return ((int32_t)(v << 21)) >> 21; }
static inline uint32_t key_of(int x, int y) { return ((uint32_t)(y & 0xfff) << 12) | (uint32_t)(x & 0xfff); }
// a triangle's hash, from its corners' keys in order
static inline uint32_t tri_hash(uint32_t a, uint32_t b, uint32_t c)
{
	uint32_t h = a * 0x9e3779b1u ^ b * 0x85ebca6bu ^ c * 0xc2b2ae35u;
	h ^= h >> 15;
	h *= 0x2c1b3c6du;
	return h ^ (h >> 12);
}
// each corner's place in key order (ties: by place in the triangle)
static inline void rank3(const uint32_t k[3], int r[3])
{
	r[0] = (k[0] > k[1]) + (k[0] > k[2]);
	r[1] = (k[1] >= k[0]) + (k[1] > k[2]);
	r[2] = (k[2] >= k[0]) + (k[2] >= k[1]);
}
// vertex entry flags
#define E_VALID 1u
#define E_VETO  2u    // the GTE depth at this pixel is another object's
#define E_EST   4u    // a depth estimated from a polygon's other corners
#define E_LEFT  8u    // on the drawing area's left edge
#define E_RIGHT 16u   // on its right edge
#define E_FAR   32u   // a backdrop's: at the back
static inline unsigned age(const vent *e, uint16_t now) { return e->f & E_VALID ? (uint8_t)(now - (e->kg >> 24)) : 0xffff; }

GS_COLD bool gs_core_alloc(void)
{
	if (!ttab)
		ttab = calloc(1u << TT_BITS, sizeof(*ttab));
	if (!vtab)
		vtab = calloc(1u << VT_BITS, sizeof(*vtab));
	if (!fifo)
		fifo = malloc(FIFO_N * sizeof(*fifo));
	if (!seq)
		seq = malloc(MAXSEQ);
	if (!vshift)
		vshift = calloc(1u << VS_BITS, sizeof(*vshift));
	return ttab && vtab && fifo && seq && vshift;
}

GS_COLD void gs_core_free(void)
{
	free(ttab);
	free(vtab);
	free(fifo);
	free(seq);
	free(vshift);
	ttab = NULL;
	vtab = NULL;
	fifo = NULL;
	fifo_head = fifo_tail = 0;
	seq = NULL;
	vshift = NULL;
	strength = near_z = 0;
}

GS_COLD void gs_core_clear(void)
{
	if (ttab)
		memset(ttab, 0, (1u << TT_BITS) * sizeof(*ttab));
	if (vtab)
		memset(vtab, 0, (1u << VT_BITS) * sizeof(*vtab));
	fifo_head = fifo_tail = 0;   // (the GPU thread is idle)
	if (vshift)
		memset(vshift, 0, (1u << VS_BITS) * sizeof(*vshift));
	near_z = aim_n = 0;
	memset(hist, 0, sizeof(hist));
	samples = nseq = bd_count = bd_hold = 0;
	flat_run = FLAT_AFTER;
	flat_step = FADE;
	flat = 1;
	memset(pass, 0, sizeof(pass));
	pass_gen = 1;
}

// A vertex on the drawing area's left edge doesn't move right in the right
// eye (on the right edge: left): the scene still reaches the picture's
// edges (an edge moved inwards would leave a strip the right eye never
// draws). Moved outwards, the GPU's scissor cuts it at the edge.
static unsigned put_flags;   // E_FAR while a backdrop polygon is captured
static inline unsigned zint(float z)
{
	int zi = z > 0 ? gs_rint(z) : 0;
	return z <= 0 ? 0 : zi < 1 ? 1 : zi > 65535 ? 65535 : zi;
}

// A vertex's depth (z 0: none). keep: an entry of this frame is not replaced
// (all triangles drawn see the same one), except an estimate by a vertex's
// own depth.
static void vput(int x, int y, float z, unsigned edge, bool keep)
{
	uint32_t key = key_of(x, y);
	vent *b = &vtab[((key * 2654435761u) >> (32 - VT_BITS)) & ~(VT_WAYS - 1u)], *victim = b;
	unsigned oldest = 0;
	// the same position again, else the way written longest ago
	for (unsigned w = 0; w < VT_WAYS; w++) {
		vent *e = &b[w];
		if ((e->f & E_VALID) && (e->kg & 0xffffff) == key) {
			if (keep && age(e, gen) == 0 && (!(e->f & E_EST) || (edge & E_EST)) && !(edge & E_VETO))
				return;
			if (age(e, gen) == 0 && (e->f & E_VETO) && !(edge & E_VETO))
				return;   // a veto stays for the frame
			victim = e;
			break;
		}
		unsigned a = age(e, gen);
		if (a > oldest)
			oldest = a, victim = e;
	}
	vent n = { { key | (uint32_t)(gen & 0xff) << 24, (uint16_t)zint(z), (uint16_t)(E_VALID | put_flags | edge) } };
	if (gs_defer)
		*victim = n;
	else   // captured on the main thread: the GPU thread may be reading it
		__atomic_store_n(&victim->u, n.u, __ATOMIC_RELAXED);
}

// the vertex entry at the position (0: none)
static const vent *vget(int x, int y, uint16_t now)
{
	uint32_t key = key_of(x, y);
	const vent *e = &vtab[((key * 2654435761u) >> (32 - VT_BITS)) & ~(VT_WAYS - 1u)];
	for (unsigned w = 0; w < VT_WAYS; w++, e++)
		if ((e->kg & 0xffffff) == key && age(e, now) <= 1)
			return e;
	return NULL;
}

// one triangle as the renderer draws it: its corners' depths (z 0: none)
// and flags; est: a 2D polygon's
static float put_est;
static void put_tri(const int (*c)[2], const float *z, const unsigned *edge)
{
	uint32_t k[3], sk[3];
	int r[3];
	for (int i = 0; i < 3; i++)
		k[i] = key_of(c[i][0], c[i][1]);
	rank3(k, r);
	for (int i = 0; i < 3; i++)
		sk[r[i]] = k[i];
	uint32_t h = tri_hash(sk[0], sk[1], sk[2]);
	tent *b = &ttab[(h >> (32 - TT_BITS)) & ~(TT_WAYS - 1u)], *victim = NULL;
	// the same triangle again, else the way written longest ago
	UNROLL for (unsigned w = 0; w < TT_WAYS; w++)
		if (b[w].h == h && (b[w].k0 & 0xffffff) == sk[0] && (b[w].f & T_VALID)) {
			victim = &b[w];
			break;
		}
	if (!victim) {
		unsigned oldest = 0;
		victim = b;
		UNROLL for (unsigned w = 0; w < TT_WAYS; w++) {
			unsigned a = b[w].f & T_VALID ? (uint8_t)(gen - (b[w].k0 >> 24)) : 0xffff;
			if (a > oldest)
				oldest = a, victim = &b[w];
		}
	}
	tent n = { h, sk[0] | (uint32_t)(gen & 0xff) << 24, { 0, 0, 0 }, T_VALID | (put_est > 0 ? 0 : T_EST) };
	UNROLL for (int i = 0; i < 3; i++) {
		n.z[r[i]] = zint(z[i]);
		n.f |= ((edge[i] | put_flags) >> 3 & 7) << (3 * r[i]);   // E_LEFT, E_RIGHT, E_FAR: T_L, T_R, T_FAR
	}
	if (gs_defer) {
		*victim = n;
		return;
	}
	// Captured on the main thread while the GPU thread may read this entry:
	// invalid while it changes, then valid with a new count (gs_shift reads
	// the flags before and after the rest).
	unsigned count = ((victim->f >> 10) + 1) & 31;
	__atomic_store_n(&victim->f, 0, __ATOMIC_RELAXED);
	__atomic_thread_fence(__ATOMIC_RELEASE);
	victim->h = n.h;
	victim->k0 = n.k0;
	memcpy(victim->z, n.z, sizeof(n.z));
	__atomic_store_n(&victim->f, n.f | count << 10, __ATOMIC_RELEASE);
}

// a corner's vertex depth: its own, else the polygon's estimate (none for a 2D polygon)
static inline void put_vertex(int x, int y, float z, unsigned edge)
{
	if (z > 0)
		vput(x, y, z, edge, true);
	else if (put_est > 0)
		vput(x, y, put_est, edge | E_EST, true);
}

// --- capture: the same triangles and clipping the renderer makes ----------
typedef struct { int x, y; } pt;
static float tri_a[3][3];   // triangle corners: x, y, 1/z
static bool tri_known[3];   // the corner's own depth is known (not the polygon's mean)
static unsigned tri_far[3]; // E_FAR: the corner is on a backdrop's vertex

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

// gpu_pica's clip_area, positions only (its rounding: lrintf), and its fan
static void clip_put(const pt *t, int x0, int y0, int x1, int y1, bool twod)
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
				o->x = p->x + gs_rint((q->x - p->x) * f);
				o->y = p->y + gs_rint((q->y - p->y) * f);
				if (side < 2) o->x = side == 0 ? x0 : x1;
				else o->y = side == 2 ? y0 : y1;
			}
		}
		if (m < 3)
			return;
		memcpy(a, b, m * sizeof(pt));
		n = m;
	}
	float z[9];
	unsigned edge[9];
	for (int i = 0; i < n; i++) {
		bool corner_unknown = twod;
		for (int k = 0; k < 3; k++)
			corner_unknown |= !tri_known[k] && a[i].x == t[k].x && a[i].y == t[k].y;
		z[i] = corner_unknown ? 0 : depth_at(a[i].x, a[i].y);
		edge[i] = a[i].x == x0 ? E_LEFT : a[i].x == x1 ? E_RIGHT : 0;
		for (int k = 0; k < 3; k++)
			if (a[i].x == t[k].x && a[i].y == t[k].y)
				edge[i] |= tri_far[k];
	}
	for (int i = 1; i + 1 < n; i++) {
		int c[3][2] = { { a[0].x, a[0].y }, { a[i].x, a[i].y }, { a[i + 1].x, a[i + 1].y } };
		float tz[3] = { z[0], z[i], z[i + 1] };
		unsigned te[3] = { edge[0], edge[i], edge[i + 1] };
		put_tri(c, tz, te);
	}
	for (int i = 0; i < n; i++)   // (each fan corner once)
		put_vertex(a[i].x, a[i].y, z[i], edge[i]);
}

static void capture(const gs_rec *r);

// The main thread hands one polygon command (0x20-0x3f) over: its corners
// with the drawing offset (E5), their GTE depths (0 = unknown), the drawing
// area (E3, E4). With gs_defer, it is queued for gs_drain (the GPU thread
// captures it before it draws it); else captured now.
void gs_capture(const uint32_t *w, uint32_t cmd, uint32_t e3, uint32_t e4, uint32_t e5, const uint16_t z[4])
{
	if (!ttab || cmd < 0x20 || cmd >= 0x40 || (gs_debug & 1))
		return;
	gs_rec now, *r = &now;
	unsigned head = fifo_head;
	if (gs_defer) {
		if (head - __atomic_load_n(&fifo_tail, __ATOMIC_ACQUIRE) >= FIFO_N) {
			n_dropped++;   // (more polygons than a frame has)
			return;
		}
		r = &fifo[head % FIFO_N];
	}
	int n = (cmd & 8) ? 4 : 3, ox = sext11(e5), oy = sext11(e5 >> 11), i = 1;
	for (int k = 0; k < n; k++) {
		if (k && (cmd & 0x10))
			i++;   // gouraud colour
		uint32_t xy = w[i++];
		if (cmd & 4)
			i++;   // texture coordinates
		r->x[k] = sext11(xy) + ox;
		r->y[k] = sext11(xy >> 16) + oy;
		r->z[k] = z[k];
	}
	r->e3 = (e3 & 0x7ffff) | cmd << 24;
	r->e4 = e4 & 0x7ffff;
	if (gs_defer)
		__atomic_store_n(&fifo_head, head + 1, __ATOMIC_RELEASE);
	else
		capture(r);
}

// the queued polygons, in order (on the thread that shifts)
void gs_drain(void)
{
	unsigned t = fifo_tail, h = __atomic_load_n(&fifo_head, __ATOMIC_ACQUIRE);
	if (t == h)
		return;
	uint64_t t0 = gs_clock ? gs_clock() : 0;
	for (; t != h; t++)
		capture(&fifo[t % FIFO_N]);
	__atomic_store_n(&fifo_tail, t, __ATOMIC_RELEASE);
	if (gs_clock)
		cap_ticks += gs_clock() - t0;
}

// a corner pair's disparity jumps more than the eyes could fuse along their edge
static inline bool jump(float da, float db, int ax, int ay, int bx, int by)
{
	return fabsf(da - db) * scale > MAX_GRADIENT * (abs(ax - bx) + abs(ay - by)) + SLACK;
}

// One polygon of n (3 or 4: a constant in each copy) corners.
static inline __attribute__((always_inline)) void capture_n(const gs_rec *r, const int n)
{
	static const int idx[2][3] = { { 0, 1, 2 }, { 1, 3, 2 } };
	int found = 0;
	pt v[4];
	float iz[4];
	unsigned zk[4], vz[4] = { 0 }, far[4] = { 0 };
	bool joined = false;   // a corner is a vertex of 3D geometry drawn before (not a backdrop's)
	UNROLL for (int k = 0; k < n; k++) {
		v[k].x = r->x[k];
		v[k].y = r->y[k];
		zk[k] = r->z[k];
	}
	int minx = v[0].x, maxx = minx, miny = v[0].y, maxy = miny;
	UNROLL for (int k = 1; k < n; k++) {
		minx = v[k].x < minx ? v[k].x : minx; maxx = v[k].x > maxx ? v[k].x : maxx;
		miny = v[k].y < miny ? v[k].y : miny; maxy = v[k].y > maxy ? v[k].y : maxy;
	}
	n_lookups += n;
	UNROLL for (int k = 0; k < n; k++)
		n_found += zk[k] != 0;
	// What this frame recorded at the corners' pixels (3D polygons drawn
	// before): a vetoed GTE depth (another object's, see below) is no
	// corner's here; a backdrop's vertex keeps the corner at the back with
	// it; a vertex depth (known or estimated) is what a corner without one
	// of its own is interpolated with (clip points), alike in every polygon.
	int avail = 0;
	UNROLL for (int k = 0; k < n; k++) {
		const vent *e = vget(v[k].x, v[k].y, gen);
		if (e && age(e, gen) == 0) {
			if (e->f & E_VETO)
				zk[k] = 0;
			if ((e->f & E_FAR) && !(e->f & E_EST))
				far[k] = E_FAR;
			joined |= !(e->f & (E_FAR | E_EST));
			vz[k] = e->z;
		}
		avail += zk[k] || vz[k];
		found += zk[k] != 0;
	}
	// Two known corners at least: a 2D polygon can share one position
	// with a projected point by chance. 2D polygons are entered too (no
	// depth): the GPU thread then knows them for what they are. A polygon
	// the GTE record missed (fewer than two depths) whose corners, two at
	// least, are 3D vertices of this frame is part of that mesh: it takes
	// their depths.
	if (found < 2 && avail >= 2)
		UNROLL for (int k = 0; k < n; k++)
			if (!zk[k] && vz[k]) {
				zk[k] = vz[k];
				found++;
			}
	bool twod = found < 2;
	float inv = 0, rz[4];   // rz: the corners' 1/z (a vetoed corner's stays unused)
	UNROLL for (int k = 0; k < n; k++)
		if (zk[k])
			inv += rz[k] = 1.0f / zk[k];
	inv = twod ? 1 : inv / found;
	// The GTE depths come by screen position: a corner can get the depth of
	// another object projected to the same pixel. A corner whose disparity
	// jumps against both other corners of a triangle (more than the eyes
	// could fuse along that edge) has no depth of its own here, and as the
	// same wrong depth reaches every polygon on that pixel, none has it
	// this frame (a veto); they all get the vertex's depth from elsewhere.
	if (!twod && near_z > 0) {
		float dk[4];   // each corner's disparity (the same for every triangle it is in)
		UNROLL for (int k = 0; k < n; k++)
			dk[k] = zk[k] ? disp(zk[k], near_z) : 0;
		bool j12 = false, have12 = false;   // the edge 1-2 both of a quad's triangles have (once)
		UNROLL for (int t = 0; t < n - 2; t++) {
			const int *q = idx[t];
			if (!zk[q[0]] || !zk[q[1]] || !zk[q[2]])
				continue;
			// each edge's jump once: jp[c] is the edge opposite corner c
			bool jp[3];
			jp[0] = jump(dk[q[1]], dk[q[2]], v[q[1]].x, v[q[1]].y, v[q[2]].x, v[q[2]].y);
			jp[1] = t == 1 && have12 ? j12 : jump(dk[q[2]], dk[q[0]], v[q[2]].x, v[q[2]].y, v[q[0]].x, v[q[0]].y);
			jp[2] = jump(dk[q[0]], dk[q[1]], v[q[0]].x, v[q[0]].y, v[q[1]].x, v[q[1]].y);
			if (n == 4 && t == 0) {
				j12 = jp[0];
				have12 = true;
			}
			UNROLL for (int c = 0; c < 3; c++) {
				int a = q[c], b = q[(c + 1) % 3], o = q[(c + 2) % 3];
				if (!zk[a] || !zk[b] || !zk[o])
					continue;
				bool ab = jp[(c + 2) % 3], ao = jp[(c + 1) % 3], bo = jp[c];
				if (ab && ao && !bo) {
					vput(v[a].x, v[a].y, 2 / (rz[b] + rz[o]), E_EST | E_VETO, false);
					zk[a] = 0;
					n_rejected++;
				}
			}
		}
	}
	if (!twod) {   // the mean of what is left (the vertex estimate's base)
		float left = 0;
		int nl = 0;
		UNROLL for (int k = 0; k < n; k++)
			if (zk[k]) { left += rz[k]; nl++; }
		if (nl)
			inv = left / nl;
	}
	put_est = twod ? 0 : 1 / inv;
	UNROLL for (int k = 0; k < n; k++)
		iz[k] = zk[k] ? rz[k] : vz[k] ? 1.0f / vz[k] : inv;
	int x0 = r->e3 & 0x3ff, y0 = (r->e3 >> 10) & 0x1ff;
	int x1 = (r->e4 & 0x3ff) + 1, y1 = ((r->e4 >> 10) & 0x1ff) + 1;
	put_flags = 0;
	// The scene's depth range and the draw order count only what is drawn:
	// geometry outside the drawing area (behind or beside the camera) would
	// pull the near depth in and push the visible scene back.
	if (!twod && maxx > x0 && minx < x1 && maxy > y0 && miny < y1) {
		union { float f; uint32_t u; } bits = { .f = 1 / inv };
		unsigned b = (bits.u - 0x3f800000) >> 19;
		b = b < HBINS ? b : HBINS - 1;
		// (the count runs a little past the backdrop: geometry joined to
		// what was drawn before it isn't the backdrop)
		if (bd_count && nseq < bd_count && b < bd_bin && !joined) {
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
	// the polygon inside the drawing area: no corner outside it, no
	// triangle clipped or wrapped (all in 0..1023)
	bool inside = minx >= x0 && maxx <= x1 && miny >= y0 && maxy <= y1;
	// the vertex depth of corners just outside the drawing area too: a
	// neighbour clipped along the same edge interpolates the same point
	// (far outside, huge polygons' corners pile up on few pixels)
	if (!inside)
		UNROLL for (int k = 0; !twod && k < n; k++)
			if ((v[k].x < x0 || v[k].x > x1 || v[k].y < y0 || v[k].y > y1) &&
			    v[k].x > x0 - 64 && v[k].x < x1 + 64 && v[k].y > y0 - 64 && v[k].y < y1 + 64)
				vput(v[k].x, v[k].y, 1 / iz[k], E_EST, true);   // an estimate: no backdrop or joining by it
	unsigned edge[4];
	float zf[4];
	UNROLL for (int k = 0; k < n; k++) {
		edge[k] = (v[k].x == x0 ? E_LEFT : v[k].x == x1 ? E_RIGHT : 0) | far[k];
		zf[k] = !twod ? zk[k] : 0;   // (= 1 / iz, rounded alike)
	}
	unsigned vdone = 0;   // corners whose vertex depth is in (a quad's two triangles share two)
	UNROLL for (int t = 0; t < n - 2; t++) {
		const int *q = idx[t];
		pt tri[3] = { v[q[0]], v[q[1]], v[q[2]] };
		int lx = tri[0].x, hx = lx, ly = tri[0].y, hy = ly;
		UNROLL for (int k = 1; k < 3; k++) {
			lx = tri[k].x < lx ? tri[k].x : lx; hx = tri[k].x > hx ? tri[k].x : hx;
			ly = tri[k].y < ly ? tri[k].y : ly; hy = tri[k].y > hy ? tri[k].y : hy;
		}
		if (hx - lx > 1023 || hy - ly > 511)
			continue;   // the PS1 drops it
		if (inside) {
			if (hx <= x0 || lx >= x1 || hy <= y0 || ly >= y1)
				continue;
			int c[3][2] = { { tri[0].x, tri[0].y }, { tri[1].x, tri[1].y }, { tri[2].x, tri[2].y } };
			float tz[3] = { zf[q[0]], zf[q[1]], zf[q[2]] };
			unsigned te[3] = { edge[q[0]], edge[q[1]], edge[q[2]] };
			put_tri(c, tz, te);
			UNROLL for (int k = 0; k < 3; k++)
				if (!(vdone >> q[k] & 1)) {
					put_vertex(c[k][0], c[k][1], tz[k], te[k]);
					vdone |= 1u << q[k];
				}
			continue;
		}
		// draw_poly_slow: one wrap for the whole triangle
		int dx = sext11(lx) - lx, dy = sext11(ly) - ly;
		UNROLL for (int k = 0; k < 3; k++) {
			tri[k].x += dx; tri[k].y += dy;
		}
		lx += dx; hx += dx; ly += dy; hy += dy;
		if (hx <= x0 || lx >= x1 || hy <= y0 || ly >= y1)
			continue;
		if (lx >= x0 && hx <= x1 && ly >= y0 && hy <= y1) {
			int c[3][2];
			float tz[3];
			unsigned te[3];
			UNROLL for (int k = 0; k < 3; k++) {
				c[k][0] = tri[k].x; c[k][1] = tri[k].y;
				tz[k] = zf[q[k]];
				te[k] = (tri[k].x == x0 ? E_LEFT : tri[k].x == x1 ? E_RIGHT : 0) | far[q[k]];
			}
			put_tri(c, tz, te);
			UNROLL for (int k = 0; k < 3; k++) {
				if (!(dx | dy) && (vdone >> q[k] & 1))
					continue;
				put_vertex(c[k][0], c[k][1], tz[k], te[k]);
				vdone |= !(dx | dy) << q[k];
			}
		} else {
			UNROLL for (int k = 0; k < 3; k++) {
				tri_a[k][0] = tri[k].x; tri_a[k][1] = tri[k].y; tri_a[k][2] = iz[q[k]];
				tri_known[k] = !twod && zk[q[k]] != 0;
				tri_far[k] = far[q[k]];
			}
			clip_put(tri, x0, y0, x1, y1, twod);
		}
	}
}

static void capture(const gs_rec *r)
{
	if (r->e3 >> 24 & 8)
		capture_n(r, 4);
	else
		capture_n(r, 3);
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
	gs_drain();   // what the GPU thread didn't capture yet (it is idle now)
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

GS_COLD void gs_core_queue(unsigned *dropped, uint64_t *ticks)
{
	*dropped = n_dropped;
	*ticks = cap_ticks;
	n_dropped = 0;
	cap_ticks = 0;
}

GS_COLD void gs_core_counts(unsigned *lookups, unsigned *found, unsigned *shifted, unsigned *rejected)
{
	*lookups = n_lookups; *found = n_found; *shifted = n_shifted; *rejected = n_rejected;
	n_lookups = n_found = n_shifted = n_rejected = 0;
}

// --- passes (GPU thread): what a buffer drew since it was last shown --------
// A pass into a buffer starts with its first draw after the frontend last
// showed it (double and single buffering, games drawing over two frames).
// 2D primitives drawn in a pass before its first polygon with depth are
// the scene's backdrop; drawn after it, HUD, text and logos.

GS_COLD void gs_core_pass_shown(int x, int y, int w, int h)
{
	struct pass *s = NULL, *old = pass;
	for (int i = 0; i < PASS_SLOTS; i++) {
		struct pass *t = &pass[i];
		if (t->live && t->x == x && t->y == y)
			s = t;
		if (!t->live || (old->live && t->shown < old->shown))
			old = t;
	}
	if (!s) {
		s = old;
		memset(s, 0, sizeof(*s));
		s->x = x; s->y = y; s->live = true;
	}
	s->w = w; s->h = h;
	s->shown = pass_gen++;
}

// the pass a primitive at VRAM pixel (x, y) belongs to
static struct pass *pass_at(int x, int y)
{
	struct pass *best = &pass[PASS_SLOTS];
	for (int i = 0; i < PASS_SLOTS; i++) {
		struct pass *s = &pass[i];
		if (s->live && ((x - s->x) & 1023) < s->w && ((y - s->y) & 511) < s->h && (best == &pass[PASS_SLOTS] || s->shown > best->shown))
			best = s;
	}
	if (best->shown >= best->start) {   // a new pass into this buffer
		best->had = best->depth >= 16;
		best->depth = 0;
		best->start = pass_gen;
	}
	return best;
}

// the shift of the vertex at VRAM pixel (x, y) this frame: the one already
// decided there if within SNAP of want or a backdrop's, else want (decided
// now if first)
static int vertex_shift(uint32_t key, int want, bool far, uint16_t now)
{
	vsent *b = &vshift[((key * 2654435761u) >> (32 - VS_BITS)) & ~(VS_WAYS - 1u)], *victim = b;
	UNROLL for (unsigned w = 0; w < VS_WAYS; w++) {
		const vsent *e = &b[w];
		if ((e->kg & 0xffffff) == key && e->f && (uint8_t)(now - (e->kg >> 24)) <= 1) {
			int have = e->shift;
			return abs(have - want) <= SNAP || (e->f & 2) ? have : want;
		}
	}
	unsigned oldest = 0;
	UNROLL for (unsigned w = 0; w < VS_WAYS; w++) {
		unsigned a = b[w].f ? (uint8_t)(now - (b[w].kg >> 24)) : 0x100;
		if (a > oldest)
			oldest = a, victim = &b[w];
	}
	victim->kg = key | (uint32_t)(uint8_t)now << 24;
	victim->shift = want;
	victim->f = 1 | far << 1;
	return want;
}

// Polygon vertices sit at pixel * 16 + (8, 7) (POLY_SHIFT_X/Y in gpu_pica);
// sprites, rectangles, lines and uploads don't. A triangle of a polygon
// with depth moves by its corners' depths; 2D primitives move as a whole,
// by whole pixels (they stay sharp): to the back when they are the backdrop
// of a pass with depth, else not at all (on the screen, as HUD and text).
int gs_shift(const gs_vtx *src, gs_vtx *dst, int n)
{
	gs_drain();   // the polygons queued so far: these triangles' among them
	float zn = near_z, k = 16 * scale * strength;
	uint16_t now = gen;
	int moved = 0;
	memcpy(dst, src, n * sizeof(*dst));
	if (!ttab || zn <= 0 || k <= 0 || (gs_debug & 2))
		return 0;
	for (int t = 0; t + 3 <= n; t += 3) {
		const gs_vtx *v = &src[t];
		float d[3];
		unsigned edge[3];   // T_L, T_R
		bool known[3], far[3] = { false };
		int found = 0, entries = 0;
		bool poly = true;
		int px[3], py[3];
		uint32_t key[3];
		UNROLL for (int i = 0; i < 3; i++) {
			poly &= (v[i].x & 15) == 8 && (v[i].y & 15) == 7;
			px[i] = v[i].x >> 4;
			py[i] = v[i].y >> 4;
			key[i] = key_of(px[i], py[i]);
		}
		// each corner's own depth and flags (the triangle's entry); without
		// a depth of its own, the vertex's (another triangle's, by position).
		// A 2D polygon's corners (T_EST) borrow only when two at least are 3D
		// polygons' vertices of this frame (known or estimated depth): then
		// it is part of a 3D mesh whose GTE record missed it; a 2D logo over
		// the scene shares a pixel with a 3D vertex at most by chance.
		if (poly) {
			uint32_t sk[3];
			int r[3];
			rank3(key, r);
			for (int i = 0; i < 3; i++)
				sk[r[i]] = key[i];
			uint32_t h = tri_hash(sk[0], sk[1], sk[2]);
			const tent *b = &ttab[(h >> (32 - TT_BITS)) & ~(TT_WAYS - 1u)], *e = NULL;
			UNROLL for (unsigned w = 0; w < TT_WAYS; w++)
				if (b[w].h == h && (b[w].k0 & 0xffffff) == sk[0] && (b[w].f & T_VALID) && (uint8_t)(now - (b[w].k0 >> 24)) <= 1) {
					e = &b[w];
					break;
				}
			tent copy;
			if (e && !gs_defer) {
				// the main thread may be writing it (see put_tri): a copy whose
				// flags were the same before and after
				uint16_t f1 = __atomic_load_n(&e->f, __ATOMIC_ACQUIRE);
				copy = *e;
				__atomic_thread_fence(__ATOMIC_ACQUIRE);
				bool same = (f1 & T_VALID) && __atomic_load_n(&e->f, __ATOMIC_RELAXED) == f1 && copy.h == h &&
					(copy.k0 & 0xffffff) == sk[0] && (uint8_t)(now - (copy.k0 >> 24)) <= 1;
				copy.f = f1;
				e = same ? &copy : NULL;
			}
			if (e) {
				unsigned z[3], fl[3], mesh = 0;
				vent vert[3] = { { { 0, 0, 0 } }, { { 0, 0, 0 } }, { { 0, 0, 0 } } };
				entries = 3;
				for (int i = 0; i < 3; i++) {
					z[i] = e->z[r[i]];
					fl[i] = e->f >> (3 * r[i]);
					if (!z[i]) {
						const vent *ve = vget(px[i], py[i], now);
						if (ve)   // (as one load: the main thread may be writing it)
							vert[i].u = __atomic_load_n(&ve->u, __ATOMIC_RELAXED);
						mesh += ve != NULL;   // 3D polygons' vertices (estimates too) only
					}
				}
				bool borrow = !(e->f & T_EST) || mesh >= 2;
				for (int i = 0; i < 3; i++) {
					bool f = fl[i] & T_FAR;
					if (!z[i] && borrow) {
						z[i] = vert[i].z;
						f = vert[i].f & E_FAR;
					}
					edge[i] = fl[i] & (T_L | T_R);
					known[i] = z[i] != 0;
					far[i] = known[i] && f;
					// disparity with the slider fully up, in units of MAX_BEHIND
					d[i] = !known[i] ? 0 : f ? 1 : disp((float)z[i], zn);
					d[i] = d[i] < -MAX_FRONT ? -MAX_FRONT : d[i];
					found += known[i];
				}
			}
		}
#ifdef PSX3D_SIM
		gs_missed += poly && !entries;
#else
		(void)entries;
#endif
		// (by its centre: a corner can sit on the buffer's edge)
		struct pass *ps = pass_at(((v[0].x + v[1].x + v[2].x) / 3) >> 4, ((v[0].y + v[1].y + v[2].y) / 3) >> 4);
		if (found) {
			float mean = 0;
			if (found < 3) {
				for (int i = 0; i < 3; i++)
					mean += known[i] ? d[i] : 0;
				mean /= found;
			}
			for (int i = 0; i < 3; i++) {
				float dd = known[i] ? d[i] : mean;
				if ((edge[i] & T_L && dd > 0) || (edge[i] & T_R && dd < 0))
					dd = 0;
				int x = dst[t + i].x + vertex_shift(key[i], gs_rint(k * dd), far[i], now);
				dst[t + i].x = x < 0 ? 0 : x > 1024 * 16 - 1 ? 1024 * 16 - 1 : x;
				moved++;
			}
			ps->depth++;
			continue;
		}
		// 2D: CPU fills (no vertex colour, untextured) stay put
		if (!ps->had || ps->depth || ((v[0].rgba >> 24) == 0 && v[0].u == v[1].u && v[1].u == v[2].u && v[0].v == v[1].v))
			continue;
		int whole = 16 * gs_rint(k * (1 / 16.0f));
		for (int i = 0; i < 3; i++) {
			if (ps != &pass[PASS_SLOTS] && px[i] == ps->x)
				continue;   // the buffer's left edge stays put
			int x = v[i].x + whole;
			dst[t + i].x = x > 1024 * 16 - 1 ? 1024 * 16 - 1 : x;
			moved++;
		}
	}
	n_shifted += moved;
	return moved;
}
