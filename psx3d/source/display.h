#ifndef PSX3D_DISPLAY_H
#define PSX3D_DISPLAY_H

#include <stdbool.h>

// Where the PS1 picture goes on the 400x240 top screen. gpulib describes the
// TV field the game's display range sits in (hres x vres) and where the
// picture starts in it (x, y: its own centring of the display range). The
// field is shown like a TV: hres maps to the box width, 240 lines (one line
// per row in Original) to the box height, so short pictures get black borders
// instead of being stretched. 480i fields have two picture lines per line.
// PAL's 256-line field is shown through a 240-line window that keeps the
// picture whole; a picture taller than 240 lines is scaled to fit.
typedef struct {
	int hres, vres;   // gpulib screen.hres / screen.vres
	int x, y;         // gpulib screen.x / screen.y (negative: starts off screen)
	int w, h;         // the picture, in PS1 pixels and lines
} display_geom;

typedef struct {
	int sx, sy, sw, sh;       // the part of the picture shown, from its top left
	float dx, dy, dw, dh;     // where it goes on the 400x240 screen
} display_rect;

#define DISPLAY_LINES 240

// box_w x box_h: the screen mode's 4:3 / stretched / widescreen box, centred
// on the screen. False if nothing of the picture is visible.
bool display_fit(const display_geom *g, float box_w, float box_h, display_rect *out);

#endif
