// Real 3D: draw-order layers for flat scenes (New 3DS only; prototype).
// Platform independent (also built into the PC replay harness).
//
// Linked after every other frontend object, and hooked in with --wrap
// (gs_shift, gs_core_frame/alloc/clear/free, gpustereo_status and
// pica_picture), so no existing code moves: the Old 3DS, which never turns
// 3D on, runs exactly the code it ran before.
#include "gpustereo.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GS_COLD __attribute__((cold))
// cells: 4x4 VRAM pixels each
#define LC 4
#define LC_W (1024 / LC)
#define LC_H (512 / LC)
static uint64_t *lcell;
#ifdef PSX3D_SIM
unsigned long long lay_cells, lay_prims, lay_tris;   // work counters (simulator)
#define LAY_COUNT(v, n) (v += (n))
#else
#define LAY_COUNT(v, n) ((void)0)
#endif
static float lstrength;              // the 3D slider (gs_core_frame)
static int lhres = 320;              // the picture's width in PS1 pixels

// --- Draw-order layers ------------------------------------------------------
// PS1 games paint back to front (no depth buffer), so the order of drawing
// is a depth map of its own. Frames without GTE depth (2D games, menus,
// logos) get layered 3D from it; frames with it keep Real 3D (the blend
// between the two follows the share of polygons with a GTE depth).
//
// Everything here runs on the GPU thread, in draw order, from the vertices
// gpu_pica hands to citro3d. Per pass into a buffer (a displayed
// rectangle, from the first draw after it was last shown):
// - a primitive (a triangle, or the two halves of a quad) gets
//   a level: one above the highest level it covers of another segment
//   (a run of primitives of one kind from one 256x256 part of the texture
//   atlas: text, a tile map, a sprite's parts; they don't step over each
//   other), never below the level reached so far in the pass (what comes
//   later is in front);
// - a big primitive (half the picture) at the start is the base; later
//   ones (fades, flashes, fog, light) take no step of their own;
// - at the end of the pass each visible level gets a depth from what is
//   visible: area (big background layers spread out, small sprites and HUD
//   stay close to the screen) and rank (every level a step); the topmost
//   level is the screen surface. The next pass uses that table, smoothed.
#ifndef LAY_MAX
#define LAY_MAX     14.0f  // px of a 320-wide picture: the deepest layer (Real 3D's back: 10)
#endif
#define LAY_FLAT    0.5f   // a picture of one layer (FMV, a still): this deep
#ifndef LAY_AREA
#define LAY_AREA    0.4f   // area vs rank in a level's depth (rank: even steps between layers)
#endif
#define LAY_SMOOTH  0.15f  // per pass
#ifndef LAY_SETTLE
#define LAY_SETTLE  45     // passes a level's new depth must hold, unchanged, before it is shown
#endif
#define LAY_WANT    30     // frames screen depth (or layers) must be asked for before the fade starts
#define LAY_MINOR   15     // per mille of the picture: a level this small is no step of its own
#define LAY_SLOTS   4
#ifndef LAY_FLOOR
#define LAY_FLOOR   1      // the level never drops in draw order
#endif
#define LAY_LEVELS  256
#define LAY_SEGS    2048   // segment levels kept for the next pass (hash table)
// A scene whose layers don't hold still (pieces that stay where they are
// but change depth from pass to pass: 2D fighters' backdrops as the
// fighters animate) gets screen depth instead, until it has been steady a while.
#ifndef LAY_UNSTABLE
#define LAY_UNSTABLE 0.3f  // share of recent pictures (about 40) that were unsteady
#endif
#ifndef LAY_JUMPY
#define LAY_JUMPY   0.08f  // a picture is unsteady: this share of the steady pieces' area changed depth
#endif
#ifndef LAY_HOLD
#define LAY_HOLD    300    // pictures: screen depth this long after the last unsteady one
#endif
// a cell: the level shown, the level by the rules alone (see lay_tri),
// the segment and the pass that drew it last
#define C_H(c)   ((unsigned)(c) & 0xff)
#define C_N(c)   ((unsigned)((c) >> 8) & 0xff)
#define C_SEG(c) ((unsigned)((c) >> 16) & 0xfff)
#define C_PID(c) ((unsigned)((c) >> 32))
typedef struct {
	int x, y, w, h;
	unsigned shown, start, pid;   // gen last shown; gen of the pass's first draw; pass id
	int cur, top;                 // levels reached: by the rules alone (cur), shown (top)
	int scur;                     // shown level reached
	int edge;                     // drawing outside shown buffers: the last base's left edge
	unsigned tris, moved;         // this pass: primitives drawn, those with a depth
	unsigned lastseg, segno;      // segments in this pass, numbered in draw order
	int segmax, ssegmax;          // the current segment's highest level: by the rules, shown
	bool used, live, flat;        // flat: the last pass had (nearly) nothing to separate
	float map[LAY_LEVELS];
} lslot;
static lslot *slots;                 // LAY_SLOTS + 1, the last: drawing outside every shown buffer
static unsigned lgen, lpass, lseg;   // GPU thread: frames shown, passes, segments
static int lkind = -1, lku, lkv;    // the current segment's kind and texture position
static struct {                      // the primitive being drawn
	bool on;
	int16_t pos[3][2];
	int ntri;                      // its triangles so far
	float share, step;
	int left, right;
	uint64_t cell;                 // what its triangles write (0: nothing)
} lprev;
// One depth per level for all buffers (they show the same scene); the
// slot "outside every buffer" keeps the starting table.
static float lmap[LAY_LEVELS], lshow[LAY_LEVELS];
static bool lmap_used;
// Each segment's level, kept from pass to pass, found by its kind and
// texture position (stable while a scene pans; draw-order position isn't:
// a segment more or less earlier would hand every later one another's).
// rx, ry, shown: where its first primitive was last pass (in its buffer) and
// the level shown there (-1: none yet)
static struct lsegent { uint32_t key; unsigned pass; uint8_t last, now; int16_t rx, ry, shown; } *lsegs, *lsegcur;   // LAY_SEGS
static float lblend;                 // 0: layers .. 1: Real 3D
static float lwant;                  // where lblend is heading (1: Real 3D)
static float lready;                 // the layers' strength: 0 until frames were seen to be flat, then faded in
static bool lflat;                   // the picture shown has nothing to separate
// Screen depth instead of layers (the fallback of frames without polygon
// depth, gpustereo_core.c): for pictures with nothing to separate (logos,
// text on black, a still) and movies the frontend shows itself. 0: layers
// .. 1: screen depth, faded over LAY_SCREEN_FADE frames.
#define LAY_SCREEN_FADE 12
static float lscreen;
static float lscreen_want;           // where lscreen is heading
static int lasked;                   // frames the other way was asked for (LAY_WANT)
static bool lnotgpu;                 // the last picture shown wasn't drawn by the GPU (a 24-bit movie)
static unsigned ltris, ldepth;       // triangles this frame, those with a GTE depth
static uint8_t lpend[LAY_LEVELS];    // passes a level's new depth has held (LAY_SETTLE)
static float lcand[LAY_LEVELS];      // that new depth
static unsigned lsteady, lmoved;     // since the last picture: area of pieces that stayed put, of those that changed depth
static float lunst;                  // share of recent pictures that were unsteady (LAY_JUMPY)
static int lhold;                    // pictures left on screen depth for an unsteady scene
static int lay_scissor_right(void);  // the draw's right limit, VRAM px (fills: all of VRAM; uploads: none, 1024)

