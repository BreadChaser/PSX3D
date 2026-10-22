// Real 3D's screen depth fallback (gpustereo_core.c): frames without polygon
// depth fade to the flat picture behind the screen, and back when depth
// returns, without flicker for games that draw on every 2nd/3rd/4th frame.
#include "../../psx3d/source/gpustereo.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#define E3 0u                          // drawing area 0,0 ..
#define E4 (319u | (239u << 10))       // .. 319,239
#define E5 0u

static void depth_frame(int with_depth)
{
	// 20 triangles with three known corners each: 60 depth samples
	for (int t = 0; with_depth && t < 20; t++) {
		uint32_t w[4] = { 0x20000000, (uint32_t)(10 + t * 10) | (20u << 16),
			(uint32_t)(40 + t * 10) | (20u << 16), (uint32_t)(10 + t * 10) | (60u << 16) };
		uint16_t z[4] = { (uint16_t)(400 + t * 150), (uint16_t)(450 + t * 150), (uint16_t)(500 + t * 150), 0 };
		gs_capture(w, 0x20, E3, E4, E5, z);
	}
	gs_core_frame(1, 320);
}

// how far the right eye moves a vertex of the last captured triangle
static int shift_now(void)
{
	gs_vtx src[3] = { { 200 * 16 + 8, 20 * 16 + 7, 0, 0, 0 }, { 230 * 16 + 8, 20 * 16 + 7, 0, 0, 0 }, { 200 * 16 + 8, 60 * 16 + 7, 0, 0, 0 } }, dst[3];
	gs_shift(src, dst, 3);
	return abs(dst[0].x - src[0].x) + abs(dst[1].x - src[1].x) + abs(dst[2].x - src[2].x);
}

int main(void)
{
	assert(gs_core_alloc());
	gs_core_clear();
	assert(gs_flat() == 1);

	// 3D gameplay: real 3D fades in over 24 frames, never back
	float last = 1;
	for (int f = 1; f <= 24; f++) {
		depth_frame(1);
		assert(gs_flat() < last);
		last = gs_flat();
	}
	assert(gs_flat() == 0);
	int full = shift_now();
	assert(full > 0);

	// 30, 20 and 15 fps games: frames without new polygons in between
	for (int every = 2; every <= 4; every++)
		for (int f = 0; f < 240; f++) {
			depth_frame(f % every == 0);
			assert(gs_flat() == 0);
		}

	// a logo / movie / 2D scene: flat after 12 frames, fully over 24 more
	depth_frame(1);
	for (int f = 1; f <= 12 + 12; f++) {
		depth_frame(0);
		if (f < 12) assert(gs_flat() == 0);
	}
	float mid = gs_flat();
	assert(mid > 0 && mid < 1);
	depth_frame(1);   // depth again: the fade turns back, the real disparity is partial
	assert(gs_flat() < mid);
	int part = shift_now();
	assert(part > 0 && part < full);
	// the fade holds while a stray depth frame is followed by none
	float back = gs_flat();
	for (int f = 0; f < 11; f++) {
		depth_frame(0);
		assert(f < 3 ? gs_flat() < back : gs_flat() == back);
		back = gs_flat();
	}
	assert(back > 0);
	for (int f = 0; f < 30; f++) depth_frame(0);
	assert(gs_flat() == 1);
	assert(shift_now() == 0);   // flat: both eyes see the same picture, shifted as a whole

	// back to gameplay: real 3D again within 24 frames
	for (int f = 0; f < 24; f++) depth_frame(1);
	assert(gs_flat() == 0);

	gs_core_free();
	puts("stereo flat fallback: ok");
	return 0;
}
