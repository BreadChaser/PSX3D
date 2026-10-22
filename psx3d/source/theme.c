// The palettes of the UI mock-up (devtools/ui-mock js/theme.js, "Original" plus
// four alternatives), token for token. Each alternative only overrides some
// tokens of the Original; tools/themes/check-mock.py compares this table with
// the mock's theme code. The cards, wordmark and backdrop fields are the
// console's own (the mock draws other screens there).
#include <string.h>
#include "psx3d.h"
#include "theme.h"

#define C TH_RGB
#define CA TH_RGBA

#define ORIGINAL \
	.menu_bg = C(6, 9, 24), .header_title = C(235, 238, 255), .header_right = C(110, 120, 160), \
	.accent = { C(230, 30, 60), C(250, 205, 20), C(30, 190, 120), C(40, 110, 230) }, \
	.hl_glow = CA(60, 90, 200, 40), .hl_top = CA(40, 58, 130, 235), .hl_bottom = CA(24, 34, 84, 235), \
	.row_dim = { 150, 150, 180 }, .row_lit = { 255, 255, 255 }, \
	.value = C(130, 160, 230), .section = C(125, 145, 190), .section_rule = C(30, 42, 72), \
	.muted = C(130, 150, 190), .white = C(230, 235, 255), \
	.hint_lib = C(90, 100, 135), .hint_menu = C(110, 120, 160), \
	.message = C(255, 210, 120), .error = C(255, 130, 130), .scrollbar = C(60, 80, 140), \
	.help_bg = CA(6, 9, 24, 225), .help_bar = C(40, 110, 230), .help_title = C(235, 238, 255), \
	.help_impact = C(250, 205, 20), .help_text = C(170, 180, 215), \
	.slow_bg = CA(40, 22, 14, 230), .slow_text = C(255, 170, 120), \
	.game_title = C(255, 255, 255), .game_sub = C(170, 185, 230), .pause_veil = CA(2, 4, 16, 170), \
	.touch_face = C(12, 18, 48), .touch_border = C(31, 42, 90), \
	.card_top = C(20, 26, 60), .card_bottom = C(8, 10, 28), .card_ink = C(220, 225, 245), \
	.card_stack = C(40, 46, 70), .version_ink = CA(150, 160, 200, 160), \
	.logo = CA(245, 240, 255, 200), .logo_3d = CA(255, 90, 190, 200), \
	.folder = C(170, 185, 220), \
	.sky = { { 256, 0, 0, 0 }, { 0, 256, 0, 0 }, { 0, 0, 256, 0 } }, \
	.grid_near = C(150, 60, 255), .grid_side = C(40, 90, 255), .grid_far = C(255, 70, 200)

