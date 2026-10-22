// Menu colour themes (Display > Theme). Synthwave holds the colours the menus
// always had; the others only move those colours around.
#include <string.h>
#include "psx3d.h"
#include "theme.h"

#define C TH_RGB
#define CA TH_RGBA

static const theme_t themes[] = {
	{
		.id = "synthwave", .name = "Synthwave", .scene = true,
		.sky_top = C(16, 2, 44), .sky_bottom = C(16, 2, 44),
		.bg = C(6, 9, 24), .title = C(235, 238, 255), .right = C(110, 120, 160),
		.accent = { C(230, 30, 60), C(250, 205, 20), C(30, 190, 120), C(40, 110, 230) },
		.hl_glow = CA(60, 90, 200, 40), .hl_top = CA(40, 58, 130, 235), .hl_bottom = CA(24, 34, 84, 235),
		.row_dim = { 150, 150, 180 }, .row_lit = { 255, 255, 285 },
		.value = C(130, 160, 230), .section = C(125, 145, 190), .rule = C(30, 42, 72),
		.text = C(230, 235, 255), .muted = C(130, 150, 190),
		.hint = C(90, 100, 135), .message = C(255, 210, 120), .error = C(255, 130, 130),
		.scrollbar = C(60, 80, 140),
		.help_bg = CA(6, 9, 24, 225), .help_bar = C(40, 110, 230), .help_title = C(235, 238, 255),
		.help_impact = C(250, 205, 20), .help_text = C(170, 180, 215),
		.game_title = C(255, 255, 255), .game_title_shadow = CA(0, 0, 0, 255), .game_sub = C(170, 185, 230),
		.corner = CA(150, 160, 200, 160), .shadow = CA(0, 0, 0, 110),
		.card_top = C(20, 26, 60), .card_bottom = C(8, 10, 28), .card_ink = C(220, 225, 245),
		.stack = { 40, 46, 70 }, .stack_step = -8,
		.veil = CA(2, 4, 16, 170), .veil_text = C(170, 185, 230),
		.slot_veil = CA(4, 6, 18, 225), .slot_frame = C(10, 12, 30), .slot_card = C(18, 22, 48),
		.slot_dim = CA(6, 9, 24, 90),
	},
	{
		.id = "grey", .name = "Dark grey",
		.sky_top = C(44, 45, 50), .sky_bottom = C(24, 25, 28),
		.bg = C(28, 29, 32), .title = C(238, 238, 240), .right = C(142, 145, 152),
		.accent = { C(120, 160, 210), C(120, 160, 210), C(120, 160, 210), C(120, 160, 210) },
		.hl_glow = CA(160, 180, 210, 28), .hl_top = CA(78, 82, 92, 240), .hl_bottom = CA(56, 59, 67, 240),
		.row_dim = { 160, 162, 170 }, .row_lit = { 255, 255, 262 },
		.value = C(170, 190, 220), .section = C(150, 154, 164), .rule = C(54, 56, 62),
		.text = C(232, 233, 238), .muted = C(150, 153, 162),
		.hint = C(112, 115, 124), .message = C(255, 210, 120), .error = C(255, 130, 130),
		.scrollbar = C(96, 99, 108),
		.help_bg = CA(24, 25, 28, 230), .help_bar = C(120, 160, 210), .help_title = C(238, 238, 240),
		.help_impact = C(250, 205, 20), .help_text = C(184, 187, 196),
		.game_title = C(246, 246, 248), .game_title_shadow = CA(0, 0, 0, 255), .game_sub = C(176, 180, 190),
		.corner = CA(160, 163, 172, 160), .shadow = CA(0, 0, 0, 120),
		.card_top = C(58, 60, 66), .card_bottom = C(36, 37, 41), .card_ink = C(226, 227, 232),
		.stack = { 56, 58, 64 }, .stack_step = -7,
		.veil = CA(8, 8, 10, 170), .veil_text = C(176, 180, 190),
		.slot_veil = CA(10, 10, 12, 225), .slot_frame = C(22, 23, 26), .slot_card = C(46, 48, 54),
		.slot_dim = CA(28, 29, 32, 90),
	},
	{
		.id = "black", .name = "Black",
		.sky_top = C(0, 0, 0), .sky_bottom = C(0, 0, 0),
		.bg = C(0, 0, 0), .title = C(240, 240, 240), .right = C(130, 130, 130),
		.accent = { C(200, 200, 200), C(200, 200, 200), C(200, 200, 200), C(200, 200, 200) },
		.hl_glow = CA(255, 255, 255, 0), .hl_top = CA(52, 52, 56, 255), .hl_bottom = CA(34, 34, 38, 255),
		.row_dim = { 150, 150, 150 }, .row_lit = { 255, 255, 255 },
		.value = C(190, 190, 195), .section = C(140, 140, 140), .rule = C(44, 44, 44),
		.text = C(235, 235, 235), .muted = C(140, 140, 140),
		.hint = C(100, 100, 100), .message = C(255, 210, 120), .error = C(255, 130, 130),
		.scrollbar = C(90, 90, 90),
		.help_bg = CA(0, 0, 0, 235), .help_bar = C(200, 200, 200), .help_title = C(240, 240, 240),
		.help_impact = C(250, 205, 20), .help_text = C(176, 176, 176),
		.game_title = C(250, 250, 250), .game_title_shadow = CA(0, 0, 0, 255), .game_sub = C(165, 165, 165),
		.corner = CA(140, 140, 140, 160), .shadow = CA(0, 0, 0, 0),
		.card_top = C(34, 34, 36), .card_bottom = C(16, 16, 18), .card_ink = C(220, 220, 220),
		.stack = { 36, 36, 38 }, .stack_step = -8,
		.veil = CA(0, 0, 0, 180), .veil_text = C(165, 165, 165),
		.slot_veil = CA(0, 0, 0, 235), .slot_frame = C(14, 14, 14), .slot_card = C(26, 26, 28),
		.slot_dim = CA(0, 0, 0, 100),
	},
	{
		.id = "light", .name = "Light",
		.sky_top = C(206, 212, 230), .sky_bottom = C(244, 245, 250),
		.bg = C(238, 239, 244), .title = C(26, 28, 44), .right = C(104, 110, 134),
		.accent = { C(50, 100, 210), C(50, 100, 210), C(50, 100, 210), C(50, 100, 210) },
		.hl_glow = CA(60, 100, 210, 26), .hl_top = CA(196, 208, 240, 255), .hl_bottom = CA(172, 188, 230, 255),
		.row_dim = { 84, 88, 108 }, .row_lit = { 10, 14, 40 },
		.value = C(40, 84, 190), .section = C(92, 100, 130), .rule = C(204, 208, 220),
		.text = C(30, 32, 48), .muted = C(98, 104, 128),
		.hint = C(132, 138, 158), .message = C(168, 98, 6), .error = C(190, 40, 40),
		.scrollbar = C(158, 166, 190),
		.help_bg = CA(240, 241, 246, 238), .help_bar = C(50, 100, 210), .help_title = C(26, 28, 44),
		.help_impact = C(168, 98, 6), .help_text = C(58, 62, 84),
		.game_title = C(26, 28, 44), .game_title_shadow = CA(255, 255, 255, 190), .game_sub = C(80, 88, 122),
		.corner = CA(96, 102, 128, 200), .shadow = CA(20, 24, 50, 70),
		.card_top = C(218, 223, 238), .card_bottom = C(196, 203, 224), .card_ink = C(52, 58, 90),
		.stack = { 200, 206, 226 }, .stack_step = -9,
		.veil = CA(2, 4, 16, 170), .veil_text = C(170, 185, 230),
		.slot_veil = CA(4, 6, 18, 225), .slot_frame = C(208, 212, 226), .slot_card = C(222, 225, 236),
		.slot_dim = CA(238, 239, 244, 110),
	},
	{
		.id = "playstation", .name = "PlayStation",
		.sky_top = C(62, 68, 88), .sky_bottom = C(34, 38, 52),
		.bg = C(36, 40, 54), .title = C(240, 242, 250), .right = C(150, 158, 180),
		.accent = { C(230, 30, 60), C(250, 205, 20), C(30, 190, 120), C(40, 110, 230) },
		.hl_glow = CA(120, 150, 230, 30), .hl_top = CA(80, 96, 150, 240), .hl_bottom = CA(56, 68, 112, 240),
		.row_dim = { 168, 174, 192 }, .row_lit = { 255, 255, 262 },
		.value = C(160, 182, 235), .section = C(150, 158, 182), .rule = C(60, 66, 86),
		.text = C(234, 237, 248), .muted = C(152, 160, 184),
		.hint = C(118, 126, 150), .message = C(255, 210, 120), .error = C(255, 130, 130),
		.scrollbar = C(100, 110, 146),
		.help_bg = CA(30, 34, 46, 230), .help_bar = C(40, 110, 230), .help_title = C(240, 242, 250),
		.help_impact = C(250, 205, 20), .help_text = C(186, 192, 212),
		.game_title = C(250, 251, 255), .game_title_shadow = CA(0, 0, 0, 255), .game_sub = C(182, 190, 214),
		.corner = CA(166, 174, 198, 160), .shadow = CA(0, 0, 0, 120),
		.card_top = C(70, 76, 98), .card_bottom = C(44, 48, 64), .card_ink = C(228, 231, 244),
		.stack = { 66, 72, 94 }, .stack_step = -8,
		.veil = CA(10, 12, 20, 170), .veil_text = C(182, 190, 214),
		.slot_veil = CA(12, 14, 22, 225), .slot_frame = C(26, 29, 40), .slot_card = C(52, 58, 78),
		.slot_dim = CA(36, 40, 54, 90),
	},
	{
		.id = "teal", .name = "Teal",
		.sky_top = C(10, 40, 48), .sky_bottom = C(4, 18, 24),
		.bg = C(4, 14, 20), .title = C(224, 246, 248), .right = C(96, 146, 156),
		.accent = { C(40, 190, 200), C(40, 190, 200), C(40, 190, 200), C(40, 190, 200) },
		.hl_glow = CA(40, 180, 200, 36), .hl_top = CA(22, 96, 112, 240), .hl_bottom = CA(12, 56, 70, 240),
		.row_dim = { 140, 172, 180 }, .row_lit = { 255, 255, 262 },
		.value = C(110, 212, 222), .section = C(100, 164, 174), .rule = C(20, 46, 56),
		.text = C(222, 244, 246), .muted = C(112, 160, 170),
		.hint = C(76, 120, 130), .message = C(255, 210, 120), .error = C(255, 130, 130),
		.scrollbar = C(40, 110, 122),
		.help_bg = CA(4, 14, 20, 230), .help_bar = C(40, 190, 200), .help_title = C(224, 246, 248),
		.help_impact = C(250, 205, 20), .help_text = C(160, 200, 208),
		.game_title = C(240, 252, 253), .game_title_shadow = CA(0, 0, 0, 255), .game_sub = C(150, 200, 208),
		.corner = CA(130, 176, 186, 160), .shadow = CA(0, 0, 0, 120),
		.card_top = C(14, 52, 62), .card_bottom = C(6, 24, 32), .card_ink = C(214, 240, 244),
		.stack = { 22, 56, 66 }, .stack_step = -6,
		.veil = CA(0, 8, 12, 170), .veil_text = C(150, 200, 208),
		.slot_veil = CA(2, 8, 12, 225), .slot_frame = C(8, 24, 32), .slot_card = C(14, 44, 54),
		.slot_dim = CA(4, 14, 20, 90),
	},
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

u32 theme_fade(u32 c, int a)
{
	return theme_alpha(c, (int)(c >> 24) * a / 255);
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
