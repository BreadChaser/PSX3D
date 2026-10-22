// The main menu PSX3D opens to (layout "A. Tiles", MAIN-MENU-DESIGN.md and
// round 3 in LIBRARY-DESIGN.md section 8 of the UI mock). Bottom: big touch
// tiles (Continue or Library as the hero, Library | Play together with its
// Host and Join buttons, Share PSX3D | Settings); no prompt line. Top: the
// library backdrop with a big picture of the selected tile, its name and one
// short line. Colours come from the menu theme.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "psx3d.h"
#include "devtools.h"

enum { E_CONTINUE, E_LIBRARY, E_HOST, E_JOIN, E_SHARE, E_SETTINGS };
enum { K_HERO, K_MID, K_SMALL, K_SUB };

typedef struct { int id, kind; float x, y, w, h; } tile_t;

static u32 accent(int i, u8 a)
{
	return theme_alpha(theme()->accent[i & 3], a);
}
#define MENU_BG (theme()->menu_bg)

// ---------------------------------------------------------------- state

static bool have_games;
static bool cont_ok;                 // a last game with a Continue auto-save
static u32 cont_saved;
static C3D_Tex cont_tex;
static bool cont_tex_ok;
static char cont_path[PATH_LEN];

static int sel_id = -1;              // selected entry; the first one at start
static float clock_s, fade = 1, pop = 1, pop_v, title_t = 1;
static int title_for = -1;
static C2D_TextBuf mbuf;             // measuring only

static tile_t tiles[6];
static int ntiles;
static tile_t group;                 // the Play together frame around Host and Join
static bool have_group;
static int picks[3], npicks;         // the Library tile's covers, chosen each start

// three different games at random for the Library tile
static void pick_covers(void)
{
	int n = library_count();
	npicks = 0;
	if (n <= 0)
		return;
	u32 seed = (u32)svcGetSystemTick();   // a small generator of our own: no new library code
	while (npicks < 3 && npicks < n) {
		seed = seed * 1664525u + 1013904223u;
		int g = (int)((seed >> 8) % (u32)n), dup = 0;
		for (int i = 0; i < npicks; i++)
			dup |= picks[i] == g;
		if (!dup)
			picks[npicks++] = g;
	}
}

static float text_w(C2D_Font f, float size, const char *s)
{
	C2D_Text t;
	float w = 0;
	if (!mbuf && !(mbuf = C2D_TextBufNew(256)))
		return 0;
	C2D_TextBufClear(mbuf);
	if (!C2D_TextFontParse(&t, f, mbuf, s))
		return 0;
	C2D_TextGetDimensions(&t, size, size, &w, NULL);
	return w;
}

static const char *cont_title(void)
{
	return g_cfg.last_game[0] ? g_cfg.last_game : g_cfg.last_game_state;
}

// The game the main menu would continue: its auto-save, picture and a path
// that still exists (the disc may have moved; then the library's copy).
static void cont_probe(void)
{
	if (cont_tex_ok)
		C3D_TexDelete(&cont_tex);
	cont_tex_ok = cont_ok = false;
	if (!g_cfg.last_game_state[0] || !g_cfg.last_game_path[0])
		return;
	char prefix[PATH_LEN];
	snprintf(prefix, sizeof(prefix), "%s/%.*s", PSX3D_STATE_DIR, 400, g_cfg.last_game_state);
	struct stat st;
	snprintf(cont_path, sizeof(cont_path), "%s", g_cfg.last_game_path);
	if (stat(cont_path, &st)) {
		cont_path[0] = 0;
		for (int i = 0; i < library_count() && !cont_path[0]; i++) {
			const game_t *g = library_get(i);
			if (g_cfg.last_game[0] && !strcmp(g->title, g_cfg.last_game) &&
			    !library_launch_path(g, cont_path, sizeof(cont_path)))
				cont_path[0] = 0;
		}
		if (!cont_path[0])
			return;
	}
	cont_ok = states_auto_peek(prefix, &cont_saved, &cont_tex, &cont_tex_ok);
}

// Play together is a frame with two buttons, Host and Join (owner: no sub-menu).
static void together_at(float x, float y, float w, float h)
{
	group = (tile_t){ -1, K_MID, x, y, w, h };
	have_group = true;
	float bw = (w - 18) / 2;
	tiles[ntiles++] = (tile_t){ E_HOST, K_SUB, x + 6, y + 30, bw, h - 36 };
	tiles[ntiles++] = (tile_t){ E_JOIN, K_SUB, x + 12 + bw, y + 30, bw, h - 36 };
}

static void layout(void)
{
	ntiles = 0;
	have_group = false;
	if (!have_games) {
		// a friend who just installed PSX3D: joining needs no games
		tiles[ntiles++] = (tile_t){ E_JOIN, K_HERO, 8, 8, 304, 94 };
		tiles[ntiles++] = (tile_t){ E_LIBRARY, K_MID, 8, 108, 304, 76 };
	} else if (cont_ok) {
		tiles[ntiles++] = (tile_t){ E_CONTINUE, K_HERO, 8, 8, 304, 94 };
		tiles[ntiles++] = (tile_t){ E_LIBRARY, K_MID, 8, 108, 149, 76 };
		together_at(163, 108, 149, 76);
	} else {
		tiles[ntiles++] = (tile_t){ E_LIBRARY, K_HERO, 8, 8, 304, 94 };
		together_at(8, 108, 304, 76);
	}
	tiles[ntiles++] = (tile_t){ E_SHARE, K_SMALL, 8, 190, 149, 42 };
	tiles[ntiles++] = (tile_t){ E_SETTINGS, K_SMALL, 163, 190, 149, 42 };
}