// Until a buffer's first pass has been measured (and for drawing outside
// every buffer shown so far: a static logo drawn once before it is shown)
// each level comes forward by a step from the base.
static void lay_prior(lslot *s)
{
	for (int l = 0; l < LAY_LEVELS; l++)
		s->map[l] = l < 4 ? 1 - l * 0.25f : 0.2f;
}

static void lay_reset(void)
{
	if (!lcell)
		return;
	memset(slots, 0, (LAY_SLOTS + 1) * sizeof(*slots));
	for (int i = 0; i <= LAY_SLOTS; i++)
		lay_prior(&slots[i]);
	slots[LAY_SLOTS].edge = -1;
	for (int l = 0; l < LAY_LEVELS; l++)
		lmap[l] = lshow[l] = slots[0].map[l];
	lmap_used = false;
	if (lcell)
		memset(lcell, 0, LC_W * LC_H * sizeof(*lcell));
	lgen = 1;
	lpass = 0;
	memset(lsegs, 0, LAY_SEGS * sizeof(*lsegs));
	lsegcur = NULL;
	lprev.on = false;
	lkind = -1;
	lblend = 0;
	lready = 0;
	lflat = false;
	lscreen = lscreen_want = 0;
	lasked = 0;
	lnotgpu = false;
	ltris = ldepth = 0;
	memset(lpend, 0, sizeof(lpend));
	lsteady = lmoved = 0;
	lunst = 0;
	lhold = 0;
}

