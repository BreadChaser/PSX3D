// The game library screen. Top: synthwave backdrop, a box art carousel and
// the game's clean title. Bottom: a quiet list (touch-friendly) and a bar of
// touch buttons. Round 3 of the library design (LIBRARY-DESIGN.md in the UI
// mock): recently played games at the top of A-Z, favourites (Y), sort views
// (X) and a letter jump (L/R, or dragging along the right edge).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "psx3d.h"
#include "devtools.h"
#include "presetupdate.h"

#define ROWS 8
#define ROW_H 24
#define RECENT_N 5

static C2D_Font title_font;
static C3D_Tex bg_tex;
static Tex3DS_Texture bg_t3x;
static C2D_Image bg_img;
static bool bg_ok;
static C2D_TextBuf tbuf;

static int sel, list_top;
static float anim, anim_v;         // carousel position (spring toward sel) and velocity
static float pop = 1, pop_v;       // selected cover scale (spring, overshoots)
static float title_t = 1;          // 0..1 title slide/fade-in after a change
static int title_for = -1;         // game the title animation belongs to
static float clock_s;              // seconds since the screen opened
static float fade = 1;             // 1 = black, eases to 0 on open
static float launch_t = -1;        // >= 0 while the launch zoom plays
static int rep_l, rep_r, rep_u, rep_d, rep_pl, rep_pr;
static char notice[96];            // a short message on the top screen
static int notice_frames;
static int jump_frames;            // > 0 while the big letter of a jump shows
static bool scrubbing;             // a drag along the list's right edge

// What the list shows: the top level (series and the games not in one), or
// the games of one open series. Entries >= 0 are games, < 0 are series -(s+1).
// The sort views put section headings between the entries: the list counts
// them as rows, the carousel only shows entries.
enum { V_AZ, V_RECENT, V_MOST, V_FAV, V_COUNT };
static const char *const view_names[V_COUNT] = { "A-Z", "Recent", "Most played", "Favourites" };
static int view[2048 + 512];
static int nview;
static int open_series = -1;
static int top_sel;                // selection to go back to when leaving a series
static struct { int before; const char *label; } hdr[2];
static int nhdr;
static int az_start = -1;          // first entry of the alphabetical part, -1 none

static const char *save_key(int game)
{
	const game_t *g = library_get(game);
	return g && g->cover_key ? g->cover_key : "";
}

static bool favourite(int game)
{
	return game >= 0 && playlog_favourite(save_key(game));
}

static u32 sort_key[2048];         // per game, while sorting
static int by_key(const void *a, const void *b)
{
	u32 ka = sort_key[*(const int *)a], kb = sort_key[*(const int *)b];
	return ka < kb ? 1 : ka > kb ? -1 : *(const int *)a - *(const int *)b;
}

// the top level, alphabetical: each series sits where its name sorts
static void add_az(void)
{
	int n = library_count(), ns = library_series_count(), si = 0;
	static int order[512];
	for (int s = 0; s < ns; s++)
		order[s] = s;
	for (int a = 1; a < ns; a++)   // insertion sort by name (few series)
		for (int b = a; b > 0 && strcasecmp(library_series_name(order[b]), library_series_name(order[b - 1])) < 0; b--) {
			int t = order[b]; order[b] = order[b - 1]; order[b - 1] = t;
		}
	for (int i = 0; i < n; i++) {
		if (library_game_series(i) >= 0)
			continue;
		while (si < ns && strcasecmp(library_series_name(order[si]), library_get(i)->title) < 0)
			view[nview++] = -(order[si++] + 1);
		view[nview++] = i;
	}
	while (si < ns)
		view[nview++] = -(order[si++] + 1);
}

static void build_view(void)
{
	nview = nhdr = 0;
	az_start = -1;
	if (open_series >= 0) {
		for (int k = 0; k < library_series_size(open_series); k++)
			view[nview++] = library_series_game(open_series, k);
		return;
	}
	int n = library_count(), mode = g_cfg.library_view;
	if (n > 2048)
		n = 2048;
	if (mode == V_FAV) {   // single games, A-Z
		az_start = 0;
		for (int i = 0; i < n; i++)
			if (favourite(i))
				view[nview++] = i;
		return;
	}
	// played games, newest (or longest) first
	static int played[2048];
	int np = 0;
	for (int i = 0; i < n; i++) {
		const char *k = save_key(i);
		u32 last = playlog_last(k);
		sort_key[i] = mode == V_MOST ? playlog_seconds(k) : last;
		if (last)
			played[np++] = i;
	}
	qsort(played, np, sizeof(played[0]), by_key);
	if (mode == V_AZ) {
		if (np) {
			hdr[0].before = 0;
			hdr[nhdr++].label = "Recently played";
			for (int i = 0; i < np && i < RECENT_N; i++)
				view[nview++] = played[i];
			hdr[nhdr].before = nview;
			hdr[nhdr++].label = "All games";
		}
		az_start = nview;
		add_az();
		return;
	}
	if (np) {
		hdr[0].before = 0;
		hdr[nhdr++].label = mode == V_MOST ? "Most played" : "Recently played";
		for (int i = 0; i < np; i++)
			view[nview++] = played[i];
	}
	if (np < n) {
		hdr[nhdr].before = nview;
		hdr[nhdr++].label = "Not played yet";
		for (int i = 0; i < n; i++)
			if (!playlog_last(save_key(i)))
				view[nview++] = i;
	}
}

static int nrows(void) { return nview + nhdr; }

static int row_of(int e)
{
	int r = e;
	for (int h = 0; h < nhdr; h++)
		if (hdr[h].before <= e)
			r++;
	return r;
}