static int sel_index(void)
{
	for (int i = 0; i < ntiles; i++)
		if (tiles[i].id == sel_id)
			return i;
	return 0;
}

static const char *label(int id)
{
	switch (id) {
	case E_CONTINUE: return "Continue";
	case E_LIBRARY: return "Library";
	case E_HOST: return "Host a game";
	case E_JOIN: return "Join a game";
	case E_SHARE: return "Share PSX3D";
	}
	return "Settings";
}

static const char *sub(int id, bool shortform)
{
	static char buf[32];
	switch (id) {
	case E_LIBRARY:
		if (!have_games) return "No games yet";
		snprintf(buf, sizeof(buf), "%d games", library_count());
		return buf;
	case E_HOST:
		return g_new3ds ? "Friends join, with or without the game" : "Friends with the same game join";
	case E_JOIN:
		if (!have_games) return shortform ? "No games needed" : "Join a friend's game";
		return "Play in a friend's game";
	case E_SHARE: return "Send it to a friend's 3DS";
	case E_SETTINGS: return "For all games";
	}
	return "";
}

// ---------------------------------------------------------------- shapes

static void tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c, float z)
{
	C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, z);
}

static void ring(float cx, float cy, float r, u32 c, float w, float z)
{
	const int n = 28;
	for (int i = 0; i < n; i++) {
		float a0 = i * 2 * M_PI / n, a1 = (i + 1) * 2 * M_PI / n;
		C2D_DrawLine(cx + r * cosf(a0), cy + r * sinf(a0), c, cx + r * cosf(a1), cy + r * sinf(a1), c, w, z);
	}
}

static u32 with_alpha(u32 c, u8 a) { return (c & 0x00ffffff) | ((u32)a << 24); }

static void icon_play(float cx, float cy, float r, u32 c, float z)
{
	ring(cx, cy, r, c, fmaxf(1.5f, r / 9), z);
	tri(cx - r * 0.3f, cy - r * 0.45f, cx - r * 0.3f, cy + r * 0.45f, cx + r * 0.5f, cy, c, z);
}

static void icon_library(float cx, float cy, float r, u32 c, float z)
{
	for (int k = 2; k >= 0; k--) {
		float w = r * 1.2f, x = cx - w / 2 + (k - 1) * r * 0.42f, y = cy - w / 2 - (k - 1) * r * 0.12f;
		float zz = z + (2 - k) * 0.002f;
		C2D_DrawRectSolid(x, y, zz, w, w, k ? with_alpha(c, (u8)(255 - k * 70)) : c);
		C2D_DrawRectSolid(x + 2, y + 2, zz + 0.0005f, w - 4, w - 4, k ? TH_CARD_BOTTOM : TH_CARD_TOP);
		if (!k)
			C2D_DrawRectSolid(x, y + w - 3, zz + 0.001f, w, 3, accent(0, 255));
	}
}

static void icon_together(float cx, float cy, float r, u32 c, float z)
{
	float w = r * 0.8f, h = r * 0.55f;
	for (int sx = -1; sx <= 1; sx += 2) {
		float x = cx + sx * r * 0.55f - w / 2;
		C2D_DrawRectSolid(x, cy - h, z, w, h, c);
		C2D_DrawRectSolid(x + 2, cy - h + 2, z + 0.001f, w - 4, h - 4, theme()->value);
		C2D_DrawRectSolid(x, cy + 1, z, w, h * 0.9f, c);
	}
	float p = fmodf(clock_s * 0.8f, 1);
	C2D_DrawCircleSolid(cx - r * 0.25f + r * 0.5f * p, cy - h - sinf(p * M_PI) * r * 0.35f, z + 0.002f,
		fmaxf(1.5f, r * 0.11f), accent((int)(clock_s * 0.8f), 255));
}

static void icon_share(float cx, float cy, float r, u32 c, float z)
{
	float px[3] = { cx + r * 0.55f, cx - r * 0.6f, cx + r * 0.55f }, py[3] = { cy - r * 0.6f, cy, cy + r * 0.6f };
	float w = fmaxf(1.5f, r / 9);
	C2D_DrawLine(px[0], py[0], c, px[1], py[1], c, w, z);
	C2D_DrawLine(px[2], py[2], c, px[1], py[1], c, w, z);
	for (int i = 0; i < 3; i++)
		C2D_DrawCircleSolid(px[i], py[i], z + 0.001f, r * 0.26f, i == 1 ? c : accent(i + 1, 255));
}

static void icon_settings(float cx, float cy, float r, u32 c, u32 hole, float z)
{
	float base = clock_s * 0.4f;
	for (int i = 0; i < 8; i++) {
		float a = base + (i + 1) * M_PI / 4, ca = cosf(a), sa = sinf(a);
		// a tooth: the rectangle (-0.16r, -0.95r)..(0.16r, -0.55r), turned by a
		float qx[4] = { -0.16f * r, 0.16f * r, 0.16f * r, -0.16f * r };
		float qy[4] = { -0.95f * r, -0.95f * r, -0.55f * r, -0.55f * r };
		float X[4], Y[4];
		for (int k = 0; k < 4; k++) {
			X[k] = cx + qx[k] * ca - qy[k] * sa;
			Y[k] = cy + qx[k] * sa + qy[k] * ca;
		}
		tri(X[0], Y[0], X[1], Y[1], X[2], Y[2], c, z);
		tri(X[0], Y[0], X[2], Y[2], X[3], Y[3], c, z);
	}
	C2D_DrawCircleSolid(cx, cy, z, r * 0.68f, c);
	C2D_DrawCircleSolid(cx, cy, z + 0.002f, r * 0.3f, hole);
}

