// One settings navigator for the library and pause menu. Runs only while idle.
#include "devtools.h"
#include "psx3d.h"
#include "statsview.h"
#include <stdio.h>
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
	CAT_PERFORMANCE,
	CAT_SOUND,
	CAT_CONTROLS,
	CAT_CHEATS,
	CAT_SAVES,
	CAT_ADVANCED,
	CAT_RESET,
	CAT_CPU,
	CAT_PICTURE,
	CAT_DEBUG,
	CAT_HACKS,
	CAT_BINDINGS,
	CAT_COUNT
};
static const char *const categories[] = {"Settings",
										 "Display",
										 "Performance",
										 "Sound",
										 "Controls",
										 "Cheats",
										 "Saves & general",
										 "Advanced",
										 "Reset to defaults",
										 "CPU & compatibility",
										 "Picture accuracy",
										 "Debug",
										 "Speed hacks",
										 "Button mappings"};
enum { F_SCREEN, F_SMOOTH, F_STEREO, F_FPS, F_SIDE, F_AUDIO, F_SOUNDS,
       F_MUSIC, F_AUTO, F_REWIND, F_RECORD, F_TONE, F_COUNT };
static const char *const labels[] = {
 [F_SCREEN]="Screen mode", [F_SMOOTH]="Smoothing", [F_STEREO]="3D",
 [F_FPS]="Show FPS", [F_SIDE]="Bottom-screen buttons", [F_AUDIO]="Smooth audio when slow",
 [F_SOUNDS]="Menu sounds", [F_MUSIC]="Library music", [F_AUTO]="Continue where I left off",
 [F_REWIND]="Rewind", [F_RECORD]="Record audio", [F_TONE]="Test tone"
};
static const int groups[] = {
 [F_SCREEN]=CAT_DISPLAY, [F_SMOOTH]=CAT_DISPLAY, [F_STEREO]=CAT_DISPLAY,
 [F_FPS]=CAT_DISPLAY, [F_SIDE]=CAT_CONTROLS, [F_AUDIO]=CAT_SOUND, [F_SOUNDS]=CAT_SOUND,
 [F_MUSIC]=CAT_SOUND, [F_AUTO]=CAT_SAVES, [F_REWIND]=CAT_SAVES,
 [F_RECORD]=CAT_DEBUG, [F_TONE]=CAT_DEBUG
};
static const char *const tips[] = {
 [F_SCREEN]="Widescreen only in games with a widescreen fix.",
 [F_SMOOTH]="Soften pixels when enlarging the picture.",
 [F_STEREO]="Use the 3D slider for screen depth.",
 [F_FPS]="Show the frame counter on the bottom screen.",
 [F_SIDE]="Choose the side for touch buttons.",
 [F_AUDIO]="Keep sound smooth when the game runs slowly.",
 [F_SOUNDS]="Play sounds while navigating menus.", [F_MUSIC]="Play music in the game library.",
 [F_AUTO]="Keep your place when leaving a game.", [F_REWIND]="Hold the rewind shortcut to go back.",
 [F_RECORD]="Capture 30 seconds for troubleshooting.", [F_TONE]="Play a tone to check audio output."
};
// Longer help, shown on the top screen while the row is selected.
static const char *const helps[] = {
 [F_SCREEN]="4:3 keeps the original shape. Stretch fills the screen. Widescreen shows a wider view in games that have a widescreen fix; other games use 4:3.",
 [F_SMOOTH]="Soften enlarged pixels. Turn off for a sharper picture.",
 [F_STEREO]="Screen depth puts the picture behind the screen when you raise the 3D slider.",
 [F_FPS]="FPS shows new pictures per second and game speed on the bottom screen. Detailed adds more numbers. Tap the counter to switch views.",
 [F_SIDE]="Place the touch buttons on the right or left side. The menu button and logo move to the other side.",
 [F_AUDIO]="Keep sound flowing when a game runs slowly; music and voices slow down with it.",
 [F_SOUNDS]="Play a sound when you move through menus or select an item.",
 [F_MUSIC]="Play background music while browsing your games.",
 [F_AUTO]="Save your place when you leave a game and offer to continue next time.",
 [F_REWIND]="Save recent moments so you can hold the Rewind button to go back. Needs free memory.",
 [F_RECORD]="Record 30 seconds of game audio for troubleshooting.",
 [F_TONE]="Play a steady tone to check audio output."
};
static const char *const category_tips[] = {
 [CAT_DISPLAY]="Picture shape, smoothing, 3D and the FPS counter.",
 [CAT_PERFORMANCE]="Frame skipping and disc reading.",
 [CAT_SOUND]="Game audio, menu sounds and library music.",
 [CAT_CONTROLS]="Buttons, touch buttons and the analog stick.",
 [CAT_CHEATS]="Turn this game's cheat codes on or off.",
 [CAT_SAVES]="Continue where you left off, rewind and memory cards.",
 [CAT_ADVANCED]="Options for games that need help to run correctly.",
 [CAT_RESET]="Restore defaults; asks for confirmation.",
 [CAT_CPU]="Region, BIOS and accuracy options for games that misbehave.",
 [CAT_PICTURE]="Options for games that draw incorrectly.",
 [CAT_DEBUG]="Troubleshooting tools.",
 [CAT_HACKS]="Faster but less accurate; may break some games.",
 [CAT_BINDINGS]="Change physical buttons, touch buttons and hotkeys.",
};
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