// the entry a list row shows, or -1 - h for heading h
static int row_entry(int r)
{
	int skip = 0;
	for (int h = 0; h < nhdr; h++) {
		if (r == hdr[h].before + h)
			return -1 - h;
		if (r > hdr[h].before + h)
			skip++;
	}
	return r - skip;
}

// the game an entry stands for (a series shows its first game's cover)
static int entry_game(int e)
{
	if (e < 0 || e >= nview)
		return -1;
	int v = view[e];
	return v >= 0 ? v : library_series_game(-v - 1, 0);
}

static int entry_series(int e)
{
	return e >= 0 && e < nview && view[e] < 0 ? -view[e] - 1 : -1;
}

static const char *entry_title(int e)
{
	int s = entry_series(e);
	if (s >= 0)
		return library_series_name(s);
	const game_t *g = library_get(entry_game(e));
	return g ? g->title : "";
}

// the theme's accent colours, one per game in turn
static const u32 *accent(int i)
{
	return &theme()->accent[i & 3];
}

void libui_init(void)
{
	title_font = C2D_FontLoad("romfs:/font/title.bcfnt");
	tbuf = C2D_TextBufNew(4096);
	FILE *f = fopen("romfs:/gfx/library_bg.t3x", "rb");
	if (f) {
		bg_t3x = Tex3DS_TextureImportStdio(f, &bg_tex, NULL, false);
		fclose(f);
		if (bg_t3x) {
			bg_img.tex = &bg_tex;
			bg_img.subtex = Tex3DS_GetSubTexture(bg_t3x, 0);
			bg_ok = true;
		}
	}
	if (!title_font)
		log_printf("libui: title font missing, using system font\n");
}

void libui_exit(void)
{
	if (bg_ok) {
		Tex3DS_TextureFree(bg_t3x);
		C3D_TexDelete(&bg_tex);
	}
	if (title_font)
		C2D_FontFree(title_font);
	C2D_TextBufDelete(tbuf);
}

static void text(C2D_Font font, float x, float y, float size, u32 color, u32 flags, const char *s)
{
	C2D_Text t;
	if (!C2D_TextFontParse(&t, font, tbuf, s))
		return;
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor | flags, x, y, 0.5f, size, size, color);
}

static float text_width(C2D_Font font, float size, const char *s)
{
	C2D_Text t;
	float w = 0, h = 0;
	if (!C2D_TextFontParse(&t, font, tbuf, s))
		return 0;
	C2D_TextGetDimensions(&t, size, size, &w, &h);
	return w;
}

// a cover, or a placeholder card with the game's initial
static void draw_cover(int e, float cx, float cy, float size, float dim, float z)
{
	int i = entry_game(e);
	const game_t *g = library_get(i);
	if (!g)
		return;
	float x = cx - size / 2, y = cy - size / 2;
	int s = entry_series(e);
	if (s >= 0) {
		// a series: the next games' covers stacked behind its first one
		// (plain cards until their art has loaded)
		for (int k = 2; k >= 1; k--) {
			float o = k * size * 0.06f, bx = x + o + ui_depth(0.3f * k), bz = z - 0.004f * k;
			C3D_Tex *t = k < library_series_size(s) && dim < 0.6f ? covers_get(library_series_game(s, k)) : NULL;
			if (t) {
				// fanned up and right, a little smaller, so each cover reads
				float bs = size * (1 - 0.07f * k), f = k * size * 0.11f;
				Tex3DS_SubTexture st = { COVER_SIZE, COVER_SIZE, 0.0f, 1.0f, 1.0f, 0.0f };
				C2D_ImageTint tint;
				C2D_PlainImageTint(&tint, C2D_Color32(0, 0, 0, 255), dim + 0.15f * k);
				C2D_DrawImageAt((C2D_Image){ t, &st }, x + size - bs + f + ui_depth(0.3f * k), y - f, bz, &tint,
					bs / COVER_SIZE, bs / COVER_SIZE);
				continue;
			}
			u8 c = (u8)(40 - k * 8);
			C2D_DrawRectSolid(bx, y - o, bz, size, size,
				C2D_Color32(c, c + 6, c + 30, (u8)(255 * (1 - dim * 0.6f))));
			C2D_DrawRectSolid(bx, y - o, bz + 0.001f, size, 2,
				(*accent(e + k) & 0x00ffffff) | ((u32)(200 * (1 - dim)) << 24));
		}
	}
	// drop shadow on the "floor", a little behind the cover
	C2D_DrawRectSolid(x + 4 + ui_depth(1.5f), y + 6, z - 0.01f, size, size, C2D_Color32(0, 0, 0, 110));
	C3D_Tex *t = covers_get(i);
	if (t) {
		Tex3DS_SubTexture st = { COVER_SIZE, COVER_SIZE, 0.0f, 1.0f, 1.0f, 0.0f };
		C2D_Image img = { t, &st };
		C2D_ImageTint tint;
		C2D_PlainImageTint(&tint, C2D_Color32(0, 0, 0, 255), dim);
		C2D_DrawImageAt(img, x, y, z, dim > 0 ? &tint : NULL, size / COVER_SIZE, size / COVER_SIZE);
	} else {
		u32 c = *accent(e);
		C2D_DrawRectangle(x, y, z, size, size, C2D_Color32(20, 26, 60, 255), C2D_Color32(20, 26, 60, 255),
			C2D_Color32(8, 10, 28, 255), C2D_Color32(8, 10, 28, 255));
		C2D_DrawRectSolid(x, y + size - 6, z, size, 6, c);
		char initial[2] = { g->title[0], 0 };
		float s = size / 60.0f;
		text(title_font, cx, cy - 14 * s, s, C2D_Color32(220, 225, 245, 255), C2D_AlignCenter, initial);
		if (dim > 0)
			C2D_DrawRectSolid(x, y, z + 0.005f, size, size, C2D_Color32(0, 0, 0, (u8)(dim * 255)));
	}
	if (s < 0 && favourite(i)) {   // a favourite: a star on the corner
		float r = size * 0.09f;
		C2D_DrawCircleSolid(x + size - r * 1.6f, y + r * 1.6f, z + 0.006f, r * 1.35f, theme_alpha(theme()->menu_bg, (u8)(200 * (1 - dim))));
		ui_star(x + size - r * 1.6f, y + r * 1.6f, r, theme_alpha(ui_bar_color(UI_COL_STAR), (int)(255 * (1 - dim * 0.7f))), 0, z + 0.007f);
	}
}

