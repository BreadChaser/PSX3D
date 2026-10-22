// display_fit: where the PS1 picture goes on the 400x240 screen. The cases are
// display setups recorded from real games (devtools/gpu-audit frames.json).
// With an argument it reads "hres vres x y w h box_w box_h" lines and prints
// the result (the image comparison in devtools/display-centering uses that).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../source/display.h"

static int failures;

static void expect(const char *name, display_geom g, float bw, float bh, bool ok,
	int sx, int sy, int sw, int sh, float dx, float dy, float dw, float dh)
{
	display_rect r;
	memset(&r, 0, sizeof(r));
	bool got = display_fit(&g, bw, bh, &r);
	bool same = got == ok && (!ok || (r.sx == sx && r.sy == sy && r.sw == sw && r.sh == sh &&
		fabsf(r.dx - dx) < 1e-3f && fabsf(r.dy - dy) < 1e-3f &&
		fabsf(r.dw - dw) < 1e-3f && fabsf(r.dh - dh) < 1e-3f));
	if (!same) {
		failures++;
		printf("FAIL %s: got %d src %d,%d %dx%d dst %.3f,%.3f %.3fx%.3f\n", name, got,
			r.sx, r.sy, r.sw, r.sh, r.dx, r.dy, r.dw, r.dh);
	}
}

#define G(...) ((display_geom){ __VA_ARGS__ })

int main(int argc, char **argv)
{
	(void)argv;
	if (argc > 1) {
		display_geom g;
		float bw, bh;
		while (scanf("%d %d %d %d %d %d %f %f", &g.hres, &g.vres, &g.x, &g.y, &g.w, &g.h, &bw, &bh) == 8) {
			display_rect r;
			if (display_fit(&g, bw, bh, &r))
				printf("1 %d %d %d %d %.6f %.6f %.6f %.6f\n", r.sx, r.sy, r.sw, r.sh, r.dx, r.dy, r.dw, r.dh);
			else
				printf("0\n");
		}
		return 0;
	}
	// Original (320x240 box), the common full field: unchanged 1:1
	expect("full 320x240", G(320, 240, 0, 0, 320, 240), 320, 240, true, 0, 0, 320, 240, 40, 0, 320, 240);
	// CTR: 512x216 picture 12 lines down: black borders, not stretched to 240
	expect("CTR", G(512, 240, 0, 12, 512, 216), 320, 240, true, 0, 0, 512, 216, 40, 12, 320, 216);
	// MGS, Silent Hill, Vagrant Story gameplay: 224 lines, 8 down
	expect("MGS", G(320, 240, 0, 8, 320, 224), 320, 240, true, 0, 0, 320, 224, 40, 8, 320, 224);
	// Silent Hill / Vagrant Story 480i: 448 lines 16 down = 224 rows 8 down
	expect("SH 480i", G(320, 480, 0, 16, 320, 448), 320, 240, true, 0, 0, 320, 448, 40, 8, 320, 224);
	// BIOS logo, 640x478 interlaced
	expect("BIOS 480i", G(640, 480, 0, 0, 640, 478), 320, 240, true, 0, 0, 640, 478, 40, 0, 320, 239);
	// GT2 menus: 352 of 368 pixels, 8 right: side borders
	expect("GT2 menu", G(368, 480, 8, 0, 352, 480), 320, 240, true, 0, 0, 352, 480,
		40 + 8 * 320.0f / 368, 0, 352 * 320.0f / 368, 240);
	// Ape Escape: starts 8 pixels before the field: those are off the TV
	expect("Ape Escape", G(368, 240, -8, 0, 376, 240), 320, 240, true, 8, 0, 368, 240, 40, 0, 320, 240);
	// FF8 battle: both
	expect("FF8 battle", G(368, 240, -8, 8, 376, 216), 320, 240, true, 8, 0, 368, 216, 40, 8, 320, 216);
	// a picture running past the field's bottom is cut there
	expect("past bottom", G(320, 240, 0, 8, 320, 240), 320, 240, true, 0, 0, 320, 232, 40, 8, 320, 232);
	expect("above top", G(320, 240, 0, -4, 320, 240), 320, 240, true, 0, 4, 320, 236, 40, 0, 320, 236);
	// PAL 256-line field: a 240-line picture is shown 1:1 wherever it sits
	expect("PAL top", G(320, 256, 0, 0, 320, 240), 320, 240, true, 0, 0, 320, 240, 40, 0, 320, 240);
	expect("PAL middle", G(320, 256, 0, 8, 320, 240), 320, 240, true, 0, 0, 320, 240, 40, 0, 320, 240);
	expect("PAL bottom", G(320, 256, 0, 16, 320, 240), 320, 240, true, 0, 0, 320, 240, 40, 0, 320, 240);
	// a short PAL picture keeps the centred window and its borders
	expect("PAL short", G(320, 256, 0, 20, 320, 200), 320, 240, true, 0, 0, 320, 200, 40, 12, 320, 200);
	// a full 256-line PAL picture has to be scaled to fit
	expect("PAL 256", G(384, 256, 0, 0, 384, 256), 320, 240, true, 0, 0, 384, 256, 40, 0, 320, 240);
	expect("PAL 512i", G(512, 512, 0, 0, 512, 512), 320, 240, true, 0, 0, 512, 512, 40, 0, 320, 240);
	// Stretch: the same framing, 400 wide
	expect("CTR stretch", G(512, 240, 0, 12, 512, 216), 400, 240, true, 0, 0, 512, 216, 0, 12, 400, 216);
	// Widescreen 16:9: 400x225 box
	expect("CTR 16:9", G(512, 240, 0, 12, 512, 216), 400, 225, true, 0, 0, 512, 216,
		0, 7.5f + 12 * 225.0f / 240, 400, 216 * 225.0f / 240);
	// nothing visible
	expect("below field", G(320, 240, 0, 240, 320, 240), 320, 240, false, 0, 0, 0, 0, 0, 0, 0, 0);
	expect("no field", G(0, 0, 0, 0, 320, 240), 320, 240, false, 0, 0, 0, 0, 0, 0, 0, 0);
	expect("no picture", G(320, 240, 0, 0, 0, 240), 320, 240, false, 0, 0, 0, 0, 0, 0, 0, 0);
	if (failures)
		printf("%d failures\n", failures);
	else
		printf("display_fit: all cases pass\n");
	return failures != 0;
}
