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

// Colours that are the same in every theme: black shadows, and the save state
// picker (savestates.c), which still draws its own literals.
#define TH_COVER_SHADOW  TH_RGBA(0, 0, 0, 110)    // drop shadow behind covers
#define TH_TITLE_SHADOW  TH_RGBA(0, 0, 0, 255)    // library title shadow (alpha scaled by the caller)
#define TH_SLOT_VEIL     TH_RGBA(4, 6, 18, 225)   // save state picker, top screen
#define TH_SLOT_FRAME    TH_RGB(10, 12, 30)       // ... preview frame
#define TH_SLOT_CARD     TH_RGB(18, 22, 48)       // ... slot thumbnail card
#define TH_SLOT_DIM      TH_RGBA(6, 9, 24, 90)    // ... unselected thumbnail veil
#define TH_SLOT_WHITE    TH_RGB(235, 238, 255)    // ... selected text
#define TH_SLOT_DIM_INK  TH_RGB(110, 120, 160)    // ... other text

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
	// Backdrop: the sunset picture goes through this colour matrix (out = row . (r, g, b) / 256
	// + row[3]); the Original's is the identity, so it is drawn as it is stored.
	s16 sky[3][4];
	u32 grid_near, grid_side, grid_far;   // wire floor: middle near the viewer, sides, toward the horizon
} theme_t;

int theme_count(void);
const theme_t *theme_get(int i);
const theme_t *theme(void);          // the one in use (g_cfg.theme)
int theme_from_id(const char *id);   // -1 when unknown

// c with alpha a (0..255) in place of its own
u32 theme_alpha(u32 c, int a);
// row text colour for lift 0..1 and opacity a
u32 theme_row(const theme_t *t, float lift, int a);
