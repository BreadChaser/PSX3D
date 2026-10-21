// Dump the real settings menu for tools/settings/spec_check.py (docs/settings.md section 7).
// Walks every page in eight contexts and cycles every value with the production
// change() code, then restores it. Output lines (tab separated):
//   ctx HOME labels | ctx HEADER text | ctx ROW page label key values current marker | OPTION key
#define SETTINGS_NAV_TEST
#define main bottom_screen_checks
#include "../../psx3d/tests/host/check.c"
#undef main
static bool rewinding;
bool rewind_active(void) { return rewinding; }
bool rewind_start(void) { rewinding = true; return true; }
void rewind_stop(void) { rewinding = false; }
int cheats_count(void) { return 0; }
const char *cheats_desc(int i) { (void)i; return ""; }
bool cheats_on(int i) { (void)i; return false; }
bool cheats_forced(int i) { (void)i; return false; }
bool cheats_widescreen(bool on) { (void)on; return ws_available(); }
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

static const char *ctx;
static void row_key(settings_ui_t *s, int item, char *out, size_t n)
{
	if (item >= CATEGORY_ROW) snprintf(out, n, ">");
	else if (item >= CORE_ROW) {
		const char *k = options_key(item - CORE_ROW) + 13;
		snprintf(out, n, "%s", !strcmp(k, "frameskip_type") ? "frameskip" : k);
	} else if (override_key(item)) snprintf(out, n, "%s", override_key(item));
	else snprintf(out, n, "-");
	(void)s;
}
static void strip_marker(const char *label, char *out, size_t n, char *marker)
{
	snprintf(out, n, "%s", label);
	size_t len = strlen(out);
	*marker = '-';
	if (len > 2 && out[len - 2] == ' ' && (out[len - 1] == '*' || out[len - 1] == 'P')) {
		*marker = out[len - 1];
		out[len - 2] = 0;
	}
}
static void clear_row(settings_ui_t *s, int item)
{
	if (s->game) {
		const char *k = override_key(item);
		if (k) config_game_clear(k);
		if (item == F_REWIND && config_rewind_on()) rewind_start();
		config_ws_sync();
	} else config_reset_defaults();
}
static void dump_page(settings_ui_t *s, int page, const char *name)
{
	open_page(s, page);
	const char *section = name;
	for (int i = 0; i < s->count; i++) {
		int item = s->items[i];
		if (item == F_HEADING) { section = "Advanced/Compatibility"; continue; }
		char label[96], key[48], marker, values[2048] = "", current[64];
		strip_marker(row_label(i, s), label, sizeof(label), &marker);
		row_key(s, item, key, sizeof(key));
		snprintf(current, sizeof(current), "%s", row_value(i, s));
		if (item < CATEGORY_ROW && item != F_RECOMMENDED) {
			char first[64];
			snprintf(first, sizeof(first), "%s", row_value(i, s));
			for (int step = 0; step < 128; step++) {
				const char *v = row_value(i, s);
				if (step && !strcmp(v, first)) break;
				if (step) strcat(values, ", ");
				strcat(values, v);
				change(s, item, 1);
				config_game_store();
				build_page(s, page);   // rows may move (e.g. Reset to recommended)
				for (int j = 0; j < s->count; j++) if (s->items[j] == item) i = j;
			}
			clear_row(s, item);
			open_page(s, page);
			for (int j = 0; j < s->count; j++) if (s->items[j] == item) i = j;
		} else snprintf(values, sizeof(values), "-");
		printf("%s\tROW\t%s\t%s\t%s\t%s\t%s\t%c\n", ctx, item == F_RECOMMENDED ? name : section, label, key, values,
			item < CATEGORY_ROW ? current : "-", marker);
	}
}
static void dump_context(bool game)
{
	settings_ui_t s = {.game = game};
	open_page(&s, CAT_HOME);
	printf("%s\tHOME\t", ctx);
	for (int i = 0; i < s.count; i++) printf("%s%s", i ? ", " : "", row_label(i, &s));
	printf("\n");
	s.page = CAT_ADVANCED;
	printf("%s\tHEADER\t%s\n", ctx, advanced_header(&s));
	static const int pages[] = {CAT_DISPLAY, CAT_CONTROLS, CAT_SAVES, CAT_ADVANCED};
	for (unsigned p = 0; p < sizeof(pages) / sizeof(pages[0]); p++)
		dump_page(&s, pages[p], categories[pages[p]]);
}
static void begin_game(const char *disc, int policy)
{
	config_game_begin("spec");
	test_ws_policy = policy;
	if (disc) {
		char line[64]; snprintf(line, sizeof(line), "CD-ROM ID: %s\n", disc);
		game_presets_disc_log(line);
	} else game_presets_reset();
	options_mark_updated();
	config_ws_sync();
	if (game_capabilities().rewind && config_rewind_on()) rewind_start();   // as emu.c does
}
int main(int argc, char **argv)
{
	if (argc != 3) { fprintf(stderr, "usage: spec <fixture presets> <defaults.ini>\n"); return 2; }
	mkdir("sdmc:", 0700); mkdir("sdmc:/3ds", 0700); mkdir(PSX3D_DIR, 0700);
	game_presets_init(argv[1], PSX3D_DIR "/presets");
	defaults_init(argv[2]);
	static struct retro_core_option_definition defs[MAX_OPTIONS];
	for (int i = 0; option_defs_us[i].key && i < MAX_OPTIONS - 1; i++) {
		defs[i].key = option_defs_us[i].key; defs[i].desc = option_defs_us[i].desc;
		defs[i].info = option_defs_us[i].info; defs[i].default_value = option_defs_us[i].default_value;
		memcpy(defs[i].values, option_defs_us[i].values, sizeof(defs[i].values));
	}
	options_register_v1(defs);
	for (int i = 0; i < nopts; i++) printf("OPTION\t%s\n", opts[i].key);
	g_syscore_pct = 30;
	static const struct { const char *name, *disc; int policy; } games[] = {
		{"game", NULL, -1}, {"tested", "SLUS99901", WS_OFFERED}, {"wsoff", "SLUS99902", WS_TOO_SLOW}};
	for (int model = 0; model < 2; model++) {
		g_new3ds = model;
		char name[32];
		config_reset_defaults();
		snprintf(name, sizeof(name), "%s-lib", model ? "new" : "old"); ctx = name;
		test_ws_policy = -1;
		dump_context(false);
		for (unsigned g = 0; g < sizeof(games) / sizeof(games[0]); g++) {
			config_reset_defaults();
			snprintf(name, sizeof(name), "%s-%s", model ? "new" : "old", games[g].name); ctx = name;
			begin_game(games[g].disc, games[g].policy);
			dump_context(true);
			config_game_reset();
			config_game_end();
			rewind_stop();
		}
	}
	return 0;
}
