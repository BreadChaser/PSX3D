// One settings navigator for the library and pause menu. Runs only while idle.
#include "devtools.h"
#include "psx3d.h"
#include "statsview.h"
#include "widescreen.h"
#include "audio_resample.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Available only inside this navigator; the network mailbox authenticates first.
static bool active, capture_debug, resume_requested;
bool settings_ui_active(void) { return active; }
bool settings_ui_debug_capture(void) { return active && capture_debug; }
// Volatile inspection only: no config write, reset on closing settings.
bool settings_ui_debug_key(const char *name)
{
	if (!active || (strcmp(name, "debug-on") && strcmp(name, "debug-off")))
		return false;
	capture_debug = !strcmp(name, "debug-on");
	return true;
}

enum {
	CAT_HOME,
	CAT_DISPLAY,
	CAT_CONTROLS,
	CAT_SAVES,
	CAT_ADVANCED,
	CAT_RESET,
	CAT_CHEATS,
	CAT_BINDINGS,
	CAT_DEBUG,
	CAT_COUNT
};
// The menu tree is fixed by docs/settings.md (tests/settings/spec.c checks it).
static const char *const categories[] = {"Settings",
										 "Display",
										 "Controls",
										 "Saves",
										 "Advanced",
										 "Reset all settings",
										 "Cheats",
										 "Button mappings",
										 "Debug"};
enum { F_SCREEN, F_SMOOTH, F_FPS, F_CONTROLLER, F_SIDE,
       F_REWIND, F_HEADING, F_RECOMMENDED, F_RECORD, F_TONE, F_THEME, F_COUNT };
static const char *const labels[] = {
 [F_SCREEN]="Screen", [F_SMOOTH]="Smoothing", [F_FPS]="FPS counter",
 [F_CONTROLLER]="Controller",
 [F_SIDE]="Bottom-screen buttons", [F_REWIND]="Rewind", [F_HEADING]="Compatibility",
 [F_RECOMMENDED]="Reset to recommended", [F_RECORD]="Record audio", [F_TONE]="Test tone",
 [F_THEME]="Theme"
};
static const char *const tips[] = {
 [F_SCREEN]="Widescreen only in games with a widescreen fix.",
 [F_SMOOTH]="Soften pixels when enlarging the picture.",
 [F_FPS]="Show the frame counter on the bottom screen.",
 [F_CONTROLLER]="Analog starts the controller in analog mode.",
 [F_SIDE]="Choose the side for touch buttons.",
 [F_REWIND]="Hold the rewind shortcut to go back.",
 [F_RECOMMENDED]="Remove this game's Advanced changes.",
 [F_RECORD]="Capture 30 seconds for troubleshooting.", [F_TONE]="Play a tone to check audio output.",
 [F_THEME]="Change the colours of the menus.",
};
// Longer help, shown on the top screen while the row is selected.
static const char *const helps[] = {
 [F_SCREEN]="Original keeps the shape of a TV picture. Stretch fills the screen. Widescreen shows a wider view in games that have a widescreen fix; other games use Original.",
 [F_SMOOTH]="Soften enlarged pixels. Turn off for a sharper picture.",
 [F_FPS]="FPS shows new pictures per second and game speed on the bottom screen. Detailed adds more numbers. Tap the counter to switch views.",
 [F_CONTROLLER]="Digital starts the DualShock in digital mode; games that use the analog sticks switch it themselves. Analog starts it in analog mode, for games that support the sticks but don't switch. The analog mode shortcut still works.",
 [F_SIDE]="Place the touch buttons on the right or left side. The menu button and logo move to the other side.",
 [F_REWIND]="Save recent moments so you can hold the Rewind button to go back. Needs free memory.",
 [F_RECOMMENDED]="Remove this game's changes to the Advanced options, so its tested settings apply again. Display, Controls and Saves are kept.",
 [F_RECORD]="Record 30 seconds of game audio for troubleshooting.",
 [F_TONE]="Play a steady tone to check audio output.",
 [F_THEME]="Choose the colours of the menus and the library backdrop. Synthwave is the original look; the others are calmer. Game pictures and cover art are not changed.",
};
static const char *const category_tips[] = {
 [CAT_DISPLAY]="Picture shape, smoothing, FPS counter and theme.",
 [CAT_CONTROLS]="Buttons, touch buttons and the controller.",
 [CAT_CHEATS]="Turn this game's cheat codes on or off.",
 [CAT_SAVES]="Memory cards and rewind.",
 [CAT_ADVANCED]="Frameskip, CPU speed and compatibility options.",
 [CAT_RESET]="Restore defaults; asks for confirmation.",
 [CAT_DEBUG]="Troubleshooting tools.",
 [CAT_BINDINGS]="Change physical buttons, touch buttons and hotkeys.",
};
// Advanced page header (docs/settings.md section 2.3; the text is checked).
static const char *const advanced_tested = "This game has tested settings. Changing them can slow it down or break it.";
static const char *const advanced_untested = "This game hasn't been tested yet; these are the standard settings.";
static const char *const advanced_library = "Games with tested settings keep their tested values for these options.";
// The technical core options of the Advanced page, in menu order.
static const char *const advanced_keys[] = {
	"pcsx_rearmed_frameskip_type", "pcsx_rearmed_psxclock", NULL, // NULL: the Compatibility heading
	"pcsx_rearmed_alt_flip", "pcsx_rearmed_gpu_slow_llists", "pcsx_rearmed_cd_turbo",
	"pcsx_rearmed_nostalls", "pcsx_rearmed_nolddelay", "pcsx_rearmed_gteregsunneeded",
	"pcsx_rearmed_nogteflags", "pcsx_rearmed_nosmccheck", "pcsx_rearmed_icache_emulation",
	"pcsx_rearmed_exception_emulation", "pcsx_rearmed_nocompathacks",
};
#define ADVANCED_KEYS (int)(sizeof(advanced_keys) / sizeof(advanced_keys[0]))
static char message[64];    // result of Add code, shown in the tip line
static int message_frames;
#define CORE_ROW 1000
#define CONTROL_ROW 2000
#define CHEAT_ROW 3000
#define CATEGORY_ROW 4000
#define MAX_ROWS 256
#define VISIBLE_ROWS 6

