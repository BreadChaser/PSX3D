// Menu colours. Every menu colour comes from the active theme (docs/settings.md,
// Display > Theme); synthwave is the default and holds the original values.
#pragma once
#include <stdbool.h>
#include <3ds.h>

#define TH_RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define TH_RGB(r, g, b) TH_RGBA(r, g, b, 255)

typedef struct {
	const char *id, *name;
	bool scene;                  // sunset sky and wireframe floor behind the library
	u32 sky_top, sky_bottom;   // plain backdrop gradient when there is no scene
	u32 bg;                    // menu pages on the bottom screen and plain top screens
	u32 title, right;          // page title, scope text beside it (and slot numbers, hints)
	u32 accent[4];             // the rule under page titles, row edges, cover cards
	u32 hl_glow, hl_top, hl_bottom;   // highlight bar
	int row_dim[3], row_lit[3];  // row text: unselected -> selected (may pass 255; clamped)
	u32 value, section, rule;  // value column, section heading, its rule
	u32 text, muted;           // body text, tips and footers
	u32 hint, message, error;  // footer hint, notices, failures
	u32 scrollbar;
	u32 help_bg, help_bar, help_title, help_impact, help_text;   // top-screen help panel
	u32 game_title, game_title_shadow, game_sub, corner;         // library titles over the backdrop
	u32 shadow;                // cover shadow
	u32 card_top, card_bottom, card_ink;   // cover placeholder card
	int stack[3], stack_step;    // cards stacked behind a series
	u32 veil, veil_text;       // over the paused game
	u32 slot_veil, slot_frame, slot_card, slot_dim;   // save state picker
} theme_t;

int theme_count(void);
const theme_t *theme_get(int i);
const theme_t *theme(void);          // the one in use (g_cfg.theme)
int theme_from_id(const char *id);   // -1 when unknown

// c with alpha a (0..255) replacing its own; c's alpha scaled by a/255
u32 theme_alpha(u32 c, int a);
u32 theme_fade(u32 c, int a);
// row text colour for lift 0..1 and opacity a
u32 theme_row(const theme_t *t, float lift, int a);