// The Play together picture: two consoles and a link between them.
static void draw_consoles(float cx, float cy, float s, float z)
{
	const theme_t *th = theme();
	u32 body = theme_alpha(th->hl_top, 255), lit = th->value;
	for (int k = -1; k <= 1; k += 2) {
		float x = cx + k * 40 * s;
		C2D_DrawRectSolid(x - 22 * s, cy - 30 * s, z, 44 * s, 27 * s, body);
		C2D_DrawRectSolid(x - 18 * s, cy - 27 * s, z + 0.001f, 36 * s, 21 * s, lit);
		C2D_DrawRectSolid(x - 20 * s, cy - 2 * s, z, 40 * s, 25 * s, body);
		C2D_DrawRectSolid(x - 12 * s, cy + 1 * s, z + 0.001f, 24 * s, 18 * s, TH_CARD_BOTTOM);
		C2D_DrawCircleSolid(x - 16 * s, cy + 6 * s, z + 0.001f, 2 * s, th->muted);
		C2D_DrawCircleSolid(x + 16 * s, cy + 6 * s, z + 0.001f, 2 * s, th->muted);
	}
	for (int i = 0; i < 5; i++) {
		float p = fmodf(clock_s * 0.8f + i / 5.0f, 1), x = cx - 16 * s + 32 * s * p;
		float y = cy - 22 * s - sinf(p * M_PI) * 12 * s;
		C2D_DrawCircleSolid(x, y, z + 0.002f, 1.6f * s, accent(i, (u8)(255 * sinf(p * M_PI))));
	}
}

// "No games yet": a dashed card with a question mark.
static void empty_card(float cx, float cy, float size, float z)
{
	float x = cx - size / 2, y = cy - size / 2;
	u32 c = theme_alpha(theme()->muted, 200);
	for (float i = 0; i < size; i += 10) {
		C2D_DrawRectSolid(x + i, y, z, 6, 2, c);
		C2D_DrawRectSolid(x + i, y + size - 2, z, 6, 2, c);
		C2D_DrawRectSolid(x, y + i, z, 2, 6, c);
		C2D_DrawRectSolid(x + size - 2, y + i, z, 2, 6, c);
	}
	float s = size / 100.0f;
	ui_text_ex(libui_font(), cx, cy - 18 * s, s, theme_alpha(theme()->muted, 160), C2D_AlignCenter, "?");
}

// One library cover (or a plain card with the initial until its art is in).
static void cover_card(int game, float cx, float cy, float size, float angle, float dim, float z)
{
	const game_t *g = library_get(game);
	if (!g)
		return;
	C3D_Tex *t = covers_get(game);
	if (t) {
		Tex3DS_SubTexture st = { COVER_SIZE, COVER_SIZE, 0.0f, 1.0f, 1.0f, 0.0f };
		C2D_ImageTint tint;
		C2D_PlainImageTint(&tint, C2D_Color32(0, 0, 0, 255), dim);
		C2D_DrawImageAtRotated((C2D_Image){ t, &st }, cx, cy, z, angle, dim > 0 ? &tint : NULL,
			size / COVER_SIZE, size / COVER_SIZE);
		return;
	}
	float x = cx - size / 2, y = cy - size / 2;
	u8 d = (u8)(255 * (1 - dim));
	C2D_DrawRectangle(x, y, z, size, size, TH_CARD_TOP, TH_CARD_TOP, TH_CARD_BOTTOM, TH_CARD_BOTTOM);
	C2D_DrawRectSolid(x, y + size - 6, z + 0.001f, size, 6, accent(game, 255));
	char initial[2] = { g->title[0], 0 };
	float s = size / 60.0f;
	ui_text_ex(libui_font(), cx, cy - 14 * s, s, theme_alpha(TH_CARD_INK, d), C2D_AlignCenter, initial);
	if (dim > 0)
		C2D_DrawRectSolid(x, y, z + 0.002f, size, size, C2D_Color32(0, 0, 0, (u8)(dim * 255)));
}

// Five covers fanned out, the middle one in front.
static void cover_fan(float cx, float cy, float size, float z)
{
	int n = library_count() < 5 ? library_count() : 5;
	static const int back_to_front[5] = { 0, 4, 1, 3, 2 };
	for (int j = 0; j < 5; j++) {
		int i = back_to_front[j];
		if (i >= n)
			continue;
		int k = i - 2, ak = abs(k);
		float s = size * (1 - 0.14f * ak);
		cover_card(i, cx + k * size * 0.42f + ui_depth(0.4f * ak), cy + ak * 6, s, k * 0.07f, ak * 0.22f, z + j * 0.01f);
	}
}

// ---------------------------------------------------------------- top screen

