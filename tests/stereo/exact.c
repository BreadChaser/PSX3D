// Real 3D (gpustereo_core.c): every triangle finds its own corners' depths.
// - a 2D polygon drawn over a 3D vertex doesn't take that vertex's depth;
// - a vertex shared by triangles moves alike in all of them, also when one
//   of its polygons has no depth for it (no cracks between them);
// - a corner with another object's depth is vetoed for every polygon;
// - 2D primitives drawn before a pass's first 3D polygon go to the back
//   by whole pixels, those drawn after it stay on the screen.
#include "../../psx3d/source/gpustereo.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#define E3 0u                          // drawing area 0,0 ..
#define E4 (319u | (239u << 10))       // .. 319,239
#define E5 0u
#define PX(x) ((x) * 16 + 8)
#define PY(y) ((y) * 16 + 7)

static uint32_t xy(int x, int y) { return (uint32_t)(x & 0x7ff) | (uint32_t)(y & 0x7ff) << 16; }

// a flat-shaded triangle (0x20) or quad (0x28) with its corners' GTE depths
static void tri(int x0, int y0, int x1, int y1, int x2, int y2, int z0, int z1, int z2)
{
	uint32_t w[4] = { 0x20000000, xy(x0, y0), xy(x1, y1), xy(x2, y2) };
	uint16_t z[4] = { (uint16_t)z0, (uint16_t)z1, (uint16_t)z2, 0 };
	gs_capture(w, 0x20, E3, E4, E5, z);
}

// right-eye shifts (1/16 px) of a drawn triangle
static void shift(int x0, int y0, int x1, int y1, int x2, int y2, int out[3], int poly)
{
	int o = poly ? 0 : 1;   // sprites: whole pixels, no (8, 7)
	gs_vtx src[3] = { { (int16_t)(PX(x0) - 8 * o), (int16_t)(PY(y0) - 7 * o), 0, 0, 0xff000000u },
		{ (int16_t)(PX(x1) - 8 * o), (int16_t)(PY(y1) - 7 * o), 0, 0, 0xff000000u },
		{ (int16_t)(PX(x2) - 8 * o), (int16_t)(PY(y2) - 7 * o), 0, 0, 0xff000000u } }, dst[3];
	gs_shift(src, dst, 3);
	for (int i = 0; i < 3; i++)
		out[i] = dst[i].x - src[i].x;
}

// a 3D scene: a grid of 20 triangle pairs over a depth slope (60+ samples)
static void scene(void)
{
	for (int i = 0; i < 20; i++) {
		int x = 10 + i * 14, z = 300 + i * 120;
		tri(x, 100, x + 14, 100, x, 140, z, z + 120, z);
		tri(x + 14, 100, x + 14, 140, x, 140, z + 120, z + 120, z);
	}
}

// the GPU thread draws the scene's triangles
static void draw_scene(void)
{
	int o[3];
	for (int i = 0; i < 20; i++) {
		int x = 10 + i * 14;
		shift(x, 100, x + 14, 100, x, 140, o, 1);
		shift(x + 14, 100, x + 14, 140, x, 140, o, 1);
	}
}

int main(void)
{
	assert(gs_core_alloc());
	gs_core_clear();
	gs_core_pass_shown(0, 0, 320, 240);
	for (int f = 0; f < 40; f++) {   // the depth range settles, real 3D fades in
		scene();
		gs_core_frame(1, 320);
	}
	assert(gs_flat() == 0);
	scene();

	// two triangles of the mesh share the edge (150,100)-(150,140): alike
	int a[3], b[3];
	shift(136, 100, 150, 100, 136, 140, a, 1);
	shift(150, 100, 150, 140, 136, 140, b, 1);
	assert(a[1] == b[0] && a[1] != 0);
	assert(a[2] == b[2]);

	// a 2D polygon (no depth) with one corner on the 3D vertex (150,100),
	// as a logo over the scene: stays on the screen, whole and unmoved
	tri(150, 100, 200, 60, 230, 90, 0, 0, 0);
	int t2d[3];
	shift(150, 100, 200, 60, 230, 90, t2d, 1);
	assert(t2d[0] == 0 && t2d[1] == 0 && t2d[2] == 0);
	// a polygon whose GTE depths the record missed, on 3D vertices of the
	// mesh: moves with the mesh (no crack)
	tri(150, 100, 164, 100, 150, 140, 0, 0, 0);
	int m3[3];
	shift(150, 100, 164, 100, 150, 140, m3, 1);
	assert(m3[0] == a[1] && m3[2] == b[1] && m3[0] != 0);
	// ... and the 3D triangles still move
	shift(136, 100, 150, 100, 136, 140, b, 1);
	assert(b[1] == a[1]);

	// a corner with no depth of its own takes the vertex's depth that
	// another polygon recorded: shared vertex (164,100) moves alike
	tri(164, 100, 178, 100, 164, 60, 0, 1000, 1000);
	int c[3], d[3];
	shift(164, 100, 178, 100, 164, 60, c, 1);
	shift(150, 100, 164, 100, 150, 140, d, 1);   // from scene(): (150,100),(164,100),(150,140)
	assert(c[0] == d[1]);

	// a corner with another object's depth (near 100 among far 3000s) is
	// vetoed: none of the polygons on that pixel keep it
	tri(250, 20, 270, 20, 250, 40, 100, 3000, 3000);
	tri(270, 20, 270, 40, 250, 20, 3000, 3000, 100);
	int e[3], g[3];
	shift(250, 20, 270, 20, 250, 40, e, 1);
	shift(270, 20, 270, 40, 250, 20, g, 1);
	assert(e[0] == g[2]);
	assert(abs(e[0] - e[1]) < 32);   // within 2 px of the far corners, not near
	draw_scene();
	gs_core_frame(1, 320);

	// backdrop: a new pass (the buffer was shown); the previous pass had
	// depth. A sprite before the first 3D polygon goes to the back by whole
	// pixels; one after it stays on the screen.
	gs_core_pass_shown(0, 0, 320, 240);
	int s1[3], s2[3];
	shift(20, 10, 60, 10, 20, 30, s1, 0);
	assert(s1[0] > 0 && s1[0] % 16 == 0 && s1[0] == s1[1] && s1[1] == s1[2]);
	scene();
	draw_scene();   // the pass's 3D polygons
	shift(20, 10, 60, 10, 20, 30, s2, 0);
	assert(s2[0] == 0 && s2[1] == 0 && s2[2] == 0);
	// the left edge of the buffer stays put (no strip the right eye misses)
	gs_core_frame(1, 320);
	gs_core_pass_shown(0, 0, 320, 240);
	shift(0, 10, 60, 10, 0, 30, s1, 0);
	assert(s1[0] == 0 && s1[2] == 0 && s1[1] > 0);

	gs_core_free();
	puts("stereo exact triangles: ok");
	return 0;
}