// the neon floor, scrolling toward the viewer; each line gets its own depth
// so the floor recedes into the screen in 3D
// The floor: one wireframe mesh, flat in the middle and rising into hills
// toward the sides, rolling toward the viewer. Same maths as
// tools/make_art.py (height(), colour()), which draws it for the title art.
#define MESH_COLS 37            // x = -18 .. 18
#define MESH_ROWS 28
#define MESH_DZ   1.4f
#define HORIZON   140.0f

static float mesh_height(float x, float z_world, float z)
{
	float a = fabsf(x) - 2.0f;
	if (a <= 0)
		return 0;
	float rise = powf(a, 1.15f) * 0.32f;
	float n = 0.8f + 0.22f * sinf(x * 0.9f + z_world * 0.35f) + 0.12f * sinf(x * 1.7f - z_world * 0.5f);
	float near = (z - 1.0f) / 6.0f;
	if (near > 1) near = 1;
	if (near < 0) near = 0;
	return rise * n * (0.35f + 0.65f * near);
}

static u32 mesh_colour(float x, float z, u8 alpha)
{
	float side = fabsf(x) / 9.0f, depth = (z - 1.0f) / 25.0f;
	if (side > 1) side = 1;
	if (depth > 1) depth = 1;
	if (depth < 0) depth = 0;
	float t = powf(side, 0.8f);
	float r = 150 + (40 - 150) * t, g = 60 + (90 - 60) * t, b = 255;
	float p = powf(depth, 0.7f) * (1 - side) * 0.85f;
	r += (255 - r) * p; g += (70 - g) * p; b += (200 - b) * p;
	return C2D_Color32((u8)r, (u8)g, (u8)b, alpha);
}

// Line clipped to just beyond the screen. Far off-screen vertices (the
// nearest grid rows land thousands of pixels out) can make the GPU drop the
// whole line, so lines vanished before reaching the screen edges.
static void clip_line(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float w)
{
	const float xmin = -8, xmax = 408, ymin = -8, ymax = 248;
	float t0 = 0, t1 = 1, dx = x1 - x0, dy = y1 - y0;
	float p[4] = { -dx, dx, -dy, dy }, q[4] = { x0 - xmin, xmax - x0, y0 - ymin, ymax - y0 };
	for (int i = 0; i < 4; i++) {
		if (p[i] == 0) {
			if (q[i] < 0) return;
			continue;
		}
		float r = q[i] / p[i];
		if (p[i] < 0) { if (r > t1) return; if (r > t0) t0 = r; }
		else { if (r < t0) return; if (r < t1) t1 = r; }
	}
	C2D_DrawLine(x0 + dx * t0, y0 + dy * t0, c0, x0 + dx * t1, y0 + dy * t1, c1, w, 0.16f);
}

static void draw_grid(float t)
{
	static float px[MESH_ROWS + 1][MESH_COLS], py[MESH_ROWS + 1][MESH_COLS];
	static float hgt[MESH_ROWS + 1][MESH_COLS];
	static float rz[MESH_ROWS + 1];
	const float cx = 200, f = 225;
	float zoff = t * 0.45f;                      // world units scrolled (slow drift)
	float phase = fmodf(zoff, MESH_DZ);

	for (int j = 0; j <= MESH_ROWS; j++) {
		// The nearest row must always land below the screen, or the floor
		// stops short of the bottom edge for part of each scroll step (at
		// z = 1.85 it projected to y = 194). It runs from 0.45 down past the
		// camera; clamped at 0.4 it still projects below the screen (y ~ 392),
		// so the lines toward it reach the bottom edge.
		float z = 0.45f - phase + j * MESH_DZ;
		if (z < 0.4f)
			z = 0.4f;
		rz[j] = z;
		float zw = z + zoff;
		for (int i = 0; i < MESH_COLS; i++) {
			float x = i - 18.0f;
			float h = mesh_height(x, zw, z);
			hgt[j][i] = h;
			// far rows sit deeper behind the screen in 3D
			px[j][i] = cx + x / z * f + ui_depth(2.5f * (z - 1) / 40.0f - 0.5f);
			py[j][i] = HORIZON + (1.0f - h) / z * f * 0.45f;
		}
	}
	u32 fillc = C2D_Color32(16, 2, 44, 255);
	// back to front: hill cells hide the lines behind them
	for (int j = MESH_ROWS - 1; j >= 0; j--) {
		for (int i = 0; i < MESH_COLS - 1; i++) {
			if (hgt[j][i] + hgt[j][i + 1] + hgt[j + 1][i] + hgt[j + 1][i + 1] < 0.01f)
				continue;   // flat floor: nothing to hide
			float minx = fminf(fminf(px[j][i], px[j][i + 1]), fminf(px[j + 1][i], px[j + 1][i + 1]));
			float maxx = fmaxf(fmaxf(px[j][i], px[j][i + 1]), fmaxf(px[j + 1][i], px[j + 1][i + 1]));
			if (maxx < 0 || minx > 400)
				continue;   // off-screen: don't spend draw budget on it
			C2D_DrawTriangle(px[j][i], py[j][i], fillc, px[j][i + 1], py[j][i + 1], fillc,
				px[j + 1][i + 1], py[j + 1][i + 1], fillc, 0.15f);
			C2D_DrawTriangle(px[j][i], py[j][i], fillc, px[j + 1][i + 1], py[j + 1][i + 1], fillc,
				px[j + 1][i], py[j + 1][i], fillc, 0.15f);
		}
		// this strip's lines: the far edge across, and the lines toward the viewer
		float zf = rz[j + 1];
		u8 fade = zf > 32 ? (u8)(255 * (40 - zf) / 8) : 255;   // fade in at the horizon
		for (int i = 0; i < MESH_COLS - 1; i++)
			clip_line(px[j + 1][i], py[j + 1][i], mesh_colour(i - 18.0f, zf, fade),
				px[j + 1][i + 1], py[j + 1][i + 1], mesh_colour(i - 17.0f, zf, fade), 1.2f);
		for (int i = 0; i < MESH_COLS; i++)
			clip_line(px[j + 1][i], py[j + 1][i], mesh_colour(i - 18.0f, zf, fade),
				px[j][i], py[j][i], mesh_colour(i - 18.0f, rz[j], 255), 1.2f);
	}
	for (int i = 0; i < MESH_COLS - 1; i++)
		clip_line(px[0][i], py[0][i], mesh_colour(i - 18.0f, rz[0], 255),
			px[0][i + 1], py[0][i + 1], mesh_colour(i - 17.0f, rz[0], 255), 1.4f);
}