static void slow_banner(void)
{
	const char *m = slow_mode_notice(false);
	if (!m)
		return;
	char l1[96], l2[96];
	const char *cut = strchr(m, ',');
	if (!cut)
		cut = strchr(m, ':');
	snprintf(l1, sizeof(l1), "%.*s", cut ? (int)(cut - m) + 1 : (int)strlen(m), m);
	snprintf(l2, sizeof(l2), "%s", cut ? cut + 2 : "");
	C2D_DrawRectSolid(0, 0, 0.65f, 400, 32, C2D_Color32(40, 22, 14, 230));   // under the text (0.7)
	ui_text_ex(NULL, 8, 2, 0.4f, C2D_Color32(255, 170, 120, 255), 0, ui_fit(l1, 0.4f, 384));
	ui_text_ex(NULL, 8, 16, 0.4f, C2D_Color32(255, 170, 120, 255), 0, ui_fit(l2, 0.4f, 384));
}

static void big_title(const char *title, const char *line, float ease)
{
	u8 a = (u8)(255 * ease);
	float size = 0.8f, w = text_w(libui_font(), size, title);
	if (w > 380)
		size *= 380 / w;
	float ty = 172 + (1 - ease) * 10, tx = 200 + ui_depth(-0.8f);
	const theme_t *th = theme();
	ui_text_ex(libui_font(), tx + 1, ty + 1, size, theme_alpha(TH_TITLE_SHADOW, a * 5 / 8), C2D_AlignCenter, title);
	ui_text_ex(libui_font(), tx, ty, size, theme_alpha(th->game_title, a), C2D_AlignCenter, title);
	ui_text_ex(NULL, tx, ty + 34, 0.45f, theme_alpha(th->game_sub, a), C2D_AlignCenter, line);
}

static void top_common(void)
{
	// small PSX3D wordmark (the slow-mode banner takes its place)
	if (!slow_mode_notice(false)) {
		float w = text_w(libui_font(), 0.42f, "PSX");
		ui_text_ex(libui_font(), 10, 6, 0.42f, C2D_Color32(245, 240, 255, 200), 0, "PSX");
		ui_text_ex(libui_font(), 10 + w + 0.42f * 6, 6, 0.42f, C2D_Color32(255, 90, 190, 200), 0, "3D");
	}
	if (!have_games) {   // first start: one friendly line
		float y = slow_mode_notice(false) ? 32 : 0;   // under the slow-mode banner
		C2D_DrawRectSolid(0, y, 0.6f, 400, 2, accent(1, 255));
		ui_text_ex(NULL, 390, y + 8, 0.38f, theme()->message, C2D_AlignRight, "Welcome! No games yet.");
	}
	slow_banner();
	ui_text_ex(NULL, 396, 226, 0.35f, TH_VERSION_INK, C2D_AlignRight, "v" PSX3D_VERSION);
}

static void draw_top(void *ud)
{
	(void)ud;
	libui_draw_backdrop();
	C2D_DrawRectSolid(0, 0, 0.3f, 400, 240, theme_alpha(theme()->menu_bg, 70));
	int id = tiles[sel_index()].id;
	float ease = 1 - (1 - title_t) * (1 - title_t);
	float s = 0.88f + 0.12f * pop, cx = 200 + ui_depth(-1.0f);
	float size = (id == E_CONTINUE ? 132 : 140) * s, cy = id == E_CONTINUE ? 86 : 92;
	char line[64];
	const char *title = label(id);
	snprintf(line, sizeof(line), "%s", sub(id, false));
	switch (id) {
	case E_CONTINUE: {
		float w = size * 1.33f, h = size;
		C2D_DrawRectSolid(cx - w / 2 - 3, cy - h / 2 - 3, 0.5f, w + 6, h + 6, accent(2, 255));
		if (cont_tex_ok)
			states_thumb_draw(&cont_tex, cx - w / 2, cy - h / 2, w, h, 0.51f);
		else
			C2D_DrawRectSolid(cx - w / 2, cy - h / 2, 0.51f, w, h, TH_CARD_BOTTOM);
		icon_play(cx, cy, size * 0.18f, C2D_Color32(255, 255, 255, 200), 0.52f);
		char when[40];
		states_when_text(when, sizeof(when), cont_saved);
		snprintf(line, sizeof(line), "Left off %s", when);
		title = cont_title();
		break;
	}
	case E_LIBRARY:
		if (have_games) cover_fan(cx, cy, size * 0.8f, 0.5f);
		else empty_card(cx, cy, size * 0.8f, 0.5f);
		break;
	case E_HOST: case E_JOIN: draw_consoles(cx, cy, size / 100, 0.5f); break;
	case E_SHARE: draw_consoles(cx, cy + size * 0.1f, size / 95, 0.5f); break;
	default: icon_settings(cx, cy, size * 0.36f, theme()->value, MENU_BG, 0.5f); break;
	}
	big_title(title, line, ease);
	top_common();
	if (fade > 0.001f)
		C2D_DrawRectSolid(0, 0, 0.95f, 400, 240, C2D_Color32(0, 0, 0, (u8)(255 * fminf(fade, 1))));
}

// ---------------------------------------------------------------- bottom screen

static void tile_icon(int id, float cx, float cy, float r, u32 ink, u32 bg, float z)
{
	switch (id) {
	case E_CONTINUE: icon_play(cx, cy, r, ink, z); break;
	case E_LIBRARY:
		if (have_games) icon_library(cx, cy, r, ink, z);
		else empty_card(cx, cy, r * 1.4f, z);
		break;
	case E_HOST: case E_JOIN:
		if (r >= 28) draw_consoles(cx, cy + r * 0.1f, r / 52, z);
		else icon_together(cx, cy, r, ink, z);
		break;
	case E_SHARE: icon_share(cx, cy, r, ink, z); break;
	default: icon_settings(cx, cy, r, ink, bg, z); break;
	}
}

