#include "display.h"

bool display_fit(const display_geom *g, float box_w, float box_h, display_rect *out)
{
	int x = g->x, y = g->y, w = g->w, h = g->h, sx = 0, sy = 0;
	if (g->hres <= 0 || g->vres <= 0)
		return false;
	// the parts of the picture outside the field are off the TV
	if (x < 0) { sx = -x; w += x; x = 0; }
	if (x + w > g->hres) w = g->hres - x;
	if (y < 0) { sy = -y; h += y; y = 0; }
	if (y + h > g->vres) h = g->vres - y;
	if (w <= 0 || h <= 0)
		return false;

	int k = g->vres > 256 ? 2 : 1;   // 480i: two picture lines per TV line
	float lines = (float)g->vres / k, top = (float)y / k, ph = (float)h / k;
	float vis = ph > DISPLAY_LINES ? ph : DISPLAY_LINES;
	// the window of the field that is shown: centred, moved to keep the picture
	float win = (lines - vis) / 2;
	if (lines > vis) {
		if (win > top) win = top;
		if (win < top + ph - vis) win = top + ph - vis;
	}
	float scale_x = box_w / g->hres, scale_y = box_h / vis;
	out->sx = sx;
	out->sy = sy;
	out->sw = w;
	out->sh = h;
	out->dx = (400 - box_w) / 2 + x * scale_x;
	out->dy = (DISPLAY_LINES - box_h) / 2 + (top - win) * scale_y;
	out->dw = w * scale_x;
	out->dh = ph * scale_y;
	return true;
}