static lslot *lay_slot(int cx, int cy)
{
	lslot *best = NULL;
	for (int i = 0; i < LAY_SLOTS; i++) {
		lslot *s = &slots[i];
		if (s->live && lgen - s->shown < 60 && ((cx - s->x) & 1023) < s->w && ((cy - s->y) & 511) < s->h &&
		    (!best || s->shown > best->shown))
			best = s;
	}
	return best ? best : &slots[LAY_SLOTS];
}

// end of a pass: a depth (share of LAY_MAX) per level from what is visible
GS_COLD static void lay_remap(lslot *s)
{
	static uint16_t cnt[LAY_LEVELS];
	float next[LAY_LEVELS];
	memset(cnt, 0, sizeof(cnt));
	unsigned total = 0, nlv = 0;
	for (int y = 0; y < s->h / LC; y++) {
		const uint64_t *row = lcell + ((s->y / LC + y) & (LC_H - 1)) * LC_W;
		for (int x = 0; x < s->w / LC; x++) {
			uint64_t c = row[(s->x / LC + x) & (LC_W - 1)];
			cnt[C_PID(c) == s->pid ? C_H(c) : 0]++;
			total++;
		}
	}
	// Only levels that show a fair part of the picture are steps: the
	// small pieces a fighter or a tile row is made of ride along with the
	// level below them (by their area), so their number doesn't move every
	// other level about.
	unsigned minor = total * LAY_MINOR / 1000;
	int topsig = 0, nsig = 0;
	for (int l = 0; l < LAY_LEVELS; l++)
		if (cnt[l] > minor) { nsig++; topsig = l; }
	unsigned below = 0;
	int rank = -1;
	for (int l = 0; l < LAY_LEVELS; l++) {
		if (cnt[l] > minor)
			rank++;
		float v;
		if (nsig <= 1)
			v = l <= topsig ? LAY_FLAT : 0;   // one picture (an FMV, a still), anything on it on the screen
		else if (l >= topsig)
			v = 0;   // the top layer and what lies on it: the screen surface
		else
			v = LAY_AREA * (1 - (float)below / total) + (1 - LAY_AREA) * (1 - (float)(rank < 0 ? 0 : rank) / (nsig - 1));
		next[l] = v;
		below += cnt[l];
	}
	(void)nlv;
	float a = lmap_used ? LAY_SMOOTH : 1;
	for (int l = 0; l < LAY_LEVELS; l++) {
		lmap[l] += (next[l] - lmap[l]) * a;
		// What is shown moves only for a real change (7% of the depth range,
		// about half a pixel) that then holds still for LAY_SETTLE passes: small
		// drifts would flip whole-pixel shifts, and blinking text or a sprite's
		// level showing in one pass and not the next re-spaces the levels above
		// it, back and forth (the right eye's picture grew and shrank).
		if (a == 1) {
			lshow[l] = lcand[l] = lmap[l];
			lpend[l] = 0;
		} else if (fabsf(lmap[l] - lshow[l]) <= 0.07f) {
			lpend[l] = 0;
		} else if (fabsf(lmap[l] - lcand[l]) > 0.07f || !lpend[l]) {
			lcand[l] = lmap[l];
			lpend[l] = 1;
		} else if (++lpend[l] >= LAY_SETTLE) {
			lshow[l] = lcand[l];
			lpend[l] = 0;
		}
	}
	lmap_used = true;
	s->used = true;
}

