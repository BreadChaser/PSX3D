// Eight approved palettes; theme roles also colour the menu effects and icons.
// tools/themes/check-mock.py checks the shared tokens against the UI mock.
#include <string.h>
#include "psx3d.h"
#include "theme.h"

#define C TH_RGB
#define CA TH_RGBA

#define ORIGINAL \
	.disc_title=C(236,236,242), .disc_rim=C(40,40,48), .disc_inner=C(26,24,34), .disc_outer=C(10,9,14), \
	.disc_mirror={C(70,72,84),C(30,30,38)}, .disc_hub={C(120,124,136),C(60,62,72)}, .disc_hole=C(6,6,10), \
	.disc_rainbow={C(255,60,90),C(255,170,40),C(220,255,60),C(50,230,170),C(60,140,255),C(170,80,255)}, \
	.input_face={C(47,163,107),C(208,49,59),C(47,111,196),C(215,127,181)}, \
	.input_shoulder=C(176,90,222), .input_stick=C(69,191,208), .input_other=C(154,165,200), .input_hide=C(43,51,96), \
	.shadow = C(0,0,0), .moon = C(3,3,7), .scene_white=C(255,255,255), .silk_light=C(255,225,240), .silk_glint=C(255,235,220), \
	.sun_sky = {C(5,2,22), C(26,6,58), C(92,16,104), C(196,40,118), C(255,110,110)}, \
	.floor_sky = {C(26,5,44),C(12,3,30),C(4,1,12)}, .sun_floor={C(44,8,66),C(18,4,40),C(4,1,12)}, \
	.sun_disc={C(255,240,160),C(255,176,96),C(255,96,112),C(255,40,140)}, \
	.sun_glow=C(255,60,130), .sun_hot=C(255,170,120), .sun_rim=C(255,90,200), .ridge_high=C(30,6,50),.ridge_low=C(60,10,70), \
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
	.icon_a = C(90, 215, 255), .icon_b = C(255, 80, 200), .icon_glow = CA(255, 90, 210, 150)

