// Test the production category model and drawing using the existing host UI.
#define SETTINGS_NAV_TEST
#define main bottom_screen_checks
#include "../../psx3d/tests/host/check.c"
#undef main
bool rewind_active(void) { return false; }
bool rewind_start(void) { return false; }
void rewind_stop(void) {}
static int cheat_fixture_count;
int cheats_count(void) { return cheat_fixture_count; }
const char *cheats_desc(int i)
{
	(void)i;
	return "";
}
bool cheats_on(int i)
{
	(void)i;
	return false;
}
bool cheats_forced(int i)
{
	(void)i;
	return false;
}
static bool test_ws_enabled;
bool cheats_widescreen(bool on) { test_ws_enabled = on; return ws_available(); }
void cheats_toggle(int i) { (void)i; }
const char *cheats_add_dialog(void) { return NULL; }
void music_play(bool on) { (void)on; }
void music_update(void) {}
bool audio_capture_start(void) { return true; }
bool audio_test_tone_toggle(void) { return false; }
#include "../../psx3d/source/settings_ui.c"
#define HAVE_NO_LANGEXTRA
#define GPU_PICA
#define _3DS
#define USE_ASYNC_CDROM
#include "../../core/frontend/libretro_core_options.h"
static void register_options(void)
{
	struct retro_core_option_definition defs[MAX_OPTIONS] = {0};
	for (int i = 0; option_defs_us[i].key && i < MAX_OPTIONS - 1; i++) {
		defs[i].key = option_defs_us[i].key;
		defs[i].desc = option_defs_us[i].desc;
		defs[i].info = option_defs_us[i].info;
		defs[i].default_value = option_defs_us[i].default_value;
		memcpy(defs[i].values, option_defs_us[i].values, sizeof(defs[i].values));
	}
	options_register_v1(defs);
}
static bool has_row(settings_ui_t *s, int item)
{
	for (int i = 0; i < s->count; i++)
		if (s->items[i] == item)
			return true;
	return false;
}
static void check_rows(settings_ui_t *s)
{
	for (int i = 0; i < s->count; i++) {
		C2D_Text t;
		float lw, vw;
		C2D_TextFontParse(&t, NULL, NULL, row_label(i, s));
		C2D_TextGetDimensions(&t, .5, .5, &lw, NULL);
		const char *v = row_value(i, s);
		C2D_TextFontParse(&t, NULL, NULL, v ? v : "");
		C2D_TextGetDimensions(&t, .45, .45, &vw, NULL);
		if (lw + vw > 284)
			fprintf(stderr, "Overlap: %s / %s\n", row_label(i, s), v);
		assert(lw + vw <= 284);
	}
}
// Screen-mode notes: missing fix, held back for speed, offered with a warning.
static void screen_note_checks(void)
{
	bool was_in_game = in_game;
	int was_mode = config_screen_mode();
	in_game = true;
	config_set_screen_mode(2);
	for (int model = 0; model < 2; model++) {
		g_new3ds = model;
		game_presets_reset();
		test_ws_policy = -1;
		config_ws_sync();
		assert(!config_ws_fix() && !strcmp(screen_note(), "This game has no widescreen fix."));
		test_ws_policy = WS_HIDDEN;
		config_ws_sync();
		assert(!config_ws_fix() && !strcmp(screen_note(), "This game has no widescreen fix."));
		test_ws_policy = WS_TOO_SLOW;
		config_ws_sync();
		assert(config_ws_fix() && !strcmp(screen_note(), model ? "Widescreen lowers the speed a lot on New 3DS."
								: "Widescreen lowers the speed a lot on Old 3DS."));
		test_ws_policy = WS_COSTLY;
		config_ws_sync();
		assert(config_ws_fix() && !strcmp(screen_note(), model ? "Widescreen can lower the speed on New 3DS."
							     : "Widescreen can lower the speed on Old 3DS."));
		test_ws_policy = WS_OFFERED;
		config_ws_sync();
		assert(config_ws_fix() && !screen_note());
	}
	test_ws_policy = -1;
	in_game = was_in_game;
	config_set_screen_mode(was_mode);
	config_ws_sync();
}
int main(void)
{
	mkdir("sdmc:", 0700);
	mkdir("sdmc:/3ds", 0700);
	game_presets_init(PSX3D_TEST_PRESETS, PSX3D_DIR "/presets");
	mkdir(PSX3D_DIR, 0700);
	assert(!FT_Init_FreeType(&ft));
	assert(!FT_New_Face(ft, getenv("PSX3D_TITLE_FONT"), 0, &title_face));
	assert(!FT_New_Face(ft, getenv("PSX3D_BODY_FONT"), 0, &body_face));
	FT_Set_Pixel_Sizes(title_face, 0, 30);
	FT_Set_Pixel_Sizes(body_face, 0, 30);
	register_options();
	char inventory_path[1024];
	snprintf(inventory_path, sizeof(inventory_path), "%s/inventory.tsv", getenv("PSX3D_MOCKUPS"));
	FILE *inventory = fopen(inventory_path, "w");
	assert(inventory);

	static const int pages[] = {CAT_HOME, CAT_DISPLAY, CAT_CONTROLS, CAT_SAVES, CAT_ADVANCED, CAT_BINDINGS};
	for (int model = 0; model < 2; model++)
		for (int game = 0; game < 2; game++) {
			g_new3ds = model;
			config_reset_defaults();
			if (game)
				config_game_begin("navigation");
			settings_ui_t s = {.game = game};
			open_page(&s, CAT_HOME);
			// Display, Controls, Saves, Advanced, (Games folder in the library,) Reset
			assert(s.count == (game ? 5 : 6) && has_row(&s, F_FOLDER) == !game);
			assert(!has_row(&s, CATEGORY_ROW + CAT_CHEATS)); // pause menu only
			for (unsigned p = 0; p < sizeof(pages) / sizeof(pages[0]); p++) {
				int page = pages[p];
				open_page(&s, page);
				for (int i = 0; i < s.count; i++) {
					int item = s.items[i];
					if (item >= 0 && item < F_COUNT)
						assert(frontend_visible(&s, item) || item == F_HEADING);
					if (item >= CORE_ROW && item < CONTROL_ROW)
						fprintf(inventory, "%s\t%s\t%s\t%s\n", model ? "New" : "Old", game ? "game" : "library",
							options_key(item - CORE_ROW), categories[page]);
				}
				if (page == CAT_SAVES)
					assert(has_row(&s, F_REWIND) == (model && game));
				if (page == CAT_CONTROLS)
					assert(has_row(&s, F_SIDE) == model && has_row(&s, F_CONTROLLER));
				if (page == CAT_ADVANCED)
					assert(has_row(&s, F_HEADING) && !has_row(&s, F_RECOMMENDED));   // no tested preset here
				char name[100];
				snprintf(name, sizeof(name), "%s-%s-%s", model ? "new" : "old", game ? "game" : "global", categories[page]);
				for (char *c = name; *c; c++)
					if (*c == ' ' || *c == '&')
						*c = '-';
				check_rows(&s);
				shot(name);
				flist_snap(&s.list, 0, s.count, list_rows(&s));
				draw_settings(&s);
				shot_end();
			}
			s.selected[CAT_SAVES] = 1;
			open_page(&s, CAT_HOME);
			open_page(&s, CAT_SAVES);
			assert(s.selected[CAT_SAVES] == 1);
			assert(parent(CAT_DEBUG) == CAT_ADVANCED && parent(CAT_BINDINGS) == CAT_CONTROLS &&
				   parent(CAT_DISPLAY) == CAT_HOME);
			// The cursor never rests on the Compatibility heading.
			open_page(&s, CAT_ADVANCED);
			for (int i = 0; i < s.count; i++)
				if (s.items[i] == F_HEADING) { s.selected[CAT_ADVANCED] = i; rebuild(&s); assert(!heading(s.selected[CAT_ADVANCED], &s)); }
			if (game)
				config_game_end();
		}
	fclose(inventory);

	// A tested preset: P marker on its value, warning header, Reset to recommended.
	for (int model = 0; model < 2; model++) {
		g_new3ds = model;
		config_reset_defaults();
		config_game_begin("Silent Hill");
		input_touch_disc_log("CD-ROM ID: SLUS00707\n");
		settings_ui_t sh = {.game = true};
		open_page(&sh, CAT_ADVANCED);
		assert(game_preset_tested() == model);   // New: overclock entry tested_with = 1
		assert(!strcmp(advanced_header(&sh), model ? advanced_tested : advanced_untested));
		assert(has_row(&sh, F_RECOMMENDED) == model);
		int clock = options_index("pcsx_rearmed_psxclock");
		assert(clock >= 0);
#ifdef PSX3D_PS1_OVERCLOCK_PRESETS
		assert(preset_row(&sh, CORE_ROW + clock) == model);
		if (model) assert(!strcmp(options_get("pcsx_rearmed_psxclock"), "100"));   // 93..100 reachable
#endif
		options_cycle(clock, 1);
		assert(overridden(&sh, CORE_ROW + clock) && !preset_row(&sh, CORE_ROW + clock));
		if (model) {
			// Reset to recommended clears Advanced only: Display stays.
			config_game_set_screen(1);
			const char *keys[] = {options_game_key("pcsx_rearmed_psxclock")};
			char k0[32]; snprintf(k0, sizeof(k0), "%s", keys[0]); keys[0] = k0;
			config_game_reset_advanced(keys, 1);
			assert(!overridden(&sh, CORE_ROW + clock) && config_game_overridden("screen"));
			shot("new-tested-Advanced");
			flist_snap(&sh.list, 0, sh.count, list_rows(&sh)); draw_settings(&sh); shot_end();
		}
		assert(config_game_reset());
		config_game_end();
		game_presets_reset();
	}

	// Widescreen "off" policy: offered with a note, not taken from the all-games choice.
	g_new3ds = false;
	config_reset_defaults();
	config_set_screen_mode(2);   // all games: Widescreen
	config_game_begin("ws-off");
	test_ws_policy = WS_TOO_SLOW;
	config_ws_sync();
	assert(config_ws_fix() && config_screen_mode() == 0 && !test_ws_enabled);
	assert(!strcmp(screen_note(), "Widescreen lowers the speed a lot on Old 3DS."));
	config_game_set_screen(2);
	config_ws_sync();
	assert(config_screen_mode() == 2 && test_ws_enabled);   // explicit per-game choice
	assert(config_game_clear("screen"));
	config_ws_sync();
	assert(config_screen_mode() == 0 && !test_ws_enabled);
	// Migration: an old Show-all override still counts as an explicit choice.
	gamecfg_set_int("preset_widescreen", 1);
	config_ws_sync();
	assert(test_ws_enabled);
	assert(config_game_reset());
	test_ws_policy = -1;
	config_game_end();
	config_reset_defaults();

	// Controller: all-games choice, per-game override, preset layer.
	g_new3ds = true;
	config_reset_defaults();
	assert(config_controller() == 0);
	settings_ui_t lib = {0};
	change(&lib, F_CONTROLLER, 1);
	assert(g_cfg.controller == 1 && config_controller() == 1);
	config_save(); config_load();
	assert(g_cfg.controller == 1 && contains(PSX3D_CONFIG, "controller=1\n"));
	config_game_begin("controller");
	settings_ui_t pad = {.game = true};
	assert(config_controller() == 1);
	change(&pad, F_CONTROLLER, 1);
	assert(config_controller() == 0 && overridden(&pad, F_CONTROLLER));
	assert(config_game_clear("controller") && config_controller() == 1);
	assert(config_game_reset());
	config_game_end();
	// Rewind is on by default on New, a per-game choice.
	config_game_begin("rewind");
	assert(config_rewind_on());
	settings_ui_t rw = {.game = true};
	change(&rw, F_REWIND, 1);
	assert(!config_rewind_on() && gamecfg_int("rewind", 1) == 0);
	assert(config_game_reset() && config_rewind_on());
	config_game_end();
	g_new3ds = false;
	config_game_begin("rewind-old");
	assert(!config_rewind_on());
	config_game_end();
	config_reset_defaults();

	// Preserve the full existing cheat capacity, not just the ordinary option count.
	settings_ui_t cheats_fixture={.game=true};cheat_fixture_count=256;
	open_page(&cheats_fixture,CAT_CHEATS);assert(cheats_fixture.count==256);
	cheat_fixture_count=0;
	// Debug rows only with debug.
	settings_ui_t conditional={0};
	open_page(&conditional,CAT_ADVANCED);assert(!has_row(&conditional,CATEGORY_ROW+CAT_DEBUG));
	g_cfg.debug=1;open_page(&conditional,CAT_ADVANCED);assert(has_row(&conditional,CATEGORY_ROW+CAT_DEBUG));
	g_cfg.debug=0;
	// Isolated overrides and legacy screen.
	config_reset_defaults();
	write_text(PSX3D_DIR "/games.ini", "navigation=2\n");
	config_game_begin("navigation");
	assert(config_game_overridden("screen"));
	assert(config_game_clear("screen") && !config_game_overridden("screen"));
	config_game_set_screen(1);
	gamecfg_set_int("rewind", 1);
	gamecfg_set("other", "keep");
	assert(config_game_clear("screen") && !gamecfg_get("screen") && config_screen_mode() == 0);
	assert(gamecfg_int("rewind", 0) == 1 && !strcmp(gamecfg_get("other"), "keep"));
	assert(!config_game_clear("renderer"));
	input_control_cycle(0, 1);
	config_controls_save();
	assert(config_control_overridden(0));
	config_control_clear(0);
	assert(!config_control_overridden(0) && input_map_is_default(0));
	assert(config_game_reset() && !gamecfg_get("other"));
	config_game_end();
	// Raw core settings in the library are all-games values.
	int n = options_count();
	for (int i = 0; i < n; i++)
		if (!strcmp(options_key(i), "pcsx_rearmed_frameskip_type")) {
			const char *v = options_cycle(i, 1);
			assert(!strcmp(config_core_override(options_key(i)), v));
		}
	// Forced options have no row and ignore saved values (docs/settings.md section 3).
	assert(options_index("pcsx_rearmed_dithering") < 0 && options_index("pcsx_rearmed_spu_reverb") < 0);
	config_set_user_core_override("pcsx_rearmed_dithering", "disabled");
	assert(!strcmp(options_get("pcsx_rearmed_dithering"), "enabled"));
	assert(!strcmp(options_get("pcsx_rearmed_spu_reverb"), "enabled"));
	assert(!strcmp(options_get("pcsx_rearmed_gteregsunneeded"), "enabled"));   // Old default
	// Header tap means Back.
	assert(ui_header_back(KEY_A) == KEY_A);
	// Remote navigation keys go through input_remote_key() now; only the
	// capture-only debug page toggle stays gated on an open Settings.
	assert(!settings_ui_debug_key("debug-on"));
	active = true;
	assert(settings_ui_debug_key("debug-on") && capture_debug);
	assert(!settings_ui_debug_key("down"));
	assert(settings_ui_debug_key("debug-off") && !capture_debug);
	active = false;
	options_clear();
	clear_overrides();
	FT_Done_Face(title_face);
	FT_Done_Face(body_face);
	FT_Done_FreeType(ft);
	screen_note_checks();
	puts("PASS: widescreen notes (no fix, costly warning, large cost) on both consoles");
	puts("PASS: page inventory, visibility, cursor memory and heading skip, tested-preset header/P/reset to "
		 "recommended, widescreen off policy, Controller and Rewind layers, forced options, production drawing");
}