// The Library tile: three random covers fanned, the front one undimmed.
static void library_picks(float x, float cy)
{
	for (int k = 0; k < npicks; k++) {
		int kk = npicks - 1 - k;
		cover_card(picks[k], x + 26 + kk * 11, cy + 2 - kk * 2, 34, (kk - 1) * 0.12f, kk ? 0.25f : 0, 0.36f + k * 0.01f);
	}
}

static void draw_tile(const tile_t *t, bool lit, int i)
{
	const theme_t *th = theme();
	float x = t->x, w = t->w, h = t->h;
	float y = t->y + (lit ? sinf(clock_s * 2) * 0.8f : 0);
	if (lit) {
		C2D_DrawRectSolid(x - 3, y - 3, 0.2f, w + 6, h + 6, theme_alpha(th->hl_glow, 70));
		C2D_DrawRectangle(x, y, 0.25f, w, h, th->hl_top, th->hl_top, th->hl_bottom, th->hl_bottom);
	} else
		C2D_DrawRectangle(x, y, 0.25f, w, h, TH_CARD_TOP, TH_CARD_TOP, TH_CARD_BOTTOM, TH_CARD_BOTTOM);
	C2D_DrawRectSolid(x, y, 0.3f, 3, h, lit ? accent(i, 255) : th->section_rule);
	u32 ink = theme_row(th, lit ? 1 : 0, 255);
	u32 subc = lit ? th->value : th->muted;
	u32 hole = TH_CARD_BOTTOM;
	if (t->kind == K_HERO) {
		if (t->id == E_CONTINUE) {
			const float tw = 112, tht = 84;
			C2D_DrawRectSolid(x + 8, y + 5, 0.35f, tw + 4, tht + 4, accent(2, 255));
			if (cont_tex_ok)
				states_thumb_draw(&cont_tex, x + 10, y + 7, tw, tht, 0.36f);
			else
				C2D_DrawRectSolid(x + 10, y + 7, 0.36f, tw, tht, TH_CARD_BOTTOM);
			icon_play(x + 10 + tw / 2, y + 7 + tht / 2, 14, C2D_Color32(255, 255, 255, 210), 0.4f);
			char when[40];
			states_when_text(when, sizeof(when), cont_saved);
			ui_text_ex(libui_font(), x + 136, y + 10, 0.55f, ink, 0, "Continue");
			ui_text_ex(NULL, x + 136, y + 40, 0.48f, ink, 0, ui_fit(cont_title(), 0.48f, 160));
			ui_text_ex(NULL, x + 136, y + 62, 0.36f, subc, 0, when);
		} else {
			tile_icon(t->id, x + 58, y + h / 2, 30, ink, hole, 0.4f);
			ui_text_ex(libui_font(), x + 116, y + 18, 0.6f, ink, 0, label(t->id));
			ui_text_ex(NULL, x + 116, y + 52, 0.42f, subc, 0, sub(t->id, !have_games));
		}
	} else if (t->kind == K_MID && t->id == E_LIBRARY && have_games) {
		library_picks(x, y + h / 2);
		ui_text_ex(NULL, x + 76, y + 18, 0.5f, ink, 0, label(t->id));
		ui_text_ex(NULL, x + 76, y + 40, 0.34f, subc, 0, ui_fit(sub(t->id, true), 0.34f, w - 80));
	} else if (t->kind == K_MID) {
		tile_icon(t->id, x + 32, y + h / 2, 20, ink, hole, 0.4f);
		ui_text_ex(NULL, x + 62, y + 15, 0.5f, ink, 0, ui_fit(label(t->id), 0.5f, w - 66));
		ui_text_ex(NULL, x + 62, y + 38, 0.34f, subc, 0, ui_fit(sub(t->id, true), 0.34f, w - 66));
	} else {
		tile_icon(t->id, x + 20, y + h / 2, 10, ink, hole, 0.4f);
		ui_text_ex(NULL, x + 38, y + (h - 15) / 2, 0.45f, ink, 0, label(t->id));
		if (t->id == E_SETTINGS) {   // X opens it too: show the letter
			C2D_DrawRectSolid(x + w - 20, y + h / 2 - 6, 0.4f, 12, 12, C2D_Color32(47, 111, 196, 255));
			ui_text_ex(NULL, x + w - 14, y + h / 2 - 7.5f, 0.36f, C2D_Color32(255, 255, 255, 255), C2D_AlignCenter, "X");
		}
	}
}

// Play together: a frame with its name, Host and Join as buttons inside it.
static void draw_group(bool lit)
{
	const theme_t *th = theme();
	float x = group.x, y = group.y, w = group.w, h = group.h;
	C2D_DrawRectangle(x, y, 0.22f, w, h, TH_CARD_TOP, TH_CARD_TOP, TH_CARD_BOTTOM, TH_CARD_BOTTOM);
	C2D_DrawRectSolid(x, y, 0.3f, 3, h, lit ? accent(1, 255) : th->section_rule);
	u32 ink = theme_row(th, lit ? 1 : 0, 255);
	icon_together(x + 20, y + 15, 11, ink, 0.3f);
	ui_text_ex(NULL, x + 36, y + 6, 0.48f, ink, 0, ui_fit("Play together", 0.48f, w - 44));
}