// under the title: series and disc counts, and in the sort views when it was played
static void sub_line(int e, char *out, size_t len)
{
	out[0] = 0;
	int ser = entry_series(e), game = entry_game(e);
	const game_t *g = library_get(game);
	if (!g)
		return;
	if (ser >= 0) {
		snprintf(out, len, "%d games", library_series_size(ser));
		return;
	}
	size_t p = 0;
	u32 last = playlog_last(save_key(game)), secs = playlog_seconds(save_key(game));
	if (g_cfg.library_view == V_MOST && secs >= 60)
		p = snprintf(out, len, secs >= 3600 ? "%lu h played" : "%lu min played",
			(unsigned long)(secs >= 3600 ? secs / 3600 : secs / 60));
	else if (last && (g_cfg.library_view == V_RECENT || (g_cfg.library_view == V_AZ && e < az_start))) {
		char when[40];
		states_when_text(when, sizeof(when), last);
		p = snprintf(out, len, "Played %s", when);
	}
	if (g->ndiscs > 1 && p < len)
		snprintf(out + p, len - p, "%s%d discs", p ? " · " : "", g->ndiscs);
}

static char letter_of(int e)
{
	const char *t = entry_title(e);
	char c = t[0] >= 'a' && t[0] <= 'z' ? t[0] - 32 : t[0];
	return c >= 'A' && c <= 'Z' ? c : '#';
}

// L/R letter jump: a big letter over the cover and the alphabet along the bottom
static void draw_jump(int a)
{
	const theme_t *th = theme();
	char cur[2] = { letter_of(sel), 0 };
	float cx = 200 + ui_depth(-1.2f);
	C2D_DrawRectSolid(cx - 56, 40, 0.8f, 112, 112, theme_alpha(th->menu_bg, a * 215 / 255));
	for (int i = 0; i < 4; i++)
		C2D_DrawRectSolid(cx - 56 + i * 28, 150, 0.81f, 26, 2, theme_alpha(th->accent[i], a));
	text(title_font, cx, 52, 2.2f, theme_alpha(th->game_title, a), C2D_AlignCenter, cur);
	// which letters have games
	bool has[27] = { false };
	for (int e = az_start < 0 ? 0 : az_start; e < nview; e++) {
		char c = letter_of(e);
		has[c == '#' ? 0 : c - 'A' + 1] = true;
	}
	C2D_DrawRectSolid(0, 204, 0.8f, 400, 20, theme_alpha(th->menu_bg, a * 200 / 255));
	for (int i = 0; i < 27; i++) {
		char l[2] = { i ? (char)('A' + i - 1) : '#', 0 };
		float x = 14 + i * 13.9f;
		bool here = l[0] == cur[0];
		u32 c = here ? th->game_title : has[i] ? th->game_sub : theme_alpha(th->game_sub, 70);
		text(NULL, x, 206, 0.4f, theme_alpha(c, here ? a : a * 200 / 255), C2D_AlignCenter, l);
		if (here)
			C2D_DrawRectSolid(x - 4, 221, 0.81f, 8, 1, theme_alpha(th->game_title, a));
	}
}