// later designators override the Original's
#pragma GCC diagnostic ignored "-Woverride-init"
static const theme_t themes[] = {
	{ ORIGINAL, .id = "original", .name = "Original (v0.5.0)" },
	{ ORIGINAL, .id = "midnight-teal", .name = "Midnight teal",
		.menu_bg = C(4, 14, 20), .hl_top = CA(20, 92, 110, 235), .hl_bottom = CA(10, 50, 64, 235),
		.hl_glow = CA(40, 180, 200, 40), .value = C(110, 210, 220), .muted = C(110, 160, 170),
		.help_bar = C(40, 190, 200), .scrollbar = C(40, 110, 120), .header_right = C(90, 140, 150),
		.hint_lib = C(70, 115, 125), .hint_menu = C(90, 140, 150), .section = C(100, 170, 180),
		.touch_face = C(6, 26, 32), .touch_border = C(20, 70, 80),
		// night: a pale teal moon over a dark sea
		.card_top = C(12, 42, 54), .card_bottom = C(4, 16, 22), .card_ink = C(215, 240, 240),
		.card_stack = C(30, 64, 74), .folder = C(150, 215, 220),
		.version_ink = CA(120, 170, 180, 160), .logo_3d = CA(90, 220, 220, 200),
		.sky = { { 26, 38, 13, 0 }, { 90, 115, 38, 0 }, { 77, 102, 90, 0 } },
		.grid_near = C(60, 200, 210), .grid_side = C(40, 110, 200), .grid_far = C(140, 240, 230) },
	{ ORIGINAL, .id = "ps1-grey", .name = "PS1 grey",
		.menu_bg = C(28, 28, 32), .hl_top = CA(90, 90, 100, 235), .hl_bottom = CA(60, 60, 70, 235),
		.hl_glow = CA(200, 200, 210, 30), .value = C(200, 200, 215), .muted = C(150, 150, 160),
		.help_bg = CA(24, 24, 28, 230), .help_bar = C(200, 200, 210), .header_right = C(140, 140, 150),
		.hint_lib = C(120, 120, 130), .hint_menu = C(140, 140, 150), .row_dim = { 170, 170, 175 },
		.scrollbar = C(110, 110, 120), .section = C(160, 160, 170),
		// grey scale, a silver sun
		.card_top = C(58, 58, 66), .card_bottom = C(30, 30, 35), .card_ink = C(228, 228, 235),
		.card_stack = C(78, 78, 88), .folder = C(200, 200, 210),
		.version_ink = CA(160, 160, 170, 160), .logo_3d = CA(200, 205, 220, 200),
		.sky = { { 110, 100, 40, 6 }, { 110, 100, 40, 6 }, { 115, 105, 45, 12 } },
		.grid_near = C(175, 178, 195), .grid_side = C(115, 120, 140), .grid_far = C(225, 228, 240) },
	{ ORIGINAL, .id = "sunset-magenta", .name = "Sunset magenta",
		.menu_bg = C(16, 4, 26), .hl_top = CA(120, 30, 120, 235), .hl_bottom = CA(60, 14, 74, 235),
		.hl_glow = CA(255, 80, 200, 40), .value = C(255, 140, 210), .muted = C(190, 130, 190),
		.help_bar = C(255, 80, 180), .help_bg = CA(16, 4, 26, 225), .header_right = C(170, 110, 170),
		.hint_lib = C(140, 90, 140), .hint_menu = C(170, 110, 170), .section = C(210, 140, 210),
		.scrollbar = C(140, 60, 140),
		// a peach sun in a magenta sky
		.card_top = C(50, 14, 58), .card_bottom = C(20, 5, 30), .card_ink = C(245, 220, 240),
		.card_stack = C(74, 32, 82), .folder = C(235, 170, 220),
		.version_ink = CA(190, 140, 190, 160), .logo_3d = CA(255, 150, 205, 200),
		.sky = { { 256, 0, 0, 0 }, { 0, 180, 26, 0 }, { 115, 0, 90, 0 } },
		.grid_near = C(255, 90, 190), .grid_side = C(170, 60, 210), .grid_far = C(255, 185, 140) },
	{ ORIGINAL, .id = "high-contrast", .name = "High contrast",
		.menu_bg = C(0, 0, 0), .hl_top = CA(30, 70, 200, 255), .hl_bottom = CA(20, 50, 160, 255),
		.hl_glow = CA(0, 0, 0, 0), .row_dim = { 215, 215, 225 }, .value = C(170, 210, 255),
		.muted = C(190, 200, 220), .hint_lib = C(170, 180, 200), .hint_menu = C(170, 180, 200),
		.header_right = C(170, 180, 200), .help_bg = CA(0, 0, 0, 240), .help_text = C(220, 225, 240),
		// black sky, a white-blue sun, bright lines
		.card_top = C(26, 28, 40), .card_bottom = C(0, 0, 0), .card_ink = C(255, 255, 255),
		.card_stack = C(52, 54, 70), .folder = C(220, 230, 255),
		.version_ink = CA(190, 200, 220, 220),
		.logo = CA(255, 255, 255, 235), .logo_3d = CA(120, 190, 255, 235),
		.sky = { { 190, 114, -95, -67 }, { 218, 131, -109, -77 }, { 256, 154, -128, -60 } },
		.grid_near = C(200, 225, 255), .grid_side = C(90, 140, 240), .grid_far = C(255, 255, 255) },
};
#define NTHEMES ((int)(sizeof(themes) / sizeof(themes[0])))

int theme_count(void) { return NTHEMES; }
const theme_t *theme_get(int i) { return &themes[i >= 0 && i < NTHEMES ? i : 0]; }
const theme_t *theme(void) { return theme_get(g_cfg.theme); }

int theme_from_id(const char *id)
{
	for (int i = 0; i < NTHEMES; i++)
		if (!strcmp(themes[i].id, id))
			return i;
	return -1;
}

u32 theme_alpha(u32 c, int a)
{
	return (c & 0x00ffffff) | ((u32)(a < 0 ? 0 : a > 255 ? 255 : a) << 24);
}

u32 theme_row(const theme_t *t, float lift, int a)
{
	int v[3];
	for (int i = 0; i < 3; i++) {
		v[i] = (int)(t->row_dim[i] + (t->row_lit[i] - t->row_dim[i]) * lift);
		v[i] = v[i] < 0 ? 0 : v[i] > 255 ? 255 : v[i];
	}
	return TH_RGBA(v[0], v[1], v[2], a);
}
