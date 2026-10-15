// The game library screen. Top: synthwave backdrop, a box art carousel and
// the game's clean title. Bottom: a quiet alphabetical list (touch-friendly).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "psx3d.h"

#define ROWS 9
#define ROW_H 24

static C2D_Font title_font;
static C3D_Tex bg_tex;
static Tex3DS_Texture bg_t3x;
static C2D_Image bg_img;
static bool bg_ok;
static C2D_TextBuf tbuf;

static int sel, list_top;
static float anim;                 // carousel position, eases toward sel
static int rep_l, rep_r, rep_u, rep_d, rep_pl, rep_pr;

// PS-era accent colours, one per game in turn
static const u32 *accent(int i)
{
	static u32 c[4];
	c[0] = C2D_Color32(230, 30, 50, 255);
	c[1] = C2D_Color32(250, 205, 20, 255);
	c[2] = C2D_Color32(30, 190, 120, 255);
	c[3] = C2D_Color32(40, 110, 230, 255);
	return &c[i & 3];
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
static void draw_cover(int i, float cx, float cy, float size, float dim)
{
	const game_t *g = library_get(i);
	if (!g)
		return;
	float x = cx - size / 2, y = cy - size / 2;
	// drop shadow
	C2D_DrawRectSolid(x + 3, y + 4, 0.4f, size, size, C2D_Color32(0, 0, 0, 120));
	C3D_Tex *t = covers_get(i);
	if (t) {
		Tex3DS_SubTexture st = { COVER_SIZE, COVER_SIZE, 0.0f, 1.0f, 1.0f, 0.0f };
		C2D_Image img = { t, &st };
		C2D_ImageTint tint;
		C2D_PlainImageTint(&tint, C2D_Color32(0, 0, 0, 255), dim);
		C2D_DrawImageAt(img, x, y, 0.5f, dim > 0 ? &tint : NULL, size / COVER_SIZE, size / COVER_SIZE);
	} else {
		u32 c = *accent(i);
		C2D_DrawRectangle(x, y, 0.5f, size, size, C2D_Color32(20, 26, 60, 255), C2D_Color32(20, 26, 60, 255),
			C2D_Color32(8, 10, 28, 255), C2D_Color32(8, 10, 28, 255));
		C2D_DrawRectSolid(x, y + size - 6, 0.5f, size, 6, c);
		char initial[2] = { g->title[0], 0 };
		float s = size / 60.0f;
		C2D_Font f = title_font ? title_font : NULL;
		text(f, cx, cy - 14 * s, s, C2D_Color32(220, 225, 245, 255), C2D_AlignCenter, initial);
		if (dim > 0)
			C2D_DrawRectSolid(x, y, 0.55f, size, size, C2D_Color32(0, 0, 0, (u8)(dim * 255)));
	}
}

static void draw_top(void *ud)
{
	(void)ud;
	int n = library_count();
	if (bg_ok)
		C2D_DrawImageAt(bg_img, 0, 0, 0.1f, NULL, 1.0f, 1.0f);

	// carousel: neighbours first so the selection sits on top
	float spacing = 116.0f;
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
			float cx = 200 + d * spacing;
			float dim = ad < 1 ? 0.55f * ad : 0.55f + 0.1f * (ad - 1);
			if (dim > 0.85f) dim = 0.85f;
			draw_cover(i, cx, 96, size, dim);
		}

	const game_t *g = library_get(sel);
	if (!g)
		return;
	// title: brand font, shrunk to fit
	C2D_Font f = title_font;
	float size = 0.9f;
	float w = text_width(f, size, g->title);
	if (w > 380)
		size *= 380 / w;
	text(f, 201, 179, size, C2D_Color32(0, 0, 0, 160), C2D_AlignCenter, g->title);
	text(f, 200, 178, size, C2D_Color32(255, 255, 255, 255), C2D_AlignCenter, g->title);
	if (g->ndiscs > 1) {
		char d[32];
		snprintf(d, sizeof(d), "%d discs", g->ndiscs);
		text(NULL, 200, 214, 0.45f, C2D_Color32(170, 185, 230, 255), C2D_AlignCenter, d);
	}
}