static void draw_top(void *ud)
{
	(void)ud;
	int n = nview;
	const theme_t *th = theme();
	if (bg_ok)
		C2D_DrawImageAt(bg_img, ui_depth(3.0f), 0, 0.1f, NULL, 1.0f, 1.0f);   // sky: far back
	draw_grid(clock_s);

	// carousel: neighbours first so the selection sits on top
	float spacing = 116.0f;
	float bob = sinf(clock_s * 2.0f) * 2.0f;
	for (int k = 3; k >= 0; k--)
		for (int side = -1; side <= 1; side += 2) {
			if (k == 0 && side == 1)
				continue;
			int i = (int)floorf(anim + 0.5f) + k * side;
			if (i < 0 || i >= n)
				continue;
			float d = i - anim;                       // distance from centre
			float ad = fabsf(d);
			float size = ad < 1 ? 136 - 48 * ad : 88 - 8 * (ad - 1);
			float cy = 96;
			if (i == sel) {
				size *= pop;
				cy += bob;
			}
			if (launch_t >= 0 && i == sel) {
				// launch: the cover rushes toward you
				float e = launch_t * launch_t;
				size *= 1 + e * 2.2f;
				cy += e * 20;
			}
			float depth = ad < 0.5f ? -1.2f + 2.4f * ad : 0.6f + 0.4f * ad;   // selected pops out
			float cx = 200 + d * spacing + ui_depth(depth);
			float dim = ad < 1 ? 0.55f * ad : 0.55f + 0.1f * (ad - 1);
			if (dim > 0.85f) dim = 0.85f;
			draw_cover(i, cx, cy, size, dim, 0.5f - ad * 0.02f);
		}

	const game_t *g = library_get(entry_game(sel));
	if (g && launch_t < 0) {
		const char *title = entry_title(sel);
		// title slides up and fades in after the selection changes
		float e = 1 - (1 - title_t) * (1 - title_t);
		if (jump_frames > 0)
			e *= 0.35f;   // the big letter is what to read now
		u8 a = (u8)(255 * e);
		float ty = 178 + (1 - e) * 10;
		C2D_Font f = title_font;
		float size = 0.9f;
		float w = text_width(f, size, title);
		if (w > 380)
			size *= 380 / w;
		float tx = 200 + ui_depth(-0.8f);   // floats just in front of the screen
		text(f, tx + 1, ty + 1, size, C2D_Color32(0, 0, 0, a * 5 / 8), C2D_AlignCenter, title);
		text(f, tx, ty, size, theme_alpha(th->game_title, a), C2D_AlignCenter, title);
		char dd[64];
		sub_line(sel, dd, sizeof(dd));
		if (dd[0])
			text(NULL, tx, ty + 36, 0.45f, theme_alpha(th->game_sub, a), C2D_AlignCenter, dd);
	}
	if (!n && open_series < 0 && g_cfg.library_view == V_FAV) {
		// no favourites yet: a big star in the empty carousel
		ui_star(200 + ui_depth(-1.0f), 92, 46, ui_bar_color(UI_COL_STAR), theme_alpha(th->menu_bg, 255), 0.5f);
		text(title_font, 200, 172, 0.8f, th->game_title, C2D_AlignCenter, "No favourites yet");
	}
	if (jump_frames > 0)
		draw_jump(jump_frames > 12 ? 255 : jump_frames * 255 / 12);
	// the view, while it isn't A-Z
	if (g_cfg.library_view != V_AZ && open_series < 0 && !slow_mode_notice(false)) {
		float w = text_width(NULL, 0.4f, view_names[g_cfg.library_view]);
		text(NULL, 392, 8, 0.4f, th->game_sub, C2D_AlignRight, view_names[g_cfg.library_view]);
		if (g_cfg.library_view == V_FAV)
			ui_star(392 - w - 9, 15, 5.5f, ui_bar_color(UI_COL_STAR), 0, 0.6f);
	}
	ui_notice(notice, notice_frames);

	// fast mode off (PC emulator or no Luma3DS): say so on the first screen
	if (slow_mode_notice(false)) {
		// two lines: split the sentence at the comma or colon
		char l1[96], l2[96];
		const char *m = slow_mode_notice(false), *cut = strchr(m, ',');
		if (!cut)
			cut = strchr(m, ':');
		snprintf(l1, sizeof(l1), "%.*s", cut ? (int)(cut - m) + 1 : (int)strlen(m), m);
		snprintf(l2, sizeof(l2), "%s", cut ? cut + 2 : "");
		C2D_DrawRectSolid(0, 0, 0.6f, 400, 32, th->slow_bg);
		ui_text_ex(NULL, 8, 2, 0.4f, th->slow_text, 0, ui_fit(l1, 0.4f, 384));
		ui_text_ex(NULL, 8, 16, 0.4f, th->slow_text, 0, ui_fit(l2, 0.4f, 384));
	}

	// which build this is, small in the corner
	text(NULL, 396, 226, 0.35f, C2D_Color32(150, 160, 200, 160), C2D_AlignRight, "v" PSX3D_VERSION);

	// fade from/to black (open, launch)
	float f = fade;
	if (launch_t >= 0)
		f = launch_t > 0.4f ? (launch_t - 0.4f) / 0.6f : 0;
	if (f > 0.001f)
		C2D_DrawRectSolid(0, 0, 0.9f, 400, 240, C2D_Color32(0, 0, 0, (u8)(255 * (f > 1 ? 1 : f))));
}

// The same backdrop for the main menu and its screens; the grid rolls on the
// system clock there.
void libui_draw_backdrop(void)
{
	if (bg_ok)
		C2D_DrawImageAt(bg_img, ui_depth(3.0f), 0, 0.1f, NULL, 1.0f, 1.0f);
	double t = svcGetSystemTick() / (double)SYSCLOCK_ARM11;
	draw_grid((float)(t - 3600.0 * (u32)(t / 3600.0)));
}

static flist_t lst;
static flist_t top_lst;   // the top-level list as it was when a series was opened

static bool is_heading(int r, void *ud)
{
	(void)ud;
	return row_entry(r) < 0;
}

static const char *game_label(int r, void *ud)
{
	(void)ud;
	int e = row_entry(r);
	if (e < 0)
		return hdr[-1 - e].label;
	// room left of the value and the star, or the full row
	return ui_fit(entry_title(e), 0.5f, entry_series(e) >= 0 ? 238 : favourite(entry_game(e)) ? 256 : 276);
}