static void draw_sub(const tile_t *t, bool lit)
{
	const theme_t *th = theme();
	float x = t->x, y = t->y, w = t->w, h = t->h;
	if (lit) {
		C2D_DrawRectSolid(x - 2, y - 2, 0.32f, w + 4, h + 4, theme_alpha(th->hl_glow, 110));
		C2D_DrawRectangle(x, y, 0.33f, w, h, th->hl_top, th->hl_top, th->hl_bottom, th->hl_bottom);
	} else
		C2D_DrawRectangle(x, y, 0.33f, w, h, th->menu_bg, th->menu_bg, TH_CARD_BOTTOM, TH_CARD_BOTTOM);
	C2D_DrawRectSolid(x, y, 0.34f, w, 1, th->scrollbar);
	ui_text_ex(NULL, x + w / 2, y + h / 2 - 9, 0.5f, theme_row(th, lit ? 1 : 0.4f, 255), C2D_AlignCenter,
		t->id == E_HOST ? "Host" : "Join");
}

static void draw_bottom(void *ud)
{
	(void)ud;
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, MENU_BG);
	int s = sel_index();
	if (have_group)
		draw_group(tiles[s].kind == K_SUB);
	for (int i = 0; i < ntiles; i++) {
		if (tiles[i].kind == K_SUB)
			draw_sub(&tiles[i], i == s);
		else
			draw_tile(&tiles[i], i == s, i);
	}
	if (fade > 0.001f)
		C2D_DrawRectSolid(0, 0, 0.95f, 320, 240, C2D_Color32(0, 0, 0, (u8)(255 * fminf(fade, 1))));
}

// D-pad moves spatially: up/down between rows, left/right within a row.
static int nav(int cur, u32 key)
{
	const tile_t *c = &tiles[cur];
	float cx = c->x + c->w / 2, cy = c->y + c->h / 2;
	int best = -1;
	float best_d = 1e9f;
	for (int i = 0; i < ntiles; i++) {
		if (i == cur)
			continue;
		float dx = tiles[i].x + tiles[i].w / 2 - cx, dy = tiles[i].y + tiles[i].h / 2 - cy;
		// left/right stays in a row (tiles that overlap in height), up/down leaves it
		bool row = tiles[i].y < c->y + c->h && c->y < tiles[i].y + tiles[i].h;
		bool ok = key == KEY_UP ? dy < -4 && !row : key == KEY_DOWN ? dy > 4 && !row :
		          key == KEY_LEFT ? dx < -4 && row : dx > 4 && row;
		if (!ok)
			continue;
		float d = (key == KEY_UP || key == KEY_DOWN) ? fabsf(dy) * 4 + fabsf(dx) : fabsf(dx);
		if (d < best_d) { best_d = d; best = i; }
	}
	return best;
}

static void settings_top(void *ud)
{
	covers_pump();
	title_t = 1;
	draw_top(ud);
}

static void present(void)
{
	ui_begin();
	video_present_vsync(false, draw_top, draw_bottom, NULL);
}

// the library entry of a disc path (Play together > Host a game)
static void launch_info(const char *path, home_launch_t *out)
{
	for (int i = 0; i < library_count(); i++) {
		const game_t *g = library_get(i);
		for (int d = 0; d < g->ndiscs; d++)
			if (!strcmp(g->discs[d], path)) {
				snprintf(out->save, sizeof(out->save), "%s", g->cover_key);
				snprintf(out->title, sizeof(out->title), "%s", g->title);
				snprintf(g_cfg.last_title, sizeof(g_cfg.last_title), "%s", g->title);
				libui_select_title(g->title);
				config_save();
				return;
			}
	}
}

static void leave(void)
{
	aptSetHomeAllowed(true);
	music_play(false);
	ui_wants_3d = false;
	if (cont_tex_ok)
		C3D_TexDelete(&cont_tex);
	cont_tex_ok = false;
}

// as the library does before a game: no network during it, music faded out
static void launch_leave(void)
{
	covers_stop_wait();
	for (int f = 0; f < 20 && aptMainLoop(); f++) {
		fade = f / 20.0f;
		music_update();
		present();
	}
	leave();
}

// Play together screens (mp_ui.c, the multiplayer work) replace these.
static void soon_bottom(void *ud)
{
	const theme_t *th = theme();
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, th->menu_bg);
	ui_menu_header("Play together", ud);
	ui_text_wrap(16, 52, 0.5f, th->help_text, 288, "Coming soon. Playing together with a friend's 3DS arrives in a later version.");
	static const ui_button_t back = { KEY_B, "Back", 0, false };
	ui_bar_draw(&back, 1);
}

static void soon_top(void *ud)
{
	(void)ud;
	libui_draw_backdrop();
	C2D_DrawRectSolid(0, 0, 0.3f, 400, 240, theme_alpha(theme()->menu_bg, 70));
	draw_consoles(200 + ui_depth(-1.0f), 92, 1.3f, 0.5f);
	big_title("Play together", "Coming soon", 1);
	top_common();
}

static void coming_soon(const char *what)
{
	static const ui_button_t back = { KEY_B, "Back", 0, false };
	while (aptMainLoop()) {
		if (dev_tick(false) || dev_ui_pending())
			return;
		input_poll();
		u32 k = ui_header_back(ui_bar_input(&back, 1, input_keys_down()));
		if (k & (KEY_B | KEY_A)) {
			sfx_play(SFX_BACK);
			return;
		}
		clock_s += 1.0f / 60.0f;
		music_update();
		ui_begin();
		video_present_vsync(false, soon_top, soon_bottom, (void *)what);
	}
}