// GPU thread: the frontend showed this VRAM rectangle
static bool ldrew;   // a primitive since the last picture shown
GS_COLD void gs_core_shown(int x, int y, int w, int h)
{
	if (w > 0 && h > 0)
		gs_core_pass_shown(x, y, w, h);
	if (!lcell || w <= 0 || h <= 0)
		return;
	lslot *s = NULL, *old = NULL;
	for (int i = 0; i < LAY_SLOTS; i++) {
		lslot *t = &slots[i];
		if (t->live && t->x == x && t->y == y)
			s = t;
		if (!old || (old->live && (!t->live || t->shown < old->shown)))
			old = t;   // a free slot, else the one shown longest ago
	}
	if (!s) {
		s = old;
		memset(s, 0, sizeof(*s));
		lay_prior(s);
		s->x = x; s->y = y; s->live = true;
	}
	s->w = w; s->h = h;
	s->shown = lgen++;
	lflat = s->used && s->flat;
	// steadiness: enough of the picture to judge (2%) stayed put
	if (lsteady * 50 >= (unsigned)(w * h) && lready >= 1) {
		float r = (float)lmoved / lsteady;
#ifdef PSX3D_SIM
		if (getenv("STEREO_LAYDBG"))
			fprintf(stderr, "steady %u moved %.3f\n", lsteady, r);
#endif
		lunst += ((r > LAY_JUMPY ? 1.0f : 0.0f) - lunst) * 0.025f;
		if (lunst > LAY_UNSTABLE)
			lhold = LAY_HOLD;
	}
	if (lhold)
		lhold--;
	lsteady = lmoved = 0;
	// Real 3D where the frame's polygons have GTE depths
	float want = ltris >= 32 && ldepth * 10 >= ltris * 3 ? 1 : ltris >= 32 && ldepth * 10 < ltris ? 0 : lblend;
	lwant = want;
	// The layers start only once frames are known to be flat (fading in);
	// until then, 3D geometry whose depth isn't settled yet would be cut into
	// layers per triangle and crack.
	if (ltris >= 32)
		lready = want < 0.5f ? (lready > 0.9f ? 1 : lready + 0.1f) : 0;
	lblend += (want - lblend) * 0.1f;
	lblend = lblend < 0.01f ? 0 : lblend > 0.99f ? 1 : lblend;
	ltris = ldepth = 0;
}

float gs_layer_blend(void) { return lblend; }

// diagnostics: per buffer, position, pass's top level, flat, the depth of levels 0..5
GS_COLD void gs_layer_status(char *out, int n)
{
	int k = 0;
	out[0] = 0;
	for (int i = 0; slots && i < LAY_SLOTS && k < n; i++) {
		const lslot *s = &slots[i];
		if (!s->live)
			continue;
		k += snprintf(out + k, n - k, "%s[%d,%d,%d,\"%s\",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]", k ? "," : "",
			s->x, s->y, s->top, s->flat ? "flat" : "", lshow[0], lshow[1], lshow[2], lshow[3], lshow[4], lshow[5]);
	}
}
bool gs_picture_flat(void) { return lflat && lblend < 0.5f; }
float gs_layer_unsteady(void) { return lunst; }
int gs_layer_hold(void) { return lhold; }