// "Today", "3 days ago", "Oct 13"
static void ago(char *out, size_t len, u32 when)
{
	time_t now = time(NULL), t = when;
	struct tm a = *localtime(&now), b = *localtime(&t);
	long days = (long)(mktime(&(struct tm){ .tm_year = a.tm_year, .tm_mon = a.tm_mon, .tm_mday = a.tm_mday, .tm_hour = 12 }) -
		mktime(&(struct tm){ .tm_year = b.tm_year, .tm_mon = b.tm_mon, .tm_mday = b.tm_mday, .tm_hour = 12 })) / 86400;
	static const char *const mon[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	if (days <= 0) snprintf(out, len, "Today");
	else if (days == 1) snprintf(out, len, "Yesterday");
	else if (days < 7) snprintf(out, len, "%ld days ago", days);
	else snprintf(out, len, "%s %d", mon[b.tm_mon], b.tm_mday);
}

static const char *game_value(int r, void *ud)
{
	(void)ud;
	static char v[24];
	int e = row_entry(r);
	if (e < 0)
		return NULL;
	int s = entry_series(e);
	if (s >= 0) {
		snprintf(v, sizeof(v), "%d  >", library_series_size(s));
		return v;
	}
	const char *k = save_key(entry_game(e));
	if (g_cfg.library_view == V_RECENT && playlog_last(k)) {
		ago(v, sizeof(v), playlog_last(k));
		return v;
	}
	u32 secs = playlog_seconds(k);
	if (g_cfg.library_view == V_MOST && playlog_last(k)) {
		snprintf(v, sizeof(v), secs >= 3600 ? "%lu h" : "%lu min", (unsigned long)(secs >= 3600 ? secs / 3600 : secs / 60));
		return v;
	}
	return NULL;
}

// The touch buttons: what each button does here; unusable ones are left out.
static int library_bar(ui_button_t *b)
{
	int n = 0;
	b[n++] = (ui_button_t){ KEY_B, open_series >= 0 ? "Back" : "Menu", 0, false };
	if (open_series < 0)
		b[n++] = (ui_button_t){ KEY_X, view_names[g_cfg.library_view], UI_ICON_SORT, false };
	if (nview && entry_series(sel) < 0)
		b[n++] = (ui_button_t){ KEY_Y, NULL, UI_ICON_STAR, favourite(entry_game(sel)) };
	b[n++] = (ui_button_t){ KEY_SELECT, NULL, UI_ICON_RESCAN, false };
	if (nview)
		b[n++] = (ui_button_t){ KEY_A, entry_series(sel) >= 0 ? "Open" : "Play", 0, false };
	return n;
}

static void draw_bottom(void *ud)
{
	(void)ud;
	int n = nrows();
	const theme_t *th = theme();
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, th->menu_bg);
	if (!nview && open_series < 0 && g_cfg.library_view == V_FAV) {
		ui_menu_header("Favourites", "None yet");
		ui_text_wrap(16, 48, 0.45f, th->help_text, 288, "Pick a game in A-Z and press Y (or tap the star).");
	}
	int srow = nview ? row_of(sel) : -1;
	flist_draw_sections(&lst, n, srow, 0, 4, 314, ROW_H, ROWS, game_label, game_value, NULL, *accent(sel), is_heading);
	// favourites: a star at the end of the row, left of any value
	for (int r = (int)floorf(lst.scroll); r < n && r <= lst.scroll + ROWS; r++) {
		int e = row_entry(r);
		if (e < 0 || entry_series(e) >= 0 || !favourite(entry_game(e)))
			continue;
		float ry = 4 + (r - lst.scroll) * ROW_H;
		if (ry < 2 || ry + ROW_H > 4 + ROWS * ROW_H + 2)
			continue;
		const char *v = game_value(r, NULL);
		float vw = v ? text_width(NULL, 0.45f, v) + 8 : 0;
		ui_star(314 - 14 - vw, ry + ROW_H / 2.0f, 5.5f, ui_bar_color(UI_COL_STAR), 0, 0.6f);
	}
	// scroll indicator
	if (n > ROWS) {
		float h = 200.0f * ROWS / n, y = 4 + 200.0f * lst.scroll / n;
		C2D_DrawRectSolid(315, y, 0.3f, 3, h, th->scrollbar);
	}
	ui_button_t bar[UI_BAR_MAX];
	ui_bar_draw(bar, library_bar(bar));
}

static bool repeat(u32 down, u32 held, u32 key, int *c)
{
	if (down & key) { *c = 0; return true; }
	if (!(held & key)) return false;
	++*c;
	return *c > 18 && (*c - 18) % 4 == 0;
}

static void clamp_list(void)
{
	int n = nview;
	if (sel < list_top) list_top = sel;
	if (sel >= list_top + ROWS) list_top = sel - ROWS + 1;
	if (list_top > n - ROWS) list_top = n - ROWS;
	if (list_top < 0) list_top = 0;
}

// ---------------------------------------------------------------- settings
// Global settings, reachable from the library with X (the defaults for every
// game; a game's own screen mode, controls and rewind live in its menu).

static void settings_draw_top(void *ud)
{
	// The library loop is paused here; finish its pending artwork uploads and
	// show the title even when remote settings opened before its first fade.
	covers_pump();
	title_t = 1;
	draw_top(ud);
}
static void settings_run(void) { settings_ui_run(false, settings_draw_top); }

// select a game by title (to come back to the last one played): its first
// row (in Recently played when it is there), else inside its series
void libui_select_title(const char *title)
{
	for (int i = 0; i < library_count(); i++)
		if (!strcmp(library_get(i)->title, title)) {
			int s = library_game_series(i);
			open_series = -1;
			build_view();
			int top_n = nrows(), found = -1;
			for (int e = 0; e < nview && found < 0; e++)
				if (view[e] == i)
					found = e;
			if (found < 0 && s >= 0)
				for (int e = 0; e < nview && found < 0; e++)
					if (view[e] == -(s + 1))
						found = e;
			sel = top_sel = found < 0 ? 0 : found;
			int top_row = row_of(sel);
			if (found >= 0 && view[found] < 0) {   // inside its series
				open_series = s;
				build_view();
				for (int e = 0; e < nview; e++)
					if (view[e] == i)
						sel = e;
			}
			anim = sel;
			title_for = sel;
			flist_snap(&lst, row_of(sel), nrows(), ROWS);
			if (open_series >= 0)   // going back out lands on the series without scrolling
				flist_snap(&top_lst, top_row, top_n, ROWS);
			clamp_list();
			return;
		}
}

