// Menu colour roles and code-drawn scene colours, shared with the approved UI mock.
#pragma once
#include <stdbool.h>
#include <3ds.h>

#define TH_RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define TH_RGB(r, g, b) TH_RGBA(r, g, b, 255)

#include "game_palette.h"

typedef struct {
	const char *id, *name;       // config.ini value, name shown in Settings (the mock's palette name)
	u32 menu_bg;                 // menu pages (bottom screen, plain top screens)
	u32 header_title, header_right;   // page title; scope text beside it
	u32 accent[4];               // rule under page titles, row edges, cover accents
	u32 hl_glow, hl_top, hl_bottom;   // highlight bar
	int row_dim[3], row_lit[3];  // row text, unselected -> selected
	u32 value, section, section_rule; // value column, section heading and its rule
	u32 muted, white;            // tips and footers; scope line
	u32 hint_lib, hint_menu;     // footers: library, pause menu
	u32 message, error;          // notices, failures
	u32 scrollbar;
	u32 help_bg, help_bar, help_title, help_impact, help_text;   // top-screen help panel
	u32 slow_bg, slow_text;      // "fast mode is off" banner
	u32 game_title, game_sub;    // library title and its second line; pause menu game name
	u32 pause_veil;              // over the paused game
	u32 touch_face, touch_border;     // touch button panel
	// Console-only (not in the mock's palettes): cards, wordmark and the backdrop.
	u32 card_top, card_bottom, card_ink;   // cover placeholder cards, unselected main menu tiles
	u32 card_stack;              // cards stacked behind a series' cover (darker per card)
	u32 version_ink;             // "v0.5.1" in the corner
	u32 logo, logo_3d;           // "PSX" and "3D" of the small wordmark
	u32 folder;                  // folder browser: the games folder line
	// Icons (icons.c, the UI mock's js/icon-tokens.js): the selected picture's
	// gradient from top-left to bottom-right, and its glow (alpha = strength,
	// 0 = none). Unselected icons use theme_icon_dim().
	u32 icon_a, icon_b, icon_glow;
	u32 disc_title, disc_rim, disc_inner, disc_outer, disc_mirror[2], disc_hub[2], disc_hole, disc_rainbow[6];
	game_palette_t game;
	u32 input_face[4], input_shoulder, input_stick, input_other, input_hide;
	u32 fx_colour, fx_colour2;
	u32 shadow, moon, scene_white, silk_light, silk_glint;
	u32 original_sky[4], original_sun[4], original_grid[3], original_floor, original_palm, original_palm_rim;
	u32 sun_sky[5], floor_sky[3], sun_floor[3], sun_disc[4];
	u32 sun_glow, sun_hot, sun_rim, ridge_high, ridge_low;
} theme_t;

#define THEMES_SHOWN 1

int theme_count(void);
const theme_t *theme_get(int i);
const theme_t *theme(void);          // the one in use (g_cfg.theme)
int theme_from_id(const char *id);   // -1 when unknown

// c with alpha a (0..255) in place of its own
u32 theme_alpha(u32 c, int a);
// row text colour for lift 0..1 and opacity a
u32 theme_row(const theme_t *t, float lift, int a);
// unselected icons: the dim row colour a little toward the background
u32 theme_icon_dim(const theme_t *t, int a);