// the primitive's depth (share of LAY_MAX) and the cells it covers
static int lleft;   // the primitive's buffer's left edge (VRAM px), -1: none
static int lright;  // its right edge (VRAM px), -1: none
static float lstep; // the primitive's depth minus the next level's in front (share)
static float lay_tri(const gs_vtx *v, bool first)
{
	int x0 = v[0].x, x1 = x0, y0 = v[0].y, y1 = y0;
	for (int i = 1; i < 3; i++) {
		x0 = v[i].x < x0 ? v[i].x : x0; x1 = v[i].x > x1 ? v[i].x : x1;
		y0 = v[i].y < y0 ? v[i].y : y0; y1 = v[i].y > y1 ? v[i].y : y1;
	}
	// the pixels it covers (polygons carry +8/16, +7/16), less one at each
	// edge: touching primitives don't overlap
	int px0 = x0 >> 4, px1 = x1 >> 4, py0 = y0 >> 4, py1 = y1 >> 4;
	int cx0 = (px0 + 1) / LC, cx1 = (px1 - 2 > px0 + 1 ? px1 - 2 : px0 + 1) / LC;
	int cy0 = (py0 + 1) / LC, cy1 = (py1 - 2 > py0 + 1 ? py1 - 2 : py0 + 1) / LC;
	cx1 = cx1 < LC_W ? cx1 : LC_W - 1;
	cy1 = cy1 < LC_H ? cy1 : LC_H - 1;
	// the same primitive: the second half of a quad (a triangle sharing an
	// edge with the one before, which was the first). Neighbouring tiles
	// share edges too: chained, they would take one another's level.
	int shared = 0;
	if (lprev.on && !first)
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				shared += v[i].x == lprev.pos[j][0] && v[i].y == lprev.pos[j][1];
	bool joined = shared >= 2 && lprev.ntri < 2;
	lprev.ntri = joined ? lprev.ntri + 1 : 1;
	for (int i = 0; i < 3; i++) {
		lprev.pos[i][0] = v[i].x;
		lprev.pos[i][1] = v[i].y;
	}
	lprev.on = true;
	ldrew = true;
	LAY_COUNT(lay_tris, 1);
	LAY_COUNT(lay_cells, (unsigned)(cx1 - cx0 + 1) * (cy1 - cy0 + 1));
	if (joined) {   // the same primitive: its level, its cells
		if (lprev.cell)
			for (int y = cy0; y <= cy1; y++)
				for (int x = cx0; x <= cx1; x++)
					lcell[y * LC_W + x] = lprev.cell;
		lleft = lprev.left;
		lright = lprev.right;
		lstep = lprev.step;
		return lprev.share;
	}
	lslot *s = lay_slot((px0 + px1) >> 1, (py0 + py1) >> 1);
	lprev.right = lright = s == &slots[LAY_SLOTS] ? -1 : s->x + s->w;
	// CPU uploads and fills (no vertex colour) are not part of the drawing
	// order: they open no pass and mark nothing; they sit at the base, or
	// with what the pass has drawn so far.
	if ((v[0].rgba >> 24) == 0) {
		lprev.cell = 0;
		lprev.left = lleft = s == &slots[LAY_SLOTS] ? s->edge : s->x;
		lprev.share = (s == &slots[LAY_SLOTS] ? s->map : lshow)[s->shown >= s->start ? 0 : s->scur];
		lprev.step = lstep = 0;
		return lprev.share;
	}
	if (s->shown >= s->start || (s == &slots[LAY_SLOTS] && s->start != lgen)) {
		// a new pass into this buffer (it was shown since the last began)
		if (s->pid && s != &slots[LAY_SLOTS]) {
			lay_remap(s);
			s->flat = s->tris < 8 || s->moved * 50 < s->tris;
		}
		s->tris = s->moved = 0;
		s->segno = 0;
		s->segmax = s->ssegmax = s->scur = 0;
		s->lastseg = ~0u;
		s->pid = ++lpass;
		s->start = lgen;
		s->cur = s->top = 0;
	}
	LAY_COUNT(lay_prims, 1);
	bool untex = v[0].u == v[1].u && v[1].u == v[2].u && v[0].v == v[1].v && v[1].v == v[2].v;
	bool poly = ((v[0].x & 15) == 8 && (v[0].y & 15) == 7);
	int kind = poly | untex << 1;
	if (kind != lkind || (!untex && (abs(v[0].u - lku) >= 256 || abs(v[0].v - lkv) >= 256))) {
		lkind = kind; lku = v[0].u; lkv = v[0].v;
		lseg = (lseg + 1) & 0xfff;
	}
	unsigned pid = s->pid;
	// over other segments: their level + 1, by the rules alone (n) and
	// shown (h); over anything: the level shown
	int covn = 0, cov = 0, any = -1;
	for (int y = cy0; y <= cy1; y++)
		for (int x = cx0; x <= cx1; x++) {
			uint64_t c = lcell[y * LC_W + x];
			if (C_PID(c) != pid)
				continue;
			if (C_SEG(c) != lseg) {
				covn = (int)C_N(c) + 1 > covn ? (int)C_N(c) + 1 : covn;
				cov = (int)C_H(c) + 1 > cov ? (int)C_H(c) + 1 : cov;
			}
			any = (int)C_H(c) > any ? (int)C_H(c) : any;
		}
	int dw = s->w ? s->w : 320, dh = s->h ? s->h : 240;
	bool big = (float)(px1 - px0) * (py1 - py0) >= 0.5f * dw * dh;
	int h, n = 0;
	uint64_t cell = 0;
	bool segfirst = false;   // the segment's first primitive in this pass
	if (big && (s->cur == 0 || s->top == 0)) {
		h = 0;   // the picture's base: a clear, a background
		s->edge = px0;
	} else if (big)
		h = n = any > 0 ? any : 0;   // over the picture: no step of its own
	else {
		// The level the pass has reached moves on when a segment ends (a
		// segment's own primitives don't lift each other).
		if (lseg != s->lastseg) {
			s->lastseg = lseg;
			s->segno++;
			int sw = px1 - px0, sh = py1 - py0;
			uint32_t key = (1u << 31 | kind | (v[0].u & 0x7ff) << 2 | (v[0].v & 0x7ff) << 13) ^
				((uint32_t)(sw > 255 ? 255 : sw) << 24 | (uint32_t)(sh > 255 ? 255 : sh) << 16);
			struct lsegent *e = &lsegs[(key * 2654435761u) >> 21];
			if (e->key != key) {
				*e = (struct lsegent){ key, s->pid, 0, 0, 0, 0, -1 };
				segfirst = true;
			} else if (e->pass != s->pid) {   // the first time in this pass
				e->last = e->now;
				e->now = 0;
				e->pass = s->pid;
				segfirst = true;
			}
			lsegcur = e;
			s->cur = s->segmax > s->cur ? s->segmax : s->cur;
			s->scur = s->ssegmax > s->scur ? s->ssegmax : s->scur;
			s->segmax = s->ssegmax = 0;
		}
		// By the rules alone: depends only on this pass's drawing.
		n = LAY_FLOOR && covn < s->cur ? s->cur : covn;
		n = n < 255 ? n : 255;
		s->segmax = n > s->segmax ? n : s->segmax;
		// Shown: a segment is one plane, all of it at least at the level
		// the whole of it reached by the rules last pass (else a tile map
		// that steps up halfway would split, and the right eye would show
		// cracks between); still in front of what it covers.
		h = LAY_FLOOR && cov < s->scur ? s->scur : cov;
		h = n > h ? n : h;
		if (lsegcur) {
			if (n > lsegcur->now)
				lsegcur->now = n;
			// at most one level up: sprites reuse texture places, and a
			// remembered level from another use must not lift the rest
			int last = lsegcur->last < n + 1 ? lsegcur->last : n + 1;
			h = last > h ? last : h;
		}
		s->ssegmax = h > s->ssegmax ? h : s->ssegmax;
	}
	h = h < LAY_LEVELS - 1 ? h : LAY_LEVELS - 1;
	if (!big || h == 0) {
		cell = (uint64_t)h | (uint64_t)n << 8 | (uint64_t)lseg << 16 | (uint64_t)pid << 32;
		s->top = h > s->top ? h : s->top;
		for (int y = cy0; y <= cy1; y++)
			for (int x = cx0; x <= cx1; x++)
				lcell[y * LC_W + x] = cell;
	}
	lprev.cell = cell;
	const float *map = s == &slots[LAY_SLOTS] ? s->map : lshow;
	lprev.share = map[h];
	// Crack fill (lay_shift) only where the neighbour on the left is nearer:
	// tiles of one layer next to each other move together, and stretching
	// every one of them by the step cut the picture into bands.
	float nearer = map[h];
	if (px0 >= 1)
		for (int y = cy0, x = (px0 - 1) / LC; y <= cy1; y++) {
			uint64_t c = lcell[y * LC_W + x];
			if (c && C_SEG(c) != lseg && map[C_H(c)] < nearer)
				nearer = map[C_H(c)];
		}
	if (segfirst && lsegcur && s != &slots[LAY_SLOTS]) {
		// a piece where it was last pass: did it move to another depth? (the
		// depth table settling on new values moves whole levels, once: not this)
		int rx = px0 - s->x, ry = py0 - s->y;
		unsigned area = (unsigned)(px1 - px0 + 1) * (py1 - py0 + 1);
		if (lsegcur->shown >= 0 && lsegcur->rx == rx && lsegcur->ry == ry) {
			lsteady += area;
			lmoved += lsegcur->shown != h && lrintf(map[lsegcur->shown] * LAY_MAX) != lrintf(map[h] * LAY_MAX) ? area : 0;
		}
		lsegcur->rx = rx; lsegcur->ry = ry; lsegcur->shown = (int16_t)h;
	}
	lprev.step = lstep = map[h] - nearer;
#ifdef PSX3D_SIM
	if (getenv("STEREO_PRIMDBG"))
		fprintf(stderr, "prim %u slot %d,%d box %d,%d-%d,%d kind %d uv %d,%d seg %u segno %u cov %d cur %d h %d big %d share %.2f\n",
			lgen, s->x, s->y, px0, py0, px1, py1, kind, v[0].u, v[0].v, lseg, s->segno, cov, s->cur, h, big, lprev.share);
#endif
	s->tris++;
	s->moved += lprev.share > 0.02f;
	lprev.left = lleft = s == &slots[LAY_SLOTS] ? s->edge : s->x;
	return lprev.share;
}