__attribute__((weak)) void mp_host_run(void) { coming_soon("Host a game"); }
__attribute__((weak)) void mp_join_run(void) { coming_soon("Join a game"); }

int home_run(bool games, char *path, size_t len, home_launch_t *out)
{
	memset(out, 0, sizeof(*out));
	have_games = games;
	cont_probe();
	layout();
	pick_covers();
	if (sel_id < 0 || sel_index() == 0)
		sel_id = tiles[0].id;
	title_for = -1;
	fade = 1;
	music_play(true);
	ui_wants_3d = true;
	covers_resume();
	bool home_pending = false;
	while (aptMainLoop()) {
		if (dev_tick(false)) {
			leave();
			return HOME_AGAIN;
		}
		int screen = dev_ui_take();
		if (screen == DEV_UI_SETTINGS) {
			settings_ui_run(false, settings_top);
			if (settings_ui_take_folders()) { leave(); return HOME_FOLDERS; }
			continue;
		}
		if (screen == DEV_UI_LIBRARY) { leave(); return HOME_LIBRARY; }
		input_poll();
		u32 k = input_keys_down();

		// HOME: held back while the covers use the network (as in the library)
		aptSetHomeAllowed(!home_pending && !covers_busy());
		if (aptCheckHomePressRejected() && !home_pending)
			home_pending = true;
		if (home_pending && covers_stop()) {
			home_pending = false;
			aptSetHomeAllowed(true);
			aptJumpToHomeMenu();
			covers_resume();
			continue;
		}

		int cur = sel_index(), before = cur;
		if (k & KEY_START) {
			leave();
			return HOME_QUIT;
		}
		if (k & KEY_B)
			sfx_play(SFX_BACK);   // the root: nothing behind it
		static const u32 dirs[4] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
		static const u32 pads[4] = { KEY_DUP | KEY_CPAD_UP, KEY_DDOWN | KEY_CPAD_DOWN, KEY_DLEFT | KEY_CPAD_LEFT, KEY_DRIGHT | KEY_CPAD_RIGHT };
		for (int d = 0; d < 4; d++)
			if (k & pads[d]) {
				int j = nav(cur, dirs[d]);
				if (j >= 0) cur = j;
			}
		if (k & KEY_TOUCH) {
			touchPosition tp;
			hidTouchRead(&tp);
			for (int i = 0; i < ntiles && (tp.px || tp.py); i++)
				if (tp.px >= tiles[i].x && tp.px < tiles[i].x + tiles[i].w &&
				    tp.py >= tiles[i].y && tp.py < tiles[i].y + tiles[i].h) {
					cur = i;
					k |= KEY_A;   // the tiles are big: one tap opens
				}
		}
		sel_id = tiles[cur].id;
		if (cur != before && !(k & KEY_A))
			sfx_play(SFX_MOVE);

		int act = (k & KEY_A) ? sel_id : (k & KEY_X) ? E_SETTINGS : -1;
		if (act >= 0)
			sfx_play(act == E_CONTINUE ? SFX_LAUNCH : SFX_SELECT);
		switch (act) {
		case E_CONTINUE:
			snprintf(path, len, "%s", cont_path);
			snprintf(out->save, sizeof(out->save), "%s", g_cfg.last_game_save);
			snprintf(out->title, sizeof(out->title), "%s", g_cfg.last_game);
			out->resume = true;
			if (g_cfg.last_game[0]) {
				snprintf(g_cfg.last_title, sizeof(g_cfg.last_title), "%s", g_cfg.last_game);
				libui_select_title(g_cfg.last_game);   // the library shows it afterwards
				config_save();
			}
			launch_leave();
			return HOME_PLAY;
		case E_LIBRARY:
			leave();
			return HOME_LIBRARY;
		case E_HOST:
		case E_JOIN:
			if (act == E_HOST)
				mp_host_run();
			else
				mp_join_run();
			if (mp_launch_request(path, len)) {
				launch_info(path, out);
				launch_leave();
				return HOME_PLAY;
			}
			if (dev_transition()) { leave(); return HOME_AGAIN; }
			continue;
		case E_SHARE:
			share_run();
			if (dev_transition()) { leave(); return HOME_AGAIN; }
			continue;
		case E_SETTINGS:
			settings_ui_run(false, settings_top);
			if (settings_ui_take_folders()) {   // Settings > Games folder
				leave();
				return HOME_FOLDERS;
			}
			// Reset all settings keeps the last game; the library may have changed
			cont_probe();
			layout();
			continue;
		}

		music_update();
		const float dt = 1.0f / 60.0f;
		clock_s += dt;
		int s = sel_index();
		if (title_for != s) {
			title_for = s;
			title_t = 0;
			pop = 0.9f;
			pop_v = 0;
		}
		pop_v += ((1.0f - pop) * 260.0f - pop_v * 14.0f) * dt;
		pop += pop_v * dt;
		if (title_t < 1) title_t = fminf(1, title_t + dt * 5);
		if (fade > 0) fade = fmaxf(0, fade - dt * 3);
		for (int i = 0; i < 5 && i < library_count(); i++)
			covers_get(i);
		for (int i = 0; i < npicks; i++)
			covers_get(picks[i]);
		covers_pump();
		present();
	}
	leave();
	return HOME_QUIT;
}