// switch views without any scrolling: opening a series starts at its top,
// going back puts the list exactly where it was
static void enter_view(int series_idx, int new_sel)
{
	bool back = series_idx < 0 && open_series >= 0;
	if (series_idx >= 0 && open_series < 0)
		top_lst = lst;
	open_series = series_idx;
	build_view();
	sel = new_sel < nview ? new_sel : nview - 1;
	if (sel < 0)
		sel = 0;
	anim = sel;
	anim_v = 0;
	title_for = -1;
	if (back) {
		lst = top_lst;
		lst.hl = row_of(sel);
		lst.hl_v = lst.scroll_v = 0;
	} else {
		flist_snap(&lst, nview ? row_of(sel) : 0, nrows(), ROWS);
	}
	clamp_list();
}

// the same list again (sort, favourite), the selection kept on its game
static void rebuild_keep(int new_sel)
{
	int old_rows = nrows();
	build_view();
	sel = new_sel < nview ? new_sel : nview - 1;
	if (sel < 0)
		sel = 0;
	if (nrows() != old_rows)
		flist_snap(&lst, nview ? row_of(sel) : 0, nrows(), ROWS);
	anim = sel;
	anim_v = 0;
	clamp_list();
}

static void say(const char *s)
{
	snprintf(notice, sizeof(notice), "%s", s);
	notice_frames = 120;
}

// R: the next letter; L: the start of this letter, or the previous one
static int letter_jump(int dir)
{
	if (az_start < 0 || az_start >= nview)
		return sel;
	if (sel < az_start)
		return dir > 0 ? az_start : sel;
	char c = letter_of(sel);
	int e = sel;
	if (dir > 0) {
		while (e < nview && letter_of(e) == c) e++;
		return e < nview ? e : sel;
	}
	while (e > az_start && letter_of(e - 1) == c) e--;
	if (e < sel)
		return e;
	if (e == az_start)
		return e;
	c = letter_of(e - 1);
	e--;
	while (e > az_start && letter_of(e - 1) == c) e--;
	return e;
}

// Returns 1 with path filled to play a game, 2 to open the folder browser,
// 4 back to the main menu, 0 to quit.
// the game picked last time libui_run returned 1
const game_t *libui_picked(void)
{
	return library_get(entry_game(sel));
}