typedef struct {
	bool game;
	int page, selected[CAT_COUNT], items[MAX_ROWS], count;
	flist_t list;
} settings_ui_t;

static bool frontend_visible(settings_ui_t *s, int i)
{
	// Only physical limits hide a row (docs/settings.md section 1).
	if (i == F_SIDE && !g_new3ds)
		return false;
	if (i == F_THEME && s->game)   // one look for the whole app
		return false;
	if (i == F_REWIND && (!s->game || !g_new3ds))
		return false;
	if (i == F_RECOMMENDED && (!s->game || !game_preset_tested()))
		return false;
	if ((i == F_RECORD || i == F_TONE) && (!s->game || !(g_cfg.debug || capture_debug)))
		return false;
	return true;
}
static void add(settings_ui_t *s, int item)
{
	if (s->count < MAX_ROWS)
		s->items[s->count++] = item;
}
static void add_frontend(settings_ui_t *s, int item)
{
	if (frontend_visible(s, item))
		add(s, item);
}
static void add_core(settings_ui_t *s, const char *key)
{
	int i = options_index(key);
	if (i >= 0)
		add(s, CORE_ROW + i);
}
static void build_page(settings_ui_t *s, int page)
{
	s->count = 0;
	options_count();   // refresh the shown core options (debug can change them)
	switch (page) {
	case CAT_HOME:
		// Cheats live in the pause menu only (settings_ui_cheats).
		for (int i = CAT_DISPLAY; i <= CAT_RESET; i++) {
			settings_ui_t probe = {.game = s->game};
			build_page(&probe, i);
			if (i == CAT_RESET || probe.count)
				add(s, CATEGORY_ROW + i);
		}
		break;
	case CAT_DISPLAY:
		add_frontend(s, F_SCREEN); add_frontend(s, F_SMOOTH); add_frontend(s, F_FPS);
		add_frontend(s, F_THEME);
		break;
	case CAT_CONTROLS:
		add(s, CATEGORY_ROW + CAT_BINDINGS);
		add_frontend(s, F_CONTROLLER);
		add_core(s, "pcsx_rearmed_analog_combo");
		add_core(s, "pcsx_rearmed_analog_axis_modifier");
		add_frontend(s, F_SIDE);
		break;
	case CAT_SAVES:
		add_core(s, "pcsx_rearmed_memcard1");
		add_core(s, "pcsx_rearmed_memcard2");
		add_frontend(s, F_REWIND);
		break;
	case CAT_ADVANCED:
		for (int i = 0; i < ADVANCED_KEYS; i++)
			if (advanced_keys[i]) add_core(s, advanced_keys[i]);
			else add(s, F_HEADING);
		add_frontend(s, F_RECOMMENDED);
		if (g_cfg.debug || capture_debug)
			add(s, CATEGORY_ROW + CAT_DEBUG);
		break;
	case CAT_DEBUG:
		add_frontend(s, F_RECORD); add_frontend(s, F_TONE);
		add_core(s, "pcsx_rearmed_display_info");
		break;
	case CAT_BINDINGS:
		for (int i = 0; i < input_controls_count(); i++)
			add(s, CONTROL_ROW + i);
		break;
	case CAT_CHEATS:
		for (int i = 0; i < cheats_count(); i++)
			add(s, CHEAT_ROW + i);
		break;
	}
}
static bool heading(int i, void *ud)
{
	settings_ui_t *s = ud;
	return i >= 0 && i < s->count && s->items[i] == F_HEADING;
}
static void rebuild(settings_ui_t *s)
{
	build_page(s, s->page);
	int *sel = &s->selected[s->page];
	if (*sel >= s->count)
		*sel = s->count ? s->count - 1 : 0;
	if (heading(*sel, s) && s->count > 1)
		*sel = *sel + 1 < s->count ? *sel + 1 : *sel - 1;
}
// The games/<game>.ini key a row's value is stored under in a game's menu.
static const char *override_key(int item)
{
	static const char *const keys[] = {
	 [F_SCREEN]="screen", [F_SMOOTH]="smooth", [F_FPS]="show_fps",
	 [F_SIDE]="touch_click_left", [F_CONTROLLER]="controller", [F_REWIND]="rewind",
	 [F_THEME]="theme"};
	if (item >= CORE_ROW && item < CONTROL_ROW)
		return options_game_key(options_key(item - CORE_ROW));
	return item >= 0 && item < F_COUNT && item < (int)(sizeof(keys) / sizeof(keys[0])) ? keys[item] : NULL;
}
static bool overridden(settings_ui_t *s, int item)
{
	if (!s->game)
		return false;
	const char *key = override_key(item);
	return key && config_game_overridden(key);
}
static const char *preset_key(int item)
{
	if (item >= CORE_ROW && item < CONTROL_ROW) {
		const char *key = options_key(item - CORE_ROW);
		if (!strcmp(key, "pcsx_rearmed_frameskip_type")) return "frameskip";
	#ifdef PSX3D_PS1_OVERCLOCK_PRESETS
		if (!strcmp(key, "pcsx_rearmed_psxclock")) return "psxclock";
#endif
		// Technical options keep the preset key name (core key minus prefix).
		return game_preset_core_value(key) ? key + 13 : "";
	}
	if (item == F_CONTROLLER) return "controller";
	if (item >= CONTROL_ROW && item < CHEAT_ROW) {
		const char *key = input_control_key(item - CONTROL_ROW);
		int slot = input_control_slot(item - CONTROL_ROW) - INPUT_PHYSICAL_N;
		bool stick = key && !strcmp(key, "touch_stick");
		bool touch_mode = game_preset_sets("touch_controls");
		if ((stick || (slot >= 0 && slot < 6)) && touch_mode &&
		    game_capabilities().touch_controls != TOUCH_CONTROLS_AUTO)
			return "touch_controls";
		if (stick) return game_preset_sets("stick") ? "stick" : touch_mode ? "touch_controls" : "stick";
		static const char *const keys[] = {"touch_l2", "touch_r2", "touch_l3", "touch_r3", "touch_x1", "touch_x2"};
		if (slot >= 0 && slot < 6) {
			if (game_preset_sets(keys[slot])) return keys[slot];
			const char *need = slot < 2 ? "l2r2" : slot < 4 ? "l3r3" : slot == 4 ? "fast_forward" : "rewind";
			return game_preset_sets(need) ? need : touch_mode ? "touch_controls" : need;
		}
	}
	return "";
}
static bool preset_row(settings_ui_t *s, int item)
{
	const char *key = preset_key(item);
	if (!s->game || !game_preset_sets(key)) return false;
	// Derived touch defaults follow the effective capability too. A user's
	// capability override is not evidence that the preset supplied the binding.
	if (!strcmp(key, "rewind") && config_rewind_on()) return false;
	if (overridden(s, item)) return false;
	if (item >= CONTROL_ROW && item < CHEAT_ROW) {
		int slot = input_control_slot(item - CONTROL_ROW);
		if (slot >= 0 && input_map_raw(slot) != INPUT_BIND_PRESET) return false;
		const char *key = input_control_key(item - CONTROL_ROW);
		if (key && !strcmp(key, "touch_stick") && g_cfg.touch_stick >= 0) return false;
	}
	return true;
}
static const char *row_label(int i, void *ud)
{
	settings_ui_t *s = ud;
	int item = s->items[i];
	if (item == CATEGORY_ROW + CAT_RESET && s->game)
		return "Reset this game";
	if (item >= CATEGORY_ROW)
		return categories[item - CATEGORY_ROW];
	if (item >= CHEAT_ROW) {
		return ui_fit(cheats_desc(item - CHEAT_ROW), 0.5f, 250);
	}
	if (item >= CONTROL_ROW) {
		static char control[80];
		int row = item - CONTROL_ROW;
		snprintf(control, sizeof(control), "%s%s", input_control_label(row, NULL),
				 config_control_overridden(row) ? " *" : preset_row(s, item) ? " P" : "");
		return control;
	}
	static char text[80];
	snprintf(text, sizeof(text), "%s%s", item >= CORE_ROW ? options_desc(item - CORE_ROW) : labels[item],
			 overridden(s, item) ? " *" : preset_row(s, item) ? " P" : "");
	return text;
}
static const char *row_value(int i, void *ud)
{
	settings_ui_t *s = ud;
	int item = s->items[i];
	if (item >= CATEGORY_ROW)
		return item == CATEGORY_ROW + CAT_RESET ? "" : ">";
	if (item >= CHEAT_ROW)
		return cheats_on(item - CHEAT_ROW) ? "On" : cheats_forced(item - CHEAT_ROW) ? "Auto" : "Off";
	if (item >= CONTROL_ROW)
		return input_control_value(item - CONTROL_ROW, NULL);
	if (item >= CORE_ROW)
		return options_value(item - CORE_ROW);
	switch (item) {
	case F_HEADING:
	case F_RECOMMENDED:
		return "";
	case F_CONTROLLER:
		return config_controller() ? "Analog" : "Digital";
	case F_SCREEN:
		return (const char *const[]){"Original", "Stretch", "Widescreen"}[config_screen_mode()];
	case F_SMOOTH:
		return g_cfg.smooth ? "On" : "Off";
	case F_FPS:
		return (const char *const[]){"Off", "FPS", "Detailed"}[g_cfg.show_fps];
	case F_THEME:
		return theme()->name;
	case F_SIDE:
		return g_cfg.touch_click_left ? "Left side" : "Right side";
	case F_REWIND:
		return config_rewind_on() ? (rewind_active() ? "On" : "Unavailable") : "Off";
	default:
		return ">";
	}
}
static int parent(int page)
{
 if (page == CAT_BINDINGS) return CAT_CONTROLS;
 if (page == CAT_DEBUG) return CAT_ADVANCED;
 return CAT_HOME;
}
static void open_page(settings_ui_t *s, int page)
{
	s->page = page;
	rebuild(s);
	s->list.init = false;
}
static void sync_picture(void) { config_ws_sync(); }
// In a game, why Widescreen is missing or what it costs on this console.
static const char *screen_note(void)
{
	int policy = ws_console_policy();
	if (config_ws_fix())
		return policy == WS_TOO_SLOW ? g_new3ds ? "Widescreen lowers the speed a lot on New 3DS."
				: "Widescreen lowers the speed a lot on Old 3DS."
			: policy != WS_COSTLY ? NULL
			: g_new3ds ? "Widescreen can lower the speed on New 3DS."
			: "Widescreen can lower the speed on Old 3DS.";
	return "This game has no widescreen fix.";
}
// The Advanced page starts with a line about this game's tested settings.
static const char *advanced_header(settings_ui_t *s)
{
	if (s->page != CAT_ADVANCED)
		return NULL;
	return !s->game ? advanced_library : game_preset_tested() ? advanced_tested : advanced_untested;
}
static float list_top(settings_ui_t *s) { return advanced_header(s) ? 62 : 40; }
static float row_height(settings_ui_t *s) { return s->page == CAT_HOME ? 20 : 23; }
static int list_rows(settings_ui_t *s) { return s->page == CAT_HOME ? 8 : advanced_header(s) ? 5 : VISIBLE_ROWS; }
static void draw_settings(void *ud)
{
	settings_ui_t *s = ud;
	int sel = s->selected[s->page];
	const theme_t *th = theme();
	u32 muted = th->muted;
	C2D_DrawRectSolid(0, 0, .1f, 320, 240, th->bg);
	ui_menu_header(categories[s->page], s->game ? ui_fit(emu_game_title(), .42f, 150) : "All games");
	const char *header = advanced_header(s);
	if (header)
		ui_text_wrap(12, 32, .39f, header == advanced_tested ? th->help_impact : muted, 296.0f, header);
	int rows = list_rows(s);
	flist_draw_sections(&s->list, s->count, sel, 0, list_top(s), 320, row_height(s), rows, row_label,
			   s->page == CAT_BINDINGS ? input_control_value : row_value, s,
			   th->accent[sel & 3], heading);
	if (s->count > rows) {
		float h = rows * 23.0f;
		C2D_DrawRectSolid(317, list_top(s) + h * s->list.scroll / s->count, .3f, 2, h * rows / s->count, muted);
	}
	if (!s->count)
		ui_text_ex(NULL, 160, 90, .48f, muted, C2D_AlignCenter,
				   s->page == CAT_CHEATS ? "No cheats for this game yet" : "No options available");
	int item = s->count ? s->items[sel] : -1;
	const char *tip = "A open category";
	if (item >= CATEGORY_ROW)
		tip = category_tips[item - CATEGORY_ROW];
	else if (item >= CHEAT_ROW)
		tip = "X add a GameShark code";
	else if (item >= CONTROL_ROW)
		tip =
			s->game ? "X use inherited mapping    Y reset controls" : "X reset all button and touch mappings";
	else if (item >= CORE_ROW)
		tip = options_impact(item - CORE_ROW);
	else if (item == F_SCREEN && s->game && screen_note())
		tip = screen_note();
	else if (item >= 0 && item < F_COUNT)
		tip = tips[item];
	if (message_frames > 0)
		ui_text_ex(NULL, 12, 183, .39f, th->message, 0, message);
	else if (tip && s->page != CAT_HOME)
		ui_text_ex(NULL, 12, 183, .39f, muted, 0, tip);
	// In a game every value belongs to that game; unchanged ones follow all games.
	if (s->game && item >= 0 && item < CONTROL_ROW && item != F_REWIND && (item >= CORE_ROW || override_key(item)))
		ui_text_ex(NULL, 12, 199, .4f, th->text, 0,
				   overridden(s, item) ? "* Changed for this game    X inherit" : preset_row(s, item) ? "P Tested setting for this game" : "Same as all games");
	// Category rows open with A; Left/Right only changes values.
	ui_text_ex(NULL, 160, 222, .39f, muted, C2D_AlignCenter,
			   s->page == CAT_HOME ? "A open    B back"
			   : s->page == CAT_CHEATS ? "A on/off    X add code    B back"
			   : item >= CATEGORY_ROW ? "A open    L/R category    B back"
			   : "Left/Right change    L/R category    B back");
}
static void change(settings_ui_t *s, int item, int dir)
{
	if (item >= CATEGORY_ROW) {
		open_page(s, item - CATEGORY_ROW);
		return;
	}
	if (item >= CHEAT_ROW) {
		cheats_toggle(item - CHEAT_ROW);
		return;
	}
	if (item >= CONTROL_ROW) {
		if (!s->game) config_user_changed(input_control_key(item - CONTROL_ROW));
		input_control_cycle(item - CONTROL_ROW, dir);
		config_controls_save();
		return;
	}
	if (item >= CORE_ROW) {
		options_cycle(item - CORE_ROW, dir);
		return;
	}
	if (!s->game && override_key(item)) config_user_changed(override_key(item));
	switch (item) {
	case F_CONTROLLER:
		if (s->game) gamecfg_set_int("controller", !config_controller());
		else g_cfg.controller ^= 1;
		input_controller_apply(true);
		break;
	case F_RECOMMENDED:
		if (ui_confirm_action("Reset to recommended?",
				"Remove this game's changes to the Advanced options. Its tested settings apply again; "
				"Display, Controls and Saves are kept.", "A  Reset to recommended", true)) {
			const char *keys[ADVANCED_KEYS];
			int n = 0;
			for (int i = 0; i < ADVANCED_KEYS; i++)
				if (advanced_keys[i]) keys[n++] = options_game_key(advanced_keys[i]) ? strdup(options_game_key(advanced_keys[i])) : NULL;
			config_game_reset_advanced(keys, n);
			for (int i = 0; i < n; i++) free((void *)keys[i]);
		}
		break;
	case F_SCREEN:
		if (s->game) {
			// Without a widescreen fix the choices are Original and Stretch.
			int n = config_ws_fix() ? 3 : 2;
			config_game_set_screen((config_screen_mode() + dir + n) % n);
			sync_picture();
		} else {
			config_user_changed("stretch");
			config_user_changed("core.pcsx_rearmed_psx3d_widescreen");
			config_set_screen_mode((config_screen_mode() + dir + 3) % 3);
		}
		break;
	case F_SMOOTH:
		g_cfg.smooth ^= 1;
		break;
	case F_FPS:
		if (s->game) {
			statsview_cycle();   // steps forward; twice is one step back
			if (dir < 0)
				statsview_cycle();
		}
		else
			g_cfg.show_fps = (g_cfg.show_fps + dir + 3) % 3;
		break;
	case F_THEME:
		// the menus redraw in the new colours at once
		config_user_changed("theme");
		g_cfg.theme = (g_cfg.theme + dir + theme_count()) % theme_count();
		break;
	case F_SIDE:
		g_cfg.touch_click_left ^= 1;
		input_touch_reset();
		break;
	case F_REWIND:
		gamecfg_set_int("rewind", !config_rewind_on());
		options_mark_updated();
		if (config_rewind_on())
			rewind_start();
		else
			rewind_stop();
		break;
	case F_RECORD:
		audio_capture_start();
		resume_requested = true;
		break;
	case F_TONE:
		audio_test_tone_toggle();
		resume_requested = true;
		break;
	}
}
// Top screen: the library or game picture, with the selected row's help.
static void (*draw_behind)(void *);
static void draw_help(void *ud)
{
	settings_ui_t *s = ud;
	if (draw_behind)
		draw_behind(NULL);
	int item = s->count ? s->items[s->selected[s->page]] : -1;
	if (item >= CATEGORY_ROW || item < 0 || item >= CHEAT_ROW)
		return;
	bool preset = preset_row(s, item);
	const char *title = item >= CONTROL_ROW ? input_control_label(item - CONTROL_ROW, NULL)
		: item >= CORE_ROW ? options_desc(item - CORE_ROW) : labels[item];
	const char *impact = item >= CORE_ROW && item < CONTROL_ROW ? options_impact(item - CORE_ROW)
		: item == F_SCREEN && s->game ? screen_note() : NULL;
	const char *help = item >= CONTROL_ROW ? input_controls_help()
		: item >= CORE_ROW ? options_info(item - CORE_ROW) : helps[item];
	char evidence_help[768];
	if (preset) {
		snprintf(evidence_help, sizeof(evidence_help), "Preset evidence: %s. %s",
			game_preset_evidence(preset_key(item)), help ? help : "");
		help = evidence_help;
		impact = "P Tested setting for this game; you can change it";
	}
	ui_help_panel(preset ? 100 : 150, title, impact, help);
}
static bool run_settings(bool game, int page, void (*draw_top)(void *))
{
	active = true;
	resume_requested = false;
	static settings_ui_t memory[2];
	settings_ui_t *s = &memory[game];
	s->game = game;
	open_page(s, page);
	while (aptMainLoop()) {
		if (dev_tick(game) || dev_ui_pending())
			break;
		input_poll();
		u32 k = ui_header_back(input_keys_down());
		if (k & KEY_B) {
			sfx_play(SFX_BACK);
			// Cheats is opened from the pause menu: B returns there.
			if (s->page == CAT_HOME || s->page == CAT_CHEATS)
				break;
			open_page(s, parent(s->page));
			k = 0;
		}
		int *sel = &s->selected[s->page];
		if (s->count && (k & (KEY_DOWN | KEY_UP))) {
			do *sel = (*sel + (k & KEY_DOWN ? 1 : s->count - 1)) % s->count;
			while (heading(*sel, s));
			sfx_play(SFX_MOVE);
		}
		if (s->page != CAT_HOME && s->page != CAT_CHEATS && (k & (KEY_L | KEY_R))) {
			int page = s->page;
			if (page == CAT_DEBUG)
				page = CAT_ADVANCED;
			if (page == CAT_BINDINGS)
				page = CAT_CONTROLS;
			settings_ui_t probe = {.game = s->game};
			do {
				page += k & KEY_R ? 1 : -1;
				if (page < CAT_DISPLAY)
					page = CAT_ADVANCED;
				if (page > CAT_ADVANCED)
					page = CAT_DISPLAY;
				build_page(&probe, page);
			} while (!probe.count);
			open_page(s, page);
			sel = &s->selected[s->page];
			k = 0;
		}
		if (k & KEY_TOUCH) {
			touchPosition p;
			hidTouchRead(&p);
			int row = flist_row_at(&s->list, list_top(s), row_height(s), p.py);
			if (p.py >= list_top(s) && p.py < list_top(s) + list_rows(s) * row_height(s) &&
			    row >= 0 && row < s->count && !heading(row, s)) {
				if (row == *sel)
					k |= KEY_A;
				else
					*sel = row;
			}
		}
		int item = s->count ? s->items[*sel] : -1;
		if ((k & KEY_X) && s->page == CAT_CHEATS) {
			const char *result = cheats_add_dialog();
			if (result) {
				snprintf(message, sizeof(message), "%s", result);
				message_frames = 180;
			}
			sync_picture();
		} else if ((k & KEY_X) && game && item >= CONTROL_ROW && item < CHEAT_ROW) {
			config_control_clear(item - CONTROL_ROW);
		} else if ((k & (KEY_X | KEY_Y)) && s->page == CAT_BINDINGS) {
			if (ui_confirm("Reset controls?",
						   game ? "Use the inherited button and touch mappings for this game?"
								: "Restore this console's default button and touch mappings?",
						   game))
				config_controls_defaults();
		} else if ((k & KEY_X) && game && item < CONTROL_ROW && override_key(item)) {
			config_game_clear(override_key(item));
			if (item == F_REWIND)
				rewind_stop();
			sync_picture();
		}
		int dir = k & KEY_LEFT ? -1 : k & (KEY_RIGHT | KEY_A) ? 1 : 0;
		if (item >= 0 && dir && ((item < CATEGORY_ROW && item != F_RECOMMENDED) || (k & KEY_A))) {
			if (item == CATEGORY_ROW + CAT_RESET) {
				if (ui_confirm(game ? "Reset this game?" : "Reset all settings?",
							   game ? "Remove this game's settings and control overrides. Return to its "
									  "preset and inherited defaults. Saves and cheats are kept."
									: "Restore this console's defaults and control mappings. Game overrides "
									  "and saves are kept.",
							   game)) {
					if (game) {
						config_game_reset();
						rewind_stop();
						sync_picture();
					} else {
						config_reset_defaults();
						music_play(false);
						music_play(true);
					}
				}
			} else {
				change(s, item, dir);
				config_game_store();   // so the row shows its * at once
			}
			sfx_play(SFX_SELECT);
		}
		if (message_frames > 0)
			message_frames--;
		if(resume_requested) break;
		rebuild(s);
		flist_update(&s->list, s->selected[s->page], s->count, list_rows(s));
		if (!game)
			music_update();
		ui_begin();
		draw_behind = draw_top;
		video_present_vsync(game, draw_help, draw_settings, s);
	}
	active = false;
	capture_debug = false;
	config_save();
	return resume_requested;
}

bool settings_ui_run(bool game, void (*draw_top)(void *)) { return run_settings(game, CAT_HOME, draw_top); }
bool settings_ui_cheats(void) { return run_settings(true, CAT_CHEATS, NULL); }