// --- the hooks ---------------------------------------------------------------

bool __real_gs_core_alloc(void);
void __real_gs_core_free(void);
void __real_gs_core_clear(void);
void __real_gs_core_frame(float strength, int hres);
int __real_gs_shift(const gs_vtx *src, gs_vtx *dst, int n);

// all of it on the heap, only while 3D is on: nothing for the Old 3DS
GS_COLD bool __wrap_gs_core_alloc(void)
{
	if (!lcell) {
		lcell = calloc(LC_W * LC_H, sizeof(*lcell));
		slots = calloc(LAY_SLOTS + 1, sizeof(*slots));
		lsegs = calloc(LAY_SEGS, sizeof(*lsegs));
		if (lcell && slots && lsegs)
			lay_reset();
		else {
			free(lcell); free(slots); free(lsegs);
			lcell = NULL; slots = NULL; lsegs = NULL;
		}
	}
	return __real_gs_core_alloc() && lcell;
}

GS_COLD void __wrap_gs_core_free(void)
{
	__real_gs_core_free();
	free(lcell); free(slots); free(lsegs);
	lcell = NULL; slots = NULL; lsegs = NULL;
	lstrength = 0;
}

GS_COLD void __wrap_gs_core_clear(void)
{
	__real_gs_core_clear();
	lay_reset();
}