// Every core option is assigned once; unknown future options stay in Advanced.
static int core_group(const char *key)
{
	const char *k = !strncmp(key, "pcsx_rearmed_", 13) ? key + 13 : key;
	if (!strncmp(k, "frameskip_", 10) || !strcmp(k, "cd_readahead"))
		return CAT_PERFORMANCE;
	if (!strcmp(k, "dithering"))
		return CAT_DISPLAY;
	if (!strcmp(k, "spu_reverb") || !strcmp(k, "nocdaudio") || !strcmp(k, "noxadecoding"))
		return CAT_SOUND;
	if (!strncmp(k, "analog_", 7))
		return CAT_CONTROLS;
	if (!strncmp(k, "memcard", 7))
		return CAT_SAVES;
	if (!strcmp(k, "display_info"))
		return CAT_DEBUG;
	if (!strcmp(k, "alt_flip") || !strcmp(k, "gpu_slow_llists"))
		return CAT_PICTURE;
	if (!strcmp(k, "psxclock") || !strcmp(k, "nolddelay") || !strcmp(k, "nosmccheck") ||
		!strcmp(k, "gteregsunneeded") || !strcmp(k, "nogteflags") || !strcmp(k, "nostalls"))
		return CAT_HACKS;
	return CAT_CPU;
}
static bool frontend_visible(settings_ui_t *s, int i)
{
	if (i == F_STEREO && !g_new3ds)
		return false;
	// App-wide: only in the library's settings.
	if ((i == F_SOUNDS || i == F_MUSIC || i == F_AUTO) && s->game)
		return false;
	if (i == F_SIDE &&
		(!g_new3ds || (s->game && (g_cfg.show_fps == 2 || (!touch_buttons_active() && !g_cfg.show_fps)))))
		return false;
	if (i == F_REWIND && (!s->game || !game_capabilities().rewind))
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
static void rebuild(settings_ui_t *s)
{
	s->count = 0;
	if (s->page == CAT_HOME) {
		// Cheats live in the pause menu only (settings_ui_cheats).
		for (int i = CAT_DISPLAY; i <= CAT_RESET; i++)
			if (i != CAT_CHEATS)
				add(s, CATEGORY_ROW + i);
	} else if (s->page == CAT_ADVANCED) {
		add(s, CATEGORY_ROW + CAT_CPU);
		add(s, CATEGORY_ROW + CAT_HACKS);
		add(s, CATEGORY_ROW + CAT_PICTURE);
		if (g_cfg.debug || capture_debug)
			add(s, CATEGORY_ROW + CAT_DEBUG);
	} else if (s->page == CAT_BINDINGS) {
		for (int i = 0; i < input_controls_count(); i++)
			add(s, CONTROL_ROW + i);
	} else if (s->page == CAT_CHEATS) {
		for (int i = 0; i < cheats_count(); i++)
			add(s, CHEAT_ROW + i);
	} else {
		if (s->page == CAT_CONTROLS)
			add(s, CATEGORY_ROW + CAT_BINDINGS);
		for (int i = 0; i < F_COUNT; i++)
			if (groups[i] == s->page && frontend_visible(s, i))
				add(s, i);
		int n = options_count();
		for (int i = 0; i < n; i++)
			if (core_group(options_key(i)) == s->page)
				add(s, CORE_ROW + i);
	}
	int *sel = &s->selected[s->page];
	if (*sel >= s->count)
		*sel = s->count ? s->count - 1 : 0;
}
// The games/<game>.ini key a row's value is stored under in a game's menu.
static const char *override_key(int item)
{
	static const char *const keys[] = {
	 [F_SCREEN]="screen", [F_SMOOTH]="smooth", [F_STEREO]="stereo", [F_FPS]="show_fps",
	 [F_SIDE]="touch_click_left", [F_AUDIO]="audio_stretch", [F_REWIND]="rewind"};
	if (item >= CORE_ROW && item < CONTROL_ROW)
		return options_game_key(options_key(item - CORE_ROW));
	return item >= 0 && item < F_COUNT && item < (int)(sizeof(keys) / sizeof(keys[0])) ? keys[item] : NULL;
}
static bool overridden(settings_ui_t *s, int item)
{
	if (!s->game)
		return false;
	if (item >= CONTROL_ROW && item < CHEAT_ROW)
		return config_control_overridden(item - CONTROL_ROW);
	const char *key = override_key(item);
	return key && config_game_overridden(key);
}
static const char *row_label(int i, void *ud)
{
	settings_ui_t *s = ud;
	int item = s->items[i];
	if (item >= CATEGORY_ROW)
		return categories[item - CATEGORY_ROW];
	if (item >= CHEAT_ROW) {
		return ui_fit(cheats_desc(item - CHEAT_ROW), 0.5f, 250);
	}
	if (item >= CONTROL_ROW) {
		static char control[80];
		int row = item - CONTROL_ROW;
		snprintf(control, sizeof(control), "%s%s", input_control_label(row, NULL),
				 config_control_overridden(row) ? " *" : "");
		return control;
	}
	static char text[80];
	snprintf(text, sizeof(text), "%s%s", item >= CORE_ROW ? options_desc(item - CORE_ROW) : labels[item],
			 overridden(s, item) ? " *" : "");
	return text;
}
static const char *row_value(int i, void *ud)
{
	settings_ui_t *s = ud;
	int item = s->items[i];
	if (item >= CATEGORY_ROW)
		return ">";
	if (item >= CHEAT_ROW)
		return cheats_on(item - CHEAT_ROW) ? "On" : cheats_forced(item - CHEAT_ROW) ? "Auto" : "Off";
	if (item >= CONTROL_ROW)
		return input_control_value(item - CONTROL_ROW, NULL);
	if (item >= CORE_ROW)
		return options_value(item - CORE_ROW);
	switch (item) {
	case F_SCREEN:
		return (const char *const[]){"4:3", "Stretch", "Widescreen"}[config_screen_mode()];
	case F_SMOOTH:
		return g_cfg.smooth ? "On" : "Off";
	case F_STEREO:
		return g_cfg.stereo ? "Screen depth" : "Off";
	case F_FPS:
		return (const char *const[]){"Off", "FPS", "Detailed"}[g_cfg.show_fps];
	case F_SIDE:
		return g_cfg.touch_click_left ? "Left side" : "Right side";
	case F_AUDIO:
		return g_cfg.audio_stretch ? "On" : "Off";
	case F_SOUNDS:
		return g_cfg.sounds ? "On" : "Off";
	case F_MUSIC:
		return g_cfg.music ? "On" : "Off";
	case F_AUTO:
		return g_cfg.autosave ? "On" : "Off";
	case F_REWIND:
		return gamecfg_int("rewind", 0) ? (rewind_active() ? "On" : "Unavailable") : "Off";
	default:
		return ">";
	}
}
static int parent(int page)
{
 if (page == CAT_BINDINGS) return CAT_CONTROLS;
 if (page >= CAT_CPU && page <= CAT_HACKS) return CAT_ADVANCED;
 return CAT_HOME;
}
static void open_page(settings_ui_t *s, int page)
{
	s->page = page;
	rebuild(s);
	s->list.init = false;
}
static void sync_picture(void) { config_ws_sync(); }
static void draw_settings(void *ud)
{
	settings_ui_t *s = ud;
	int sel = s->selected[s->page];
	u32 muted = C2D_Color32(130, 150, 190, 255), white = C2D_Color32(230, 235, 255, 255);
	C2D_DrawRectSolid(0, 0, .1f, 320, 240, C2D_Color32(6, 9, 24, 255));
	ui_menu_header(categories[s->page], s->game ? ui_fit(emu_game_title(), .42f, 150) : "All games");
	flist_draw(&s->list, s->count, sel, 0, 40, 320, s->page == CAT_HOME ? 20 : 23,
			   s->page == CAT_HOME ? 8 : VISIBLE_ROWS, row_label,
			   s->page == CAT_BINDINGS ? input_control_value : row_value, s,
			   (const u32[]){0xff3c1ee6, 0xff14cdfa, 0xff78be1e, 0xffe66e28}[sel & 3]);
	int rows = s->page == CAT_HOME ? 8 : VISIBLE_ROWS;
	if (s->count > rows) {
		float h = rows * 23.0f;
		C2D_DrawRectSolid(317, 40 + h * s->list.scroll / s->count, .3f, 2, h * rows / s->count, muted);
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
	else if (item == F_SCREEN && s->game && !config_ws_fix())
		tip = "This game has no widescreen fix.";
	else if (item >= 0)
		tip = tips[item];
	if (message_frames > 0)
		ui_text_ex(NULL, 12, 183, .39f, C2D_Color32(255, 210, 120, 255), 0, message);
	else if (tip && s->page != CAT_HOME)
		ui_text_ex(NULL, 12, 183, .39f, muted, 0, tip);
	// In a game every value belongs to that game; unchanged ones follow all games.
	if (s->game && item >= 0 && item < CHEAT_ROW && item != F_REWIND && (item >= CORE_ROW || override_key(item)))
		ui_text_ex(NULL, 12, 199, .4f, white, 0,
				   overridden(s, item) ? "* Changed for this game    X use all-games value" : "Same as all games");
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
		input_control_cycle(item - CONTROL_ROW, dir);
		config_controls_save();
		return;
	}
	if (item >= CORE_ROW) {
		options_cycle(item - CORE_ROW, dir);
		return;
	}
	switch (item) {
	case F_SCREEN:
		if (s->game) {
			// Without a widescreen fix the choices are 4:3 and Stretch.
			int n = config_ws_fix() ? 3 : 2;
			config_game_set_screen((config_screen_mode() + dir + n) % n);
			sync_picture();
		} else
			config_set_screen_mode((config_screen_mode() + dir + 3) % 3);
		break;
	case F_SMOOTH:
		g_cfg.smooth ^= 1;
		break;
	case F_STEREO:
		g_cfg.stereo = (g_cfg.stereo + dir + 2) % 2;
		config_sync_stereo();
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
	case F_SIDE:
		g_cfg.touch_click_left ^= 1;
		input_touch_reset();
		break;
	case F_AUDIO:
		g_cfg.audio_stretch = g_cfg.audio_stretch ? 0 : 50;
		break;
	case F_SOUNDS:
		g_cfg.sounds ^= 1;
		break;
	case F_MUSIC:
		g_cfg.music ^= 1;
		if (!s->game) {
			music_play(false);
			music_play(true);
		}
		break;
	case F_AUTO:
		g_cfg.autosave ^= 1;
		break;
	case F_REWIND:
		gamecfg_set_int("rewind", !gamecfg_int("rewind", 0));
		if (gamecfg_int("rewind", 0))
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
	if (item >= CONTROL_ROW)
		ui_help_panel(150, input_control_label(item - CONTROL_ROW, NULL), NULL, input_controls_help());
	else if (item >= CORE_ROW)
		ui_help_panel(150, options_desc(item - CORE_ROW), options_impact(item - CORE_ROW),
					  options_info(item - CORE_ROW));
	else
		ui_help_panel(150, labels[item], NULL, helps[item]);
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
		u32 k = input_keys_down();
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
			*sel = (*sel + (k & KEY_DOWN ? 1 : s->count - 1)) % s->count;
			sfx_play(SFX_MOVE);
		}
		if (s->page != CAT_HOME && s->page != CAT_CHEATS && (k & (KEY_L | KEY_R))) {
			int page = s->page;
			if (page >= CAT_CPU && page <= CAT_HACKS)
				page = CAT_ADVANCED;
			if (page == CAT_BINDINGS)
				page = CAT_CONTROLS;
			do {
				page += k & KEY_R ? 1 : -1;
				if (page < CAT_DISPLAY)
					page = CAT_ADVANCED;
				if (page > CAT_ADVANCED)
					page = CAT_DISPLAY;
			} while (page == CAT_CHEATS);
			open_page(s, page);
			sel = &s->selected[s->page];
			k = 0;
		}
		if (k & KEY_TOUCH) {
			touchPosition p;
			hidTouchRead(&p);
			int row = flist_row_at(&s->list, 40, s->page == CAT_HOME ? 20 : 23, p.py);
			if (p.py >= 40 && p.py < (s->page == CAT_HOME ? 200 : 178) && row >= 0 && row < s->count) {
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
		if (item >= 0 && dir && (item < CATEGORY_ROW || (k & KEY_A))) {
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
			} else
				change(s, item, dir);
			sfx_play(SFX_SELECT);
		}
		if (message_frames > 0)
			message_frames--;
		if(resume_requested) break;
		rebuild(s);
		flist_update(&s->list, s->selected[s->page], s->count, s->page == CAT_HOME ? 8 : VISIBLE_ROWS);
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