// later designators override the Original's
#pragma GCC diagnostic ignored "-Woverride-init"
static const theme_t themes[] = {
	{ ORIGINAL, .id = "original", .name = "Original (v0.5.0)", .fx_colour = C(255, 70, 210), .fx_colour2 = C(170, 80, 255) },
	{ ORIGINAL, .id = "midnight-teal", .name = "Midnight teal", .fx_colour = C(120, 230, 225), .fx_colour2 = C(60, 120, 200),
		.menu_bg = C(4, 14, 20), .hl_top = CA(20, 92, 110, 235), .hl_bottom = CA(10, 50, 64, 235),
		.hl_glow = CA(40, 180, 200, 40), .value = C(110, 210, 220), .muted = C(110, 160, 170),
		.help_bar = C(40, 190, 200), .scrollbar = C(40, 110, 120), .header_right = C(90, 140, 150),
		.hint_lib = C(70, 115, 125), .hint_menu = C(90, 140, 150), .section = C(100, 170, 180),
		.touch_face = C(6, 26, 32), .touch_border = C(20, 70, 80),
		// night: a pale teal moon over a dark sea
		.card_top = C(12, 42, 54), .card_bottom = C(4, 16, 22), .card_ink = C(215, 240, 240),
		.card_stack = C(30, 64, 74), .folder = C(150, 215, 220),
		.version_ink = CA(120, 170, 180, 160), .logo_3d = CA(90, 220, 220, 200),
		.icon_a = C(150, 250, 230), .icon_b = C(60, 140, 235), .icon_glow = CA(90, 220, 230, 130) },
	{ ORIGINAL, .id = "ps1-grey", .name = "PS1 grey", .fx_colour = C(205, 212, 230), .fx_colour2 = C(140, 150, 180),
		.menu_bg = C(28, 28, 32), .hl_top = CA(90, 90, 100, 235), .hl_bottom = CA(60, 60, 70, 235),
		.hl_glow = CA(200, 200, 210, 30), .value = C(200, 200, 215), .muted = C(150, 150, 160),
		.help_bg = CA(24, 24, 28, 230), .help_bar = C(200, 200, 210), .header_right = C(140, 140, 150),
		.hint_lib = C(120, 120, 130), .hint_menu = C(140, 140, 150), .row_dim = { 170, 170, 175 },
		.scrollbar = C(110, 110, 120), .section = C(160, 160, 170),
		// grey scale, a silver sun
		.card_top = C(58, 58, 66), .card_bottom = C(30, 30, 35), .card_ink = C(228, 228, 235),
		.card_stack = C(78, 78, 88), .folder = C(200, 200, 210),
		.version_ink = CA(160, 160, 170, 160), .logo_3d = CA(200, 205, 220, 200),
		.icon_a = C(245, 246, 252), .icon_b = C(135, 145, 180), .icon_glow = CA(210, 215, 235, 90) },
	{ ORIGINAL, .id = "sunset-magenta", .name = "Sunset magenta", .fx_colour = C(255, 130, 200), .fx_colour2 = C(255, 190, 140),
		.menu_bg = C(16, 4, 26), .hl_top = CA(120, 30, 120, 235), .hl_bottom = CA(60, 14, 74, 235),
		.hl_glow = CA(255, 80, 200, 40), .value = C(255, 140, 210), .muted = C(190, 130, 190),
		.help_bar = C(255, 80, 180), .help_bg = CA(16, 4, 26, 225), .header_right = C(170, 110, 170),
		.hint_lib = C(140, 90, 140), .hint_menu = C(170, 110, 170), .section = C(210, 140, 210),
		.scrollbar = C(140, 60, 140),
		// a pink sun in a magenta sky
		.card_top = C(50, 14, 58), .card_bottom = C(20, 5, 30), .card_ink = C(245, 220, 240),
		.card_stack = C(74, 32, 82), .folder = C(235, 170, 220),
		.version_ink = CA(190, 140, 190, 160), .logo_3d = CA(255, 150, 205, 200),
		.icon_a = C(255, 205, 140), .icon_b = C(255, 70, 170), .icon_glow = CA(255, 110, 190, 150) },
	{ ORIGINAL, .id = "high-contrast", .name = "High contrast", .fx_colour = C(200, 225, 255), .fx_colour2 = C(70, 110, 210),
		.menu_bg = C(0, 0, 0), .hl_top = CA(30, 70, 200, 255), .hl_bottom = CA(20, 50, 160, 255),
		.hl_glow = CA(0, 0, 0, 0), .row_dim = { 215, 215, 225 }, .value = C(170, 210, 255),
		.muted = C(190, 200, 220), .hint_lib = C(170, 180, 200), .hint_menu = C(170, 180, 200),
		.header_right = C(170, 180, 200), .help_bg = CA(0, 0, 0, 240), .help_text = C(220, 225, 240),
		// black sky, a white-blue sun, bright lines
		.card_top = C(26, 28, 40), .card_bottom = C(0, 0, 0), .card_ink = C(255, 255, 255),
		.card_stack = C(52, 54, 70), .folder = C(220, 230, 255),
		.version_ink = CA(190, 200, 220, 220),
		.logo = CA(255, 255, 255, 235), .logo_3d = CA(120, 190, 255, 235),
		.icon_a = C(255, 255, 255), .icon_b = C(255, 255, 255), .icon_glow = CA(120, 170, 255, 0) },
	{ ORIGINAL, .id = "phosphor", .name = "Phosphor",
		.menu_bg = CA(2, 10, 5, 255),
		.header_title = CA(235, 238, 255, 255),
		.header_right = CA(90, 150, 105, 255),
		.hl_glow = CA(60, 255, 120, 35),
		.hl_top = CA(20, 90, 45, 235),
		.hl_bottom = CA(8, 50, 24, 235),
		.row_dim = { 150, 190, 160 },
		.row_lit = { 255, 255, 255 },
		.value = CA(110, 240, 150, 255),
		.section = CA(100, 190, 130, 255),
		.section_rule = CA(30, 42, 72, 255),
		.muted = CA(100, 170, 120, 255),
		.white = CA(230, 235, 255, 255),
		.hint_lib = CA(70, 120, 85, 255),
		.hint_menu = CA(90, 150, 105, 255),
		.message = CA(255, 210, 120, 255),
		.error = CA(255, 130, 130, 255),
		.scrollbar = CA(40, 120, 60, 255),
		.help_bg = CA(2, 10, 5, 230),
		.help_bar = CA(70, 230, 120, 255),
		.help_title = CA(235, 238, 255, 255),
		.help_impact = CA(250, 205, 20, 255),
		.help_text = CA(170, 180, 215, 255),
		.slow_bg = CA(40, 22, 14, 230),
		.slow_text = CA(255, 170, 120, 255),
		.game_title = CA(255, 255, 255, 255),
		.game_sub = CA(170, 185, 230, 255),
		.pause_veil = CA(2, 4, 16, 170),
		.touch_face = CA(4, 22, 10, 255),
		.touch_border = CA(20, 80, 40, 255),
		.fx_colour = CA(80, 255, 140, 255),
		.fx_colour2 = CA(30, 140, 80, 255),
		.icon_a = CA(190, 255, 200, 255),
		.icon_b = CA(40, 200, 100, 255),
		.icon_glow = CA(80, 255, 140, 140) },
	{ ORIGINAL, .id = "eclipse", .name = "Eclipse",
		.menu_bg = CA(6, 5, 12, 255),
		.header_title = CA(235, 238, 255, 255),
		.header_right = CA(150, 140, 160, 255),
		.hl_glow = CA(255, 220, 170, 30),
		.hl_top = CA(70, 58, 40, 235),
		.hl_bottom = CA(36, 28, 22, 235),
		.row_dim = { 150, 150, 180 },
		.row_lit = { 255, 255, 255 },
		.value = CA(240, 215, 170, 255),
		.section = CA(200, 185, 165, 255),
		.section_rule = CA(30, 42, 72, 255),
		.muted = CA(160, 150, 170, 255),
		.white = CA(230, 235, 255, 255),
		.hint_lib = CA(120, 112, 135, 255),
		.hint_menu = CA(150, 140, 160, 255),
		.message = CA(255, 210, 120, 255),
		.error = CA(255, 130, 130, 255),
		.scrollbar = CA(110, 95, 80, 255),
		.help_bg = CA(6, 5, 12, 230),
		.help_bar = CA(245, 205, 140, 255),
		.help_title = CA(235, 238, 255, 255),
		.help_impact = CA(250, 205, 20, 255),
		.help_text = CA(170, 180, 215, 255),
		.slow_bg = CA(40, 22, 14, 230),
		.slow_text = CA(255, 170, 120, 255),
		.game_title = CA(255, 255, 255, 255),
		.game_sub = CA(170, 185, 230, 255),
		.pause_veil = CA(2, 4, 16, 170),
		.touch_face = CA(16, 14, 24, 255),
		.touch_border = CA(60, 52, 50, 255),
		.fx_colour = CA(255, 228, 190, 255),
		.fx_colour2 = CA(130, 110, 255, 255),
		.icon_a = CA(255, 232, 195, 255),
		.icon_b = CA(140, 120, 255, 255),
		.icon_glow = CA(255, 225, 185, 110) },
	{ ORIGINAL, .id = "ember", .name = "Ember",
		.menu_bg = CA(14, 5, 3, 255),
		.header_title = CA(235, 238, 255, 255),
		.header_right = CA(170, 120, 95, 255),
		.hl_glow = CA(255, 120, 40, 40),
		.hl_top = CA(120, 44, 16, 235),
		.hl_bottom = CA(66, 22, 8, 235),
		.row_dim = { 150, 150, 180 },
		.row_lit = { 255, 255, 255 },
		.value = CA(255, 175, 110, 255),
		.section = CA(220, 150, 105, 255),
		.section_rule = CA(30, 42, 72, 255),
		.muted = CA(190, 140, 110, 255),
		.white = CA(230, 235, 255, 255),
		.hint_lib = CA(140, 95, 75, 255),
		.hint_menu = CA(170, 120, 95, 255),
		.message = CA(255, 210, 120, 255),
		.error = CA(255, 130, 130, 255),
		.scrollbar = CA(140, 70, 35, 255),
		.help_bg = CA(14, 5, 3, 230),
		.help_bar = CA(255, 130, 50, 255),
		.help_title = CA(235, 238, 255, 255),
		.help_impact = CA(250, 205, 20, 255),
		.help_text = CA(170, 180, 215, 255),
		.slow_bg = CA(40, 22, 14, 230),
		.slow_text = CA(255, 170, 120, 255),
		.game_title = CA(255, 255, 255, 255),
		.game_sub = CA(170, 185, 230, 255),
		.pause_veil = CA(2, 4, 16, 170),
		.touch_face = CA(30, 10, 5, 255),
		.touch_border = CA(90, 36, 16, 255),
		.fx_colour = CA(255, 140, 50, 255),
		.fx_colour2 = CA(255, 80, 30, 255),
		.icon_a = CA(255, 215, 130, 255),
		.icon_b = CA(255, 80, 30, 255),
		.icon_glow = CA(255, 120, 40, 150) },
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

u32 theme_icon_dim(const theme_t *t, int a)
{
	int v[3];
	for (int i = 0; i < 3; i++)
		v[i] = t->row_dim[i] + ((int)((t->menu_bg >> (8 * i)) & 255) - t->row_dim[i]) * 15 / 100;
	return TH_RGBA(v[0], v[1], v[2], a < 0 ? 0 : a > 255 ? 255 : a);
}