GS_COLD void __wrap_gs_core_frame(float strength, int hres)
{
	__real_gs_core_frame(strength, hres);
	lstrength = strength;
	// screen depth or layers: asked for LAY_WANT frames in a row before the
	// fade starts (a still that is flat one pass and not the next would
	// otherwise move the whole picture back and forth); movies and unsteady
	// scenes at once
	bool unsteady = lhold && lblend < 0.5f;
	float now = lnotgpu || gs_picture_flat() || unsteady ? 1 : 0;
	lasked = now == lscreen_want ? 0 : lasked + 1;
	if (lnotgpu || (unsteady && now > lscreen_want) || lasked >= LAY_WANT) {
		lscreen_want = now;
		lasked = 0;
	}
	float want = lscreen_want;
	lscreen += want > lscreen ? 1.0f / LAY_SCREEN_FADE : want < lscreen ? -1.0f / LAY_SCREEN_FADE : 0;
	lscreen = lscreen < 0.01f ? 0 : lscreen > 0.99f ? 1 : lscreen;
	lhres = hres > 0 ? hres : 320;
}

// Real 3D's shift (gpustereo_core.c), then the layers', blended. The layers
// put everything behind the screen: the right eye moves right only.
int __wrap_gs_shift(const gs_vtx *src, gs_vtx *dst, int n)
{
	int moved = __real_gs_shift(src, dst, n);
	if (!lcell || lstrength <= 0)
		return moved;
	float a = gs_focus() > 0 ? lblend : 0;
	float k = 16 * LAY_MAX * lhres / 320.0f * lstrength * (1 - a) * (1 - lscreen) * lready;
	// Draws the drawing area doesn't limit (fills, CPU uploads) would carry
	// the shift into whatever lies to their right: the other framebuffer of
	// games that keep two side by side (SotN), whose left edge then showed
	// it every other frame.
	int scr = lay_scissor_right();
	moved = 0;
	for (int t = 0; t + 3 <= n; t += 3) {
		gs_vtx *d = &dst[t];
		const gs_vtx *s = &src[t];
		bool depth = d[0].x != s[0].x || d[1].x != s[1].x || d[2].x != s[2].x;
		ltris++;
		ldepth += depth;
		// settled on Real 3D (3D gameplay): no layer work at all. While
		// fading between the two (or about to), a triangle with real depth takes only Real
		// 3D's shift (one value per shared corner, scaled by the blend): a
		// per-triangle layer step on top of it opened cracks between
		// neighbours sharing those corners.
		lright = -1;
		float lay = a < 1 && !(depth && (a > 0 || lwant >= 1)) ? lay_tri(s, t == 0) * k : 0;
		int limit = lright >= 0 && scr > lright ? lright : -1;   // the buffer's right edge, if the draw passes it
		if (limit >= 0 && s[0].u == s[1].u && s[1].u == s[2].u && s[0].v == s[1].v && s[1].v == s[2].v) {
			d[0] = s[0]; d[1] = s[1]; d[2] = s[2];   // a fill: one colour, nothing to move
			continue;
		}
		// A layer moves right further than the one in front of it: next to
		// a nearer neighbour on its left it would leave a crack. Its left
		// edge comes back by that step (5 px at most; the texture stretches).
		int xmin = s[0].x < s[1].x ? s[0].x : s[1].x;
		xmin = s[2].x < xmin ? s[2].x : xmin;
		long whole = lrintf(lay * (1 / 16.0f)), pull = whole - lrintf((lay - lstep * k) * (1 / 16.0f));
		pull = pull < 5 ? pull : 5;
		for (int i = 0; i < 3; i++) {
			float sh = a * (d[i].x - s[i].x);
			// the buffer's left edge stays put (moved right, it would leave
			// a strip the right eye never draws); whole pixels: sprites stay sharp
			if (lay > 0 && !(lleft >= 0 && (((s[i].x >> 4) - lleft) & 1023) == 0))
				sh += 16 * (whole - (s[i].x == xmin ? pull : 0));
			int x = s[i].x + (int)lrintf(sh);
			if (limit >= 0 && x > limit * 16 && x > s[i].x)
				x = s[i].x > limit * 16 ? s[i].x : limit * 16;
			d[i].x = x < 0 ? 0 : x > 1024 * 16 - 1 ? 1024 * 16 - 1 : x;
			moved += d[i].x != s[i].x;
		}
	}
	return moved;
}