// A game started here: the main menu continues it next time.
void home_note_game(const char *path, const char *save_name, const char *title, const char *state_name)
{
	if (!strcmp(g_cfg.last_game_path, path) && !strcmp(g_cfg.last_game_state, state_name) &&
	    !strcmp(g_cfg.last_game, title ? title : "") && !strcmp(g_cfg.last_game_save, save_name ? save_name : ""))
		return;
	snprintf(g_cfg.last_game_path, sizeof(g_cfg.last_game_path), "%s", path);
	snprintf(g_cfg.last_game_state, sizeof(g_cfg.last_game_state), "%s", state_name);
	snprintf(g_cfg.last_game, sizeof(g_cfg.last_game), "%s", title ? title : "");
	snprintf(g_cfg.last_game_save, sizeof(g_cfg.last_game_save), "%s", save_name ? save_name : "");
	config_save();
}

// ---------------------------------------------------------------- empty library

#define DEFAULT_GAMES_DIR "sdmc:/roms/psx"
static char empty_msg[96];
static int empty_msg_frames;

static void empty_top(void *ud)
{
	(void)ud;
	libui_draw_backdrop();
	C2D_DrawRectSolid(0, 0, 0.3f, 400, 240, theme_alpha(theme()->menu_bg, 70));
	empty_card(200 + ui_depth(-1.0f), 92, 112, 0.5f);
	big_title("No games yet", "Put PS1 games in " DEFAULT_GAMES_DIR " and press SELECT", 1);
	top_common();
	if (fade > 0.001f)
		C2D_DrawRectSolid(0, 0, 0.95f, 400, 240, C2D_Color32(0, 0, 0, (u8)(255 * fminf(fade, 1))));
}

static const ui_button_t empty_bar[3] = {
	{ KEY_B, "Menu", 0, false }, { KEY_Y, "Folders", UI_ICON_FOLDER, false }, { KEY_SELECT, "Look again", UI_ICON_RESCAN, false },
};

static void empty_bottom(void *ud)
{
	(void)ud;
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, MENU_BG);
	ui_menu_header("Library", "Empty");
	static const char *const steps[3] = {
		"Copy games to " DEFAULT_GAMES_DIR, "Press SELECT", "Or pick another folder (Y)"
	};
	const theme_t *th = theme();
	u32 white = th->white, muted = th->muted;
	for (int i = 0; i < 3; i++) {
		float y = 42 + i * 28;
		C2D_DrawCircleSolid(22, y + 8, 0.3f, 9, accent(i, 255));
		char n[2] = { (char)('1' + i), 0 };
		ui_text_ex(NULL, 22, y + 1, 0.42f, th->menu_bg, C2D_AlignCenter, n);
		ui_text_ex(NULL, 40, y, 0.48f, white, 0, steps[i]);
	}
	ui_text_ex(NULL, 14, 132, 0.4f, muted, 0, ".cue + .bin, .chd, .pbp, .iso, .m3u");
	ui_text_wrap(14, 156, 0.4f, muted, 292, "No games? You can still join a friend: B, then Play together.");
	if (empty_msg_frames > 0)
		ui_text_ex(NULL, 8, 186, 0.4f, th->message, 0, ui_fit(empty_msg, 0.4f, 304));
	ui_bar_draw(empty_bar, 3);
	if (fade > 0.001f)
		C2D_DrawRectSolid(0, 0, 0.95f, 320, 240, C2D_Color32(0, 0, 0, (u8)(255 * fminf(fade, 1))));
}

int home_empty_library_run(void)
{
	have_games = false;
	fade = 1;
	empty_msg_frames = 0;
	music_play(true);
	ui_wants_3d = true;
	int r = 0;
	while (aptMainLoop()) {
		if (dev_tick(false)) { r = 3; break; }
		int screen = dev_ui_take();
		if (screen == DEV_UI_SETTINGS) { settings_ui_run(false, empty_top); continue; }
		input_poll();
		u32 k = ui_header_back(ui_bar_input(empty_bar, 3, input_keys_down()));
		if (k & KEY_START) { r = 0; break; }
		if (k & KEY_B) { sfx_play(SFX_BACK); r = 1; break; }
		if (k & KEY_Y) { sfx_play(SFX_SELECT); r = 2; break; }
		if (k & KEY_X) { sfx_play(SFX_SELECT); settings_ui_run(false, empty_top); continue; }
		if (k & KEY_SELECT) {
			const char *dir = g_cfg.games_dir[0] ? g_cfg.games_dir : DEFAULT_GAMES_DIR;
			sfx_play(SFX_SELECT);
			if (library_load(dir, true)) {
				if (dir != g_cfg.games_dir) {
					snprintf(g_cfg.games_dir, sizeof(g_cfg.games_dir), "%s", dir);
					config_save();
				}
				r = 3;
				break;
			}
			snprintf(empty_msg, sizeof(empty_msg), "Still no games in %.70s", dir);
			empty_msg_frames = 180;
		}
		music_update();
		clock_s += 1.0f / 60.0f;
		if (fade > 0) fade = fmaxf(0, fade - 3.0f / 60.0f);
		if (empty_msg_frames > 0) empty_msg_frames--;
		ui_begin();
		video_present_vsync(false, empty_top, empty_bottom, NULL);
	}
	music_play(false);
	ui_wants_3d = false;
	return r;
}
