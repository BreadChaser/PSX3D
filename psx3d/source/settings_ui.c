// One settings navigator for the library and pause menu. Runs only while idle.
#include "devtools.h"
#include "psx3d.h"
#include "statsview.h"
#include <stdio.h>
#include <string.h>

// Available only inside this navigator; the network mailbox authenticates first.
static bool active, capture_debug, resume_requested;
static u32 remote_key;
bool settings_ui_active(void) { return active; }
bool settings_ui_debug_capture(void) { return active && capture_debug; }
u32 dev_ui_keys_take(void)
{
	u32 k = remote_key;
	remote_key = 0;
	return k;
}
bool settings_ui_key(const char *name)
{
	static const char *const names[] = {"a", "b", "x", "y", "up", "down", "left", "right", "l", "r"};
	static const u32 keys[] = {KEY_A,	 KEY_B,	   KEY_X,	  KEY_Y, KEY_UP,
							   KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_L, KEY_R};
	if (!active || remote_key)
		return false;
	// Volatile inspection only: no config write, reset on closing settings.
	if (!strcmp(name, "debug-on") || !strcmp(name, "debug-off")) {
		capture_debug = !strcmp(name, "debug-on");
		return true;
	}
	for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
		if (!strcmp(name, names[i])) {
			remote_key = keys[i];
			return true;
		}
	return false;
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
 [F_SCREEN]="Widescreen may change the game's picture.",
 [F_SMOOTH]="Soften pixels when enlarging the picture.",
 [F_STEREO]="Use the 3D slider for screen depth.",
 [F_FPS]="Show the frame counter on the bottom screen.",
 [F_SIDE]="Choose the side for touch buttons.",
 [F_AUDIO]="Keep sound smooth when the game runs slowly.",
 [F_SOUNDS]="Play sounds while navigating menus.", [F_MUSIC]="Play music in the game library.",
 [F_AUTO]="Keep your place when leaving a game.", [F_REWIND]="Hold the rewind shortcut to go back.",
 [F_RECORD]="Capture 30 seconds for troubleshooting.", [F_TONE]="Play a tone to check audio output."
};
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
		for (int i = CAT_DISPLAY; i <= CAT_RESET; i++)
			if (i != CAT_CHEATS || s->game)
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
static bool per_game(settings_ui_t *s, int item)
{
	return s->game && ((item >= CONTROL_ROW && item < CATEGORY_ROW) || item == F_SCREEN ||
					   item == F_REWIND || item == F_RECORD || item == F_TONE);
}
static const char *override_key(int item)
{
	if (item == F_SCREEN)
		return "screen";
	if (item == F_REWIND)
		return "rewind";
	return NULL;
}
static const char *row_label(int i, void *ud)
{
	settings_ui_t *s = ud;
	int item = s->items[i];
	if (item >= CATEGORY_ROW)
		return categories[item - CATEGORY_ROW];
	if (item >= CHEAT_ROW) {
		static char cheat[48];
		snprintf(cheat, sizeof(cheat), "%.30s", cheats_desc(item - CHEAT_ROW));
		return cheat;
	}
	if (item >= CONTROL_ROW) {
		static char control[80];
		int row = item - CONTROL_ROW;
		snprintf(control, sizeof(control), "%s%s", input_control_label(row, NULL),
				 config_control_overridden(row) ? " *" : "");
		return control;
	}
	if (item >= CORE_ROW)
		return options_desc(item - CORE_ROW);
	static char text[80];
	const char *key = override_key(item);
	snprintf(text, sizeof(text), "%s%s", labels[item],
			 s->game && key && config_game_overridden(key) ? " *" : "");
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
static void sync_picture(void)
{
	bool want = config_screen_mode() == 2;
	config_set_ws_native(want && cheats_widescreen(want));
}
static void draw_settings(void *ud)
{
	settings_ui_t *s = ud;
	int sel = s->selected[s->page];
	u32 muted = C2D_Color32(130, 150, 190, 255), white = C2D_Color32(230, 235, 255, 255);
	C2D_DrawRectSolid(0, 0, .1f, 320, 240, C2D_Color32(6, 9, 24, 255));
	ui_menu_header(categories[s->page], s->game ? "this game" : "all games");
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
		tip = item == CATEGORY_ROW + CAT_RESET ? "Restore defaults; asks for confirmation."
											   : "A open    L/R next category";
	else if (item >= CHEAT_ROW)
		tip = "This game: A on/off    X add code";
	else if (item >= CONTROL_ROW)
		tip =
			s->game ? "X use inherited mapping    Y reset controls" : "X reset all button and touch mappings";
	else if (item >= CORE_ROW)
		tip = options_impact(item - CORE_ROW);
	else if (item >= 0)
		tip = tips[item];
	if (tip && s->page != CAT_HOME)
		ui_text_ex(NULL, 12, 183, .39f, muted, 0, tip);
	if (item >= 0 && item < CATEGORY_ROW) {
		const char *scope = per_game(s, item) ? "This game" : "All games";
		const char *key = override_key(item);
		char text[100];
		snprintf(text, sizeof(text), "%s%s", scope,
				 s->game && key && config_game_overridden(key) ? "  * override - X use default" : "");
		ui_text_ex(NULL, 12, 199, .4f, white, 0, text);
	}
	ui_text_ex(NULL, 160, 222, .39f, muted, C2D_AlignCenter,
			   s->page == CAT_HOME ? "A open    B back" : "Left/Right change    L/R category    B back");
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
		if (s->game)
			config_game_set_screen((config_screen_mode() + dir + 3) % 3);
		else
			config_set_screen_mode((config_screen_mode() + dir + 3) % 3);
		if (s->game)
			sync_picture();
		break;
	case F_SMOOTH:
		g_cfg.smooth ^= 1;
		break;
	case F_STEREO:
		g_cfg.stereo = (g_cfg.stereo + dir + 2) % 2;
		config_sync_stereo();
		break;
	case F_FPS:
		if (s->game)
			statsview_cycle();
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
static bool run_settings(bool game, int page, void (*draw_top)(void *))
{
	active = true;
	resume_requested = false;
	remote_key = 0;
	static settings_ui_t memory[2];
	settings_ui_t *s = &memory[game];
	s->game = game;
	open_page(s, page);
	while (aptMainLoop()) {
		if (dev_tick(game) || dev_ui_pending())
			break;
		input_poll();
		u32 k = input_keys_down() | dev_ui_keys_take();
		if (k & KEY_B) {
			sfx_play(SFX_BACK);
			if (s->page == CAT_HOME)
				break;
			open_page(s, parent(s->page));
			k = 0;
		}
		int *sel = &s->selected[s->page];
		if (s->count && (k & (KEY_DOWN | KEY_UP))) {
			*sel = (*sel + (k & KEY_DOWN ? 1 : s->count - 1)) % s->count;
			sfx_play(SFX_MOVE);
		}
		if (s->page != CAT_HOME && (k & (KEY_L | KEY_R))) {
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
			} while (page == CAT_CHEATS && !game);
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
			cheats_add_dialog();
			sync_picture();
		} else if ((k & KEY_X) && game && item >= CONTROL_ROW && item < CHEAT_ROW) {
			config_control_clear(item - CONTROL_ROW);
		} else if ((k & (KEY_X | KEY_Y)) && s->page == CAT_BINDINGS) {
			if (ui_confirm("RESET CONTROLS?",
						   game ? "Use the inherited button and touch mappings for this game?"
								: "Restore this console's default button and touch mappings?",
						   game))
				config_controls_defaults();
		} else if ((k & KEY_X) && game && override_key(item)) {
			config_game_clear(override_key(item));
			if (item == F_REWIND)
				rewind_stop();
			sync_picture();
		}
		int dir = k & KEY_LEFT ? -1 : k & (KEY_RIGHT | KEY_A) ? 1 : 0;
		if (item >= 0 && dir && (item < CATEGORY_ROW || (k & KEY_A))) {
			if (item == CATEGORY_ROW + CAT_RESET) {
				if (ui_confirm(game ? "RESET THIS GAME?" : "RESET ALL SETTINGS?",
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
		if(resume_requested) break;
		rebuild(s);
		flist_update(&s->list, s->selected[s->page], s->count, s->page == CAT_HOME ? 8 : VISIBLE_ROWS);
		if (!game)
			music_update();
		ui_begin();
		video_present_vsync(game, draw_top, draw_settings, s);
	}
	active = false;
	capture_debug = false;
	remote_key = 0;
	config_save();
	return resume_requested;
}

bool settings_ui_run(bool game, void (*draw_top)(void *)) { return run_settings(game, CAT_HOME, draw_top); }
bool settings_ui_cheats(void) { return run_settings(true, CAT_CHEATS, NULL); }