#ifdef PSX3D_SIM
int sim_scissor_right(void);   // the replay's citro3d
static int lay_scissor_right(void) { return sim_scissor_right(); }
#else
void __real_gpustereo_status(char *out, int n);
GS_COLD void __wrap_gpustereo_status(char *out, int n)
{
	__real_gpustereo_status(out, n);
	int k = strlen(out);
	char buf[512];
	gs_layer_status(buf, sizeof(buf));
	snprintf(out + k, n - k, ",\"layers3d\":{\"real3d\":%.2f,\"flat\":%s,\"unsteady\":%.3f,\"hold\":%d,\"buffers\":[%s]}",
		lblend, gs_picture_flat() ? "true" : "false", lunst, lhold, buf);
}

// the picture the frontend shows (GPU thread, in draw order): a buffer
#include <3ds.h>
#include <citro3d.h>
#include "../vendor/citro3d/internal.h"
#include "../../core/plugins/gpu_pica/gpu_pica.h"
// the draw being mirrored (gpustereo.c, inside C3D_DrawArrays) uses this scissor
static int lay_scissor_right(void)
{
	C3D_Context *ctx = C3Di_GetContext();
	return ctx->scissor[0] == GPU_SCISSOR_DISABLE ? 1024 : (int)(ctx->scissor[2] & 0xffff) + 1;
}
int __real_pica_picture(C3D_Tex **tex, float *l, float *t, float *r, float *b, float *w, float *h);
int __wrap_pica_picture(C3D_Tex **tex, float *l, float *t, float *r, float *b, float *w, float *h)
{
	int pic = __real_pica_picture(tex, l, t, r, b, w, h);
	lnotgpu = pic != PICA_PIC_GPU;
	if (pic == PICA_PIC_GPU && lcell && lstrength > 0 && ldrew) {
		// once per picture: both eyes and thumbnails ask again
		ldrew = false;
		gs_core_shown((int)lrintf(*l * 1024), (int)lrintf((1 - *t) * 512), (int)*w, (int)*h);
	}
	return pic;
}

#endif

// How much of the 3D effect is screen depth (video.c). Without polygon
// depth (gpustereo_core.c's gs_flat) the layers take over, except for
// pictures with nothing to separate and movies: there screen depth stays.
float __real_gs_flat(void);
float __wrap_gs_flat(void)
{
	float f = __real_gs_flat();
	return lcell && lstrength > 0 ? f * lscreen : f;
}