static void draw_bottom(void *ud)
{
	(void)ud;
	int n = library_count();
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, C2D_Color32(6, 9, 24, 255));
	for (int r = 0; r < ROWS; r++) {
		int i = list_top + r;
		if (i >= n)
			break;
		const game_t *g = library_get(i);
		float y = 4 + r * ROW_H;
		if (i == sel) {
			C2D_DrawRectSolid(0, y, 0.2f, 320, ROW_H, C2D_Color32(28, 38, 86, 255));
			C2D_DrawRectSolid(0, y, 0.3f, 4, ROW_H, *accent(i));
		}
		char line[96];
		snprintf(line, sizeof(line), "%.40s", g->title);
		text(NULL, 12, y + 4, 0.5f, i == sel ? C2D_Color32(255, 255, 255, 255) : C2D_Color32(150, 160, 190, 255), 0, line);
	}
	// scroll indicator
	if (n > ROWS) {
		float h = 216.0f * ROWS / n, y = 4 + 216.0f * list_top / n;
		C2D_DrawRectSolid(314, y, 0.3f, 3, h, C2D_Color32(60, 80, 140, 255));
	}
	text(NULL, 8, 222, 0.4f, C2D_Color32(90, 100, 135, 255), 0,
		"A play    Y folders    SELECT rescan    START quit");
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
	int n = library_count();
	if (sel < list_top) list_top = sel;
	if (sel >= list_top + ROWS) list_top = sel - ROWS + 1;
	if (list_top > n - ROWS) list_top = n - ROWS;
	if (list_top < 0) list_top = 0;
}

// select a game by title (to come back to the last one played)
void libui_select_title(const char *title)
{
	for (int i = 0; i < library_count(); i++)
		if (!strcmp(library_get(i)->title, title)) {
			sel = i;
			anim = i;
			clamp_list();
			return;
		}
}

// Returns 1 with path filled to play a game, 2 to open the folder browser,
// 0 to quit.
int libui_run(char *out_path, size_t len)
{
	int n = library_count();
	if (sel >= n) sel = n - 1;
	if (sel < 0) sel = 0;
	clamp_list();

	while (aptMainLoop()) {
		input_poll();
		u32 k = input_keys_down(), h = input_keys_held();
		n = library_count();

		if (k & KEY_START)
			return 0;
		if (k & KEY_Y)
			return 2;
		if (k & KEY_SELECT) {
			covers_reset();
			library_load(g_cfg.games_dir, true);
			n = library_count();
			if (sel >= n) sel = n - 1;
			if (sel < 0) sel = 0;
		}
		if (n) {
			if (repeat(k, h, KEY_DRIGHT | KEY_CPAD_RIGHT, &rep_r) || repeat(k, h, KEY_DDOWN | KEY_CPAD_DOWN, &rep_d))
				sel = sel + 1 < n ? sel + 1 : sel;
			if (repeat(k, h, KEY_DLEFT | KEY_CPAD_LEFT, &rep_l) || repeat(k, h, KEY_DUP | KEY_CPAD_UP, &rep_u))
				sel = sel > 0 ? sel - 1 : 0;
			if (repeat(k, h, KEY_R, &rep_pr)) { sel += ROWS; if (sel >= n) sel = n - 1; }
			if (repeat(k, h, KEY_L, &rep_pl)) { sel -= ROWS; if (sel < 0) sel = 0; }

			if (k & KEY_TOUCH) {
				touchPosition tp;
				hidTouchRead(&tp);
				int r = (tp.py - 4) / ROW_H;
				if (tp.py < 220 && r >= 0 && r < ROWS && list_top + r < n) {
					if (list_top + r == sel)
						k |= KEY_A;     // tap the selected game again to play
					sel = list_top + r;
				}
			}
			clamp_list();

			if (k & KEY_A) {
				const game_t *g = library_get(sel);
				library_launch_path(g, out_path, len);
				snprintf(g_cfg.last_title, sizeof(g_cfg.last_title), "%s", g->title);
				config_save();
				return 1;
			}
		}

		// ease the carousel toward the selection
		anim += (sel - anim) * 0.25f;
		if (fabsf(sel - anim) < 0.01f)
			anim = sel;
		// the selection first, then outward: what's on screen loads first
		covers_focus(sel);
		for (int d = 0; d <= 4; d++) {
			if (sel + d < n) covers_get(sel + d);
			if (d && sel - d >= 0) covers_get(sel - d);
		}

		C2D_TextBufClear(tbuf);
		ui_begin();
		video_present_vsync(false, draw_top, draw_bottom, NULL);
	}
	return 0;
}