int libui_run(char *out_path, size_t len)
{
	if (open_series >= library_series_count())
		open_series = -1;
	build_view();
	int n = nview;
	if (sel >= n) sel = n - 1;
	if (sel < 0) sel = 0;
	clamp_list();

	music_play(true);
	ui_wants_3d = true;
	fade = 1;
	covers_resume();
	bool home_pending = false;
	static u32 frame_no __attribute__((unused));
	while (aptMainLoop()) {
		if (dev_tick(false)) {
			music_play(false);
			ui_wants_3d = false;
			return 3;
		}
		int screen = dev_ui_take();
		if (screen == DEV_UI_SETTINGS) {
			settings_run();
			if (settings_ui_take_folders()) { ui_wants_3d = false; return 2; }
			continue;
		}
		input_poll();
		ui_button_t bar[UI_BAR_MAX];
		u32 h = input_keys_held(), k = ui_bar_input(bar, library_bar(bar), input_keys_down());
		n = nview;
		int before = sel;

		// HOME: held back while the covers use the network. When pressed,
		// stop them (the UI keeps running meanwhile), then go HOME ourselves.
		aptSetHomeAllowed(!home_pending && !covers_busy());
		if (aptCheckHomePressRejected() && !home_pending) {
			log_printf("home: pressed while covers busy\n");
			home_pending = true;
		}
		if (home_pending && covers_stop()) {
			log_printf("home: network closed, jumping\n");
			home_pending = false;
			aptSetHomeAllowed(true);
			aptJumpToHomeMenu();
			log_printf("home: back\n");
			covers_resume();
			continue;
		}
		// Retained to preserve the validated Old 3DS executable layout.
		frame_no++;

		if (k & KEY_START) {
			aptSetHomeAllowed(true);
			music_play(false);
			ui_wants_3d = false;
			return 0;
		}
		if ((k & KEY_X) && open_series < 0) {
			// Sort: A-Z, Recently played, Most played, Favourites
			int game = n ? entry_game(sel) : -1;
			g_cfg.library_view = (g_cfg.library_view + 1) % V_COUNT;
			config_save();
			build_view();
			int to = 0;
			for (int e = 0; e < nview; e++)
				if (view[e] == game) { to = e; break; }
			enter_view(-1, to);
			n = nview;
			char msg[48];
			snprintf(msg, sizeof(msg), "Sorted: %s", g_cfg.library_view == V_RECENT ? "Recently played" : view_names[g_cfg.library_view]);
			say(msg);
			sfx_play(SFX_SELECT);
			before = sel;
		}
		if ((k & KEY_Y) && n && entry_series(sel) < 0) {
			bool on = playlog_toggle_favourite(save_key(entry_game(sel)));
			say(on ? "Added to favourites" : "Removed from favourites");
			sfx_play(on ? SFX_SELECT : SFX_BACK);
			rebuild_keep(sel);   // the Favourites view drops the row; the cursor stays put
			n = nview;
			before = sel;
		}
		if ((k & KEY_B) && open_series < 0) {
			// the main menu (an open series closes first, below)
			aptSetHomeAllowed(true);
			sfx_play(SFX_BACK);
			ui_wants_3d = false;
			return 4;
		}
		if (k & KEY_SELECT) {
			covers_reset();
			library_load(g_cfg.games_dir, true);
			enter_view(-1, 0);
			n = nview;
			char msg[96];
			int omitted = library_omitted();
			if (omitted)
				snprintf(msg, sizeof(msg), "Library updated: %d games, %d skipped", library_count(), omitted);
			else
				snprintf(msg, sizeof(msg), "Library updated: %d games", library_count());
			say(msg);
			sfx_play(SFX_SELECT);
		}
		if (n) {
			if (repeat(k, h, KEY_DRIGHT | KEY_CPAD_RIGHT, &rep_r) || repeat(k, h, KEY_DDOWN | KEY_CPAD_DOWN, &rep_d))
				sel = sel + 1 < n ? sel + 1 : sel;
			if (repeat(k, h, KEY_DLEFT | KEY_CPAD_LEFT, &rep_l) || repeat(k, h, KEY_DUP | KEY_CPAD_UP, &rep_u))
				sel = sel > 0 ? sel - 1 : 0;
			// L/R: a letter in A-Z and Favourites, a page elsewhere
			bool letters = az_start >= 0 && open_series < 0;
			if (repeat(k, h, KEY_R, &rep_pr)) {
				if (letters) { sel = letter_jump(1); jump_frames = 48; }
				else { sel += ROWS; if (sel >= n) sel = n - 1; }
			}
			if (repeat(k, h, KEY_L, &rep_pl)) {
				if (letters) { sel = letter_jump(-1); jump_frames = 48; }
				else { sel -= ROWS; if (sel < 0) sel = 0; }
			}

			touchPosition tp;
			if (k & KEY_TOUCH) {
				hidTouchRead(&tp);
				int r = flist_row_at(&lst, 4, ROW_H, tp.py);
				// (0,0) is what a touch with no position reads as: ignore it
				scrubbing = letters && tp.px >= 300 && tp.py >= 4 && tp.py < 4 + ROWS * ROW_H;
				if (!scrubbing && (tp.px || tp.py) && tp.py >= 4 && tp.py < 4 + ROWS * ROW_H &&
				    r >= 0 && r < nrows() && row_entry(r) >= 0) {
					if (row_entry(r) == sel)
						k |= KEY_A;     // tap the selected game again to play
					sel = row_entry(r);
				}
			}
			if (scrubbing && (h & KEY_TOUCH)) {
				// dragging along the right edge runs through A-Z
				hidTouchRead(&tp);
				float f = (tp.py - 4) / (float)(ROWS * ROW_H);
				f = f < 0 ? 0 : f > 0.999f ? 0.999f : f;
				sel = az_start + (int)(f * (n - az_start));
				jump_frames = 48;
			} else
				scrubbing = false;
			clamp_list();

			if (sel != before)
				sfx_play(SFX_MOVE);

			if ((k & KEY_B) && open_series >= 0) {
				sfx_play(SFX_BACK);
				enter_view(-1, top_sel);
				continue;
			}
			if ((k & KEY_A) && entry_series(sel) >= 0) {
				sfx_play(SFX_SELECT);
				top_sel = sel;
				enter_view(entry_series(sel), 0);
				continue;
			}
			if (k & KEY_A) {
				const game_t *g = library_get(entry_game(sel));
				if (!library_launch_path(g, out_path, len)) {
					// do not start with just disc 1
					say("Could not write the disc list. Is the SD card full?");
					sfx_play(SFX_BACK);
					continue;
				}
				snprintf(g_cfg.last_title, sizeof(g_cfg.last_title), "%s", g->title);
				config_save();
				covers_stop_wait();   // no network during the game
				// launch sound, music fades out while it plays
				sfx_play(SFX_LAUNCH);
				for (int f = 0; f <= 36 && aptMainLoop(); f++) {
					launch_t = f / 36.0f;
					clock_s += 1.0f / 60.0f;
					music_update();
					C2D_TextBufClear(tbuf);
					ui_begin();
					video_present_vsync(false, draw_top, draw_bottom, NULL);
				}
				launch_t = -1;
				fade = 1;
				aptSetHomeAllowed(true);
				music_play(false);
				ui_wants_3d = false;
				return 1;
			}
		}

		music_update();
		flist_update(&lst, n ? row_of(sel) : 0, nrows(), ROWS);
		// springs: carousel glides to the selection, the new cover pops
		const float dt = 1.0f / 60.0f;
		clock_s += dt;
		anim_v += ((sel - anim) * 180.0f - anim_v * 22.0f) * dt;
		anim += anim_v * dt;
		if (fabsf(sel - anim) < 0.002f && fabsf(anim_v) < 0.01f) {
			anim = sel;
			anim_v = 0;
		}
		if (title_for != sel) {
			title_for = sel;
			title_t = 0;
			pop = 0.88f;
			pop_v = 0;
		}
		pop_v += ((1.0f - pop) * 260.0f - pop_v * 14.0f) * dt;
		pop += pop_v * dt;
		if (title_t < 1) title_t = fminf(1, title_t + dt * 5);
		if (fade > 0) fade = fmaxf(0, fade - dt * 3);
		if (notice_frames > 0) notice_frames--;
		if (jump_frames > 0 && !scrubbing) jump_frames--;
		// a downloaded preset pack: checked and swapped in here, used from the next game
		if (presets_update_poll()) say("Game settings updated");
		// the selection first, then outward: what's on screen loads first
		covers_focus(entry_game(sel));
		for (int d = 0; d <= 4; d++) {
			if (sel + d < n) covers_get(entry_game(sel + d));
			if (d && sel - d >= 0) covers_get(entry_game(sel - d));
		}

		covers_pump();
		C2D_TextBufClear(tbuf);
		ui_begin();
		video_present_vsync(false, draw_top, draw_bottom, NULL);
	}
	return 0;
}

C2D_Font libui_font(void)
{
	return title_font;
}
