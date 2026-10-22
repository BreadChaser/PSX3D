// Menu colours. Every menu colour that differs between palettes comes from the
// active theme (Settings > Display > Theme). The palettes and token names are
// the UI mock-up's (devtools/ui-mock js/theme.js); a token the palettes never
// change stays a literal where it is drawn. docs/dev/themes.md lists each
// token and where it is used.
#pragma once
#include <stdbool.h>
#include <3ds.h>

#define TH_RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define TH_RGB(r, g, b) TH_RGBA(r, g, b, 255)

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
} theme_t;

int theme_count(void);
const theme_t *theme_get(int i);
const theme_t *theme(void);          // the one in use (g_cfg.theme)
int theme_from_id(const char *id);   // -1 when unknown

// c with alpha a (0..255) in place of its own
u32 theme_alpha(u32 c, int a);
// row text colour for lift 0..1 and opacity a
u32 theme_row(const theme_t *t, float lift, int a);
