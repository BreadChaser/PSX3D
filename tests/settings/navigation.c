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
bool cheats_widescreen(bool on) { return on; }
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
int main(void)
{
	mkdir("sdmc:", 0700);
	mkdir("sdmc:/3ds", 0700);
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
 char all_path[1024];snprintf(all_path,sizeof(all_path),"%s/all-options.tsv",getenv("PSX3D_MOCKUPS"));
 FILE *all=fopen(all_path,"w");assert(all);
 for(int i=0;i<nopts;i++) fprintf(all,"%s\t%s\t%s\n",opts[i].key,opts[i].desc,categories[core_group(opts[i].key)]);
 fclose(all);
	assert(inventory);

	for (int model = 0; model < 2; model++)
			for (int game = 0; game < 2; game++) {
				g_new3ds = model;
				config_reset_defaults();
				if (game)
					config_game_begin("navigation");
				settings_ui_t s = {.game = game};
				open_page(&s, CAT_HOME);
				assert(s.count == 7);
				assert(!has_row(&s, CATEGORY_ROW + CAT_CHEATS)); // pause menu only
				int seen[MAX_OPTIONS] = {0}, n = options_count();
				for (int page = CAT_DISPLAY; page < CAT_COUNT; page++)
					if (page != CAT_RESET) {
						open_page(&s, page);
						for (int i = 0; i < s.count; i++) {
							int item = s.items[i];
							assert(item >= CATEGORY_ROW || item >= CORE_ROW || (item >= 0 && item < F_COUNT));
							if (item >= CORE_ROW && item < CONTROL_ROW)
								seen[item - CORE_ROW]++;
							if (item >= 0 && item < F_COUNT)
								assert(frontend_visible(&s, item));
						}
						if (page == CAT_DISPLAY)
							assert(has_row(&s, F_STEREO) == model);
						if (page == CAT_SAVES)
							assert(has_row(&s, F_REWIND) == (model && game));
						char name[100];
						snprintf(name, sizeof(name), "%s-%s-%s-%d", model ? "new" : "old",
								 game ? "game" : "global", categories[page], 0);
						for (char *c = name; *c; c++)
							if (*c == ' ' || *c == '&')
								*c = '-';
						check_rows(&s);
						shot(name);
						flist_snap(&s.list, 0, s.count, VISIBLE_ROWS);
						draw_settings(&s);
						shot_end();
					}
				for (int i = 0; i < n; i++) {
					assert(seen[i] == 1);
					fprintf(inventory, "%s\t%s\t%s\t%s\t%s\n", model ? "New" : "Old",
							"GPU", options_key(i), options_desc(i),
							categories[core_group(options_key(i))]);
				} // No missing or duplicated core settings.
				s.selected[CAT_SOUND] = 2;
				open_page(&s, CAT_HOME);
				open_page(&s, CAT_SOUND);
				assert(s.selected[CAT_SOUND] == 2);
				assert(parent(CAT_CPU) == CAT_ADVANCED && parent(CAT_BINDINGS) == CAT_CONTROLS &&
					   parent(CAT_DISPLAY) == CAT_HOME);
				if (game)
					config_game_end();
			}
	fclose(inventory);

 // Preserve the full existing cheat capacity, not just the ordinary option count.
 settings_ui_t cheats_fixture={.game=true};cheat_fixture_count=256;
 open_page(&cheats_fixture,CAT_CHEATS);assert(cheats_fixture.count==256);
 cheat_fixture_count=0;
 // Conditional rows still follow the existing parent option and debug gates.
 g_new3ds=true;config_reset_defaults();
 settings_ui_t conditional={0};
 config_set_core_override("pcsx_rearmed_frameskip_type","disabled");open_page(&conditional,CAT_PERFORMANCE);
 int base_count=conditional.count;
 config_set_core_override("pcsx_rearmed_frameskip_type","auto_threshold");open_page(&conditional,CAT_PERFORMANCE);
 assert(conditional.count==base_count);
 for(int model=0;model<2;model++) {
  g_new3ds=model;
  config_game_begin("Castlevania - Symphony of the Night");
  settings_ui_t curated={.game=true};open_page(&curated,CAT_PERFORMANCE);
  int unlisted_count=curated.count;
  input_touch_disc_log("CD-ROM ID: SLUS00067\n");open_page(&curated,CAT_PERFORMANCE);
  assert(curated.count==unlisted_count-1);
  shot(model?"new-curated-Performance":"old-curated-Performance");
  flist_snap(&curated.list,0,curated.count,VISIBLE_ROWS);draw_settings(&curated);shot_end();
  config_game_end();
 }
 g_new3ds=true;
 assert(!strcmp(options_get("pcsx_rearmed_frameskip_type"),"auto"));
 open_page(&conditional,CAT_ADVANCED);assert(!has_row(&conditional,CATEGORY_ROW+CAT_DEBUG));
 g_cfg.debug=1;open_page(&conditional,CAT_ADVANCED);assert(has_row(&conditional,CATEGORY_ROW+CAT_DEBUG));
 g_cfg.debug=0;
	// Isolated overrides, including preset renderer fallback and legacy screen.
	g_new3ds = false;
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
	// Raw core settings remain shared. Category navigation must not change their storage scope.
	int n = options_count();
	for (int i = 0; i < n; i++)
		if (!strcmp(options_key(i), "pcsx_rearmed_frameskip_type")) {
			const char *v = options_cycle(i, 1);
			assert(!strcmp(config_core_override(options_key(i)), v));
		}
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
	puts("PASS: category inventory, visibility, cursor memory, scope, individual override/reset, production "
		 "drawing and remote UI key gate");
}
