// Hosts the PCSX-ReARMed libretro core: implements the callbacks it needs
// and runs the per-frame loop plus the in-game menu.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "psx3d.h"
#ifdef PSX3D_PICA
#include "../../core/plugins/gpu_pica/gpu_pica.h"
#endif

#define SRM_CHECK_FRAMES 180
#define DISPLAY_HZ 59.831        // 3DS LCD refresh rate   // look for memory card writes every ~3 seconds

static char game_name[PATH_LEN];   // save/state file name (disc 1's name)
static char game_title[PATH_LEN];  // what the menu shows
static char osd_msg[128];
static int  osd_frames;
static double fps_target = 60.0;
static float fps_measured, emu_ms, present_ms;
static bool core_initialized;
static bool has_audio;
static retro_audio_buffer_status_callback_t abuf_status_cb;
static struct retro_disk_control_callback disk;   // multi-disc games (via .m3u)
static bool have_disk;

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
	static char last[512];
	static unsigned repeats;
	char buf[512];
	va_list ap;
	(void)level;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	// some core messages repeat every frame; writing those to the SD card
	// costs real time, so only log a message once until it changes
	if (strcmp(buf, last) == 0) {
		repeats++;
		return;
	}
	if (repeats)
		log_printf("(previous message repeated %u times)\n", repeats);
	repeats = 0;
	snprintf(last, sizeof(last), "%s", buf);
	log_printf("%s", buf);
}

static void osd(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(osd_msg, sizeof(osd_msg), fmt, ap);
	va_end(ap);
	osd_frames = 120;
	log_printf("osd: %s\n", osd_msg);
}

static bool environment(unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		*(bool *)data = true;
		return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		*(const char **)data = PSX3D_BIOS_DIR;
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = PSX3D_SAVE_DIR;
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
	case RETRO_ENVIRONMENT_SET_VARIABLES:
		options_register(data);
		return true;
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		*(unsigned *)data = 1;   // v1: options come with descriptions
		return true;
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
		options_register_v1(data);
		return true;
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:   // the core uses this even for English
		options_register_v1(((const struct retro_core_options_intl *)data)->us);
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data;
		v->value = options_get(v->key);
		if (sw_prof) {
			// each option once, so a hardware run can be replayed in the simulator
			static char seen[2048];
			char tag[80];
			snprintf(tag, sizeof(tag), "|%s|", v->key);
			if (!strstr(seen, tag) && strlen(seen) + strlen(tag) < sizeof(seen)) {
				strcat(seen, tag);
				log_printf("opt %s=%s\n", v->key, v->value ? v->value : "(none)");
			}
		}
		return v->value != NULL;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool *)data = options_take_updated();
		return true;
	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
		return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback *)data)->log = core_log;
		return true;
	case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER: {
		struct retro_framebuffer *fb = data;
		size_t pitch;
		void *p = video_framebuffer(fb->width, fb->height, &pitch);
		if (!p)
			return false;
		fb->data = p;
		fb->pitch = pitch;
		fb->format = RETRO_PIXEL_FORMAT_RGB565;
		fb->memory_flags = 0;
		return true;
	}
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
		const struct retro_system_av_info *av = data;
		fps_target = av->timing.fps;
		return true;
	}
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
		return true;
	case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK: {
		// lets the core's "auto" frameskip skip drawing when audio runs low
		const struct retro_audio_buffer_status_callback *cb = data;
		abuf_status_cb = cb ? cb->callback : NULL;
		return true;
	}
	case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
		return true;
	case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
		*(unsigned *)data = 0;   // only the basic interface
		return true;
	case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
		memcpy(&disk, data, sizeof(disk));
		have_disk = disk.get_num_images && disk.set_image_index && disk.set_eject_state;
		return true;
	case RETRO_ENVIRONMENT_SET_MESSAGE: {
		const struct retro_message *m = data;
		osd("%s", m->msg);
		return true;
	}
	case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: {
		const struct retro_message_ext *m = data;
		if (m->target != RETRO_MESSAGE_TARGET_LOG)
			osd("%s", m->msg);
		return true;
	}
	default:
		{
			static u32 seen[8];   // log each unknown request once
			if (cmd < 256 && !(seen[cmd >> 5] & (1u << (cmd & 31)))) {
				seen[cmd >> 5] |= 1u << (cmd & 31);
				log_printf("env: unhandled %u\n", cmd);
			}
		}
		return false;
	}
}

static unsigned dup_frames, late_frames;   // per log line: frames not drawn, frames started late

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
	if (!data)
		dup_frames++;
	video_frame(data, w, h, pitch);
}

static void audio_sample_cb(int16_t l, int16_t r)
{
	int16_t s[2] = { l, r };
	audio_write(s, 1);
}

static bool audio_muted;
static int speed_icon;     // 1 fast-forward, -1 rewind, 0 normal: shown top right

// ▶▶ or ◀◀ in the top right corner while fast-forward or rewind is held
static void game_draw_top(void *ud)
{
	(void)ud;
	if (!speed_icon)
		return;
	static float t;
	t += 1.0f / 60.0f;
	float x = 400 - 46 + ui_depth(-0.6f), y = 10, h = 16, w = 13;
	u8 a = (u8)(200 + 55 * sinf(t * 6));
	u32 c = C2D_Color32(255, 255, 255, a), sh = C2D_Color32(0, 0, 0, 120);
	C2D_DrawRectSolid(x - 6, y - 4, 0.8f, 2 * w + 12, h + 8, C2D_Color32(0, 0, 0, 90));
	for (int k = 0; k < 2; k++) {
		float x0 = x + k * w;
		for (int pass = 0; pass < 2; pass++) {
			float o = pass ? 0 : 1;
			u32 col = pass ? c : sh;
			if (speed_icon > 0)
				C2D_DrawTriangle(x0 + o, y + o, col, x0 + o, y + h + o, col, x0 + w + o, y + h / 2 + o, col, 0.81f + pass * 0.01f);
			else
				C2D_DrawTriangle(x0 + w + o, y + o, col, x0 + w + o, y + h + o, col, x0 + o, y + h / 2 + o, col, 0.81f + pass * 0.01f);
		}
	}
}   // fast-forward / rewind frames make no sound

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
	if (audio_muted)
		return frames;
	return audio_write(data, frames);
}

static void input_poll_cb(void) { input_poll(); }

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
	return input_state(port, device, index, id);
}

// ---------------------------------------------------------------- files

static void make_path(char *out, size_t len, const char *dir, const char *ext)
{
	snprintf(out, len, "%s/%s%s", dir, game_name, ext);
}

static bool write_file(const char *path, const void *data, size_t size)
{
	FILE *f = fopen(path, "wb");
	bool ok;
	if (!f)
		return false;
	ok = fwrite(data, 1, size, f) == size;
	fclose(f);
	return ok;
}

static size_t read_file(const char *path, void *data, size_t size)
{
	FILE *f = fopen(path, "rb");
	size_t n;
	if (!f)
		return 0;
	n = fread(data, 1, size, f);
	fclose(f);
	return n;
}

// memory card 1 lives in the core's SAVE_RAM; we persist it as <game>.srm
static u8 *srm_shadow;

static void srm_load(void)
{
	char path[PATH_LEN];
	void *mem = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);

	free(srm_shadow);
	srm_shadow = NULL;
	if (!mem || !size)
		return;
	make_path(path, sizeof(path), PSX3D_SAVE_DIR, ".srm");
	if (read_file(path, mem, size))
		log_printf("loaded memory card %s\n", path);
	srm_shadow = malloc(size);
	if (srm_shadow)
		memcpy(srm_shadow, mem, size);
}

static void srm_save(bool force)
{
	char path[PATH_LEN];
	void *mem = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);

	if (!mem || !size)
		return;
	if (!force && srm_shadow && memcmp(srm_shadow, mem, size) == 0)
		return;
	make_path(path, sizeof(path), PSX3D_SAVE_DIR, ".srm");
	if (write_file(path, mem, size)) {
		if (srm_shadow)
			memcpy(srm_shadow, mem, size);
		log_printf("saved memory card %s\n", path);
	}
}

static int state_slot;
static void bottom_backlight(bool on);
static unsigned menu_disc;
static int game_frames;      // frames since the game (re)started showing   // disc picked in the menu (left/right), inserted with A

static void state_ext(char *ext, size_t len, int slot)
{
	if (slot == STATE_AUTO)
		snprintf(ext, len, ".sta");
	else
		snprintf(ext, len, ".st%d", slot);
}

static bool state_save_to(int slot)
{
	char path[PATH_LEN], ext[16];
	size_t size = retro_serialize_size();
	void *buf = size ? malloc(size) : NULL;
	bool ok = false;

	if (!buf || !retro_serialize(buf, size)) {
		osd(buf ? "Save state failed" : "Not enough memory to save a state");
		free(buf);
		return false;
	}
	state_ext(ext, sizeof(ext), slot);
	make_path(path, sizeof(path), PSX3D_STATE_DIR, ext);
	if (write_file(path, buf, size)) {
		make_path(path, sizeof(path), PSX3D_STATE_DIR, "");
		state_thumb_write(path, slot);
		ok = true;
	}
	free(buf);
	return ok;
}


static bool state_load_from(int slot)
{
	char path[PATH_LEN], ext[16];
	size_t size = retro_serialize_size();
	void *buf = size ? malloc(size) : NULL;
	bool ok = false;

	state_ext(ext, sizeof(ext), slot);
	make_path(path, sizeof(path), PSX3D_STATE_DIR, ext);
	if (!buf)
		osd("Not enough memory to load a state");
	else if (read_file(path, buf, size) == 0)
		osd("Slot %d is empty", slot);
	else if (!retro_unserialize(buf, size))
		osd("That save is from an incompatible version");
	else
		ok = true;
	free(buf);
	if (ok)
		rewind_reset();
	return ok;
}

static void state_save(void)
{
	osd(state_save_to(state_slot) ? "Saved to slot %d" : "Could not write slot %d", state_slot);
}

static void state_load(void)
{
	if (state_load_from(state_slot))
		osd("Loaded slot %d", state_slot);
}

// ---------------------------------------------------------------- menu

enum { M_RESUME, M_SAVE, M_LOAD, M_DISC, M_CHEATS, M_SETTINGS, M_RESET, M_EXIT,
       M_SCREEN, M_3D, M_RES3D, M_SMOOTH, M_FPS, M_CONTROLS, M_REWIND, M_AUTOSAVE, M_CORE, M_RECORD, M_TONE, M_COUNT };
#define M_FIRST_SETTING M_SCREEN

// items on the main page or the settings page; the debug ones only with
// debug=1 in config.ini, the disc swap only for multi-disc games
static int menu_items(int *items, bool settings)
{
	int n = 0;
	for (int i = settings ? M_FIRST_SETTING : 0; i < (settings ? M_COUNT : M_FIRST_SETTING); i++) {
		if ((i == M_RECORD || i == M_TONE) && !g_cfg.debug)
			continue;
		if (i == M_DISC && !(have_disk && disk.get_num_images() > 1))
			continue;
		items[n++] = i;
	}
	return n;
}

typedef struct {
	int sel, main_sel;
	bool settings;
	bool core_page, cheat_page, ctl_page;
	int ctl_sel;
	flist_t ctl_fl;
	int cheat_sel;
	flist_t cheat_fl;
	int core_sel, core_scroll;
	flist_t fl, core_fl;
	int items[M_COUNT], nitems;
	char val[M_COUNT][48];
} menu_t;


#define MENU_ROW_H 24
#define MENU_ROWS 7
#define CORE_ROWS 7

static const u32 menu_accent_cols[4] = { 0xff3c1ee6, 0xff14cdfa, 0xff78be1e, 0xffe66e28 }; // ABGR: red, yellow, green, blue

static const char *const menu_names[M_COUNT];
static void menu_draw_top(void *ud)
{
	C2D_Font f = libui_font();
	C2D_DrawRectSolid(0, 0, 0.6f, 400, 240, C2D_Color32(2, 4, 16, 170));
	float dx = ui_depth(-0.8f);
	ui_text_ex(f, 201 + dx, 87, 1.1f, C2D_Color32(0, 0, 0, 150), C2D_AlignCenter, "PAUSED");
	ui_text_ex(f, 200 + dx, 86, 1.1f, C2D_Color32(255, 255, 255, 255), C2D_AlignCenter, "PAUSED");
	ui_text_ex(NULL, 200 + dx, 136, 0.55f, C2D_Color32(170, 185, 230, 255), C2D_AlignCenter, game_title);
	// a thin PS-coloured rule under the word
	for (int i = 0; i < 4; i++)
		C2D_DrawRectSolid(140 + i * 30 + dx, 128, 0.65f, 28, 2, menu_accent_cols[i]);
	menu_t *m = ud;
	if (m && m->core_page)
		ui_help_panel(152, NULL, options_impact(m->core_sel), options_info(m->core_sel));
	else if (m && m->settings && !m->ctl_page && !m->cheat_page && m->sel < m->nitems)
		ui_help_for(152, menu_names[m->items[m->sel]]);
}

static const char *const menu_names[M_COUNT] = {
	[M_RESUME] = "Resume", [M_SAVE] = "Save state", [M_LOAD] = "Load state", [M_DISC] = "Swap disc", [M_CHEATS] = "Cheats",
	[M_SETTINGS] = "Settings", [M_RESET] = "Reset game", [M_EXIT] = "Exit to library",
	[M_SCREEN] = "Screen", [M_3D] = "3D", [M_RES3D] = "3D resolution", [M_SMOOTH] = "Smoothing",
	[M_FPS] = "Show FPS", [M_CONTROLS] = "Controls", [M_REWIND] = "Rewind",
	[M_AUTOSAVE] = "Continue where I left off", [M_CORE] = "Core options", [M_RECORD] = "Debug: record audio",
	[M_TONE] = "Debug: test tone",
};

static const char *menu_label(int i, void *ud)
{
	menu_t *m = ud;
	return menu_names[m->items[i]];
}

static const char *menu_value(int i, void *ud)
{
	menu_t *m = ud;
	return m->val[m->items[i]];
}

static const char *core_label(int i, void *ud)
{
	(void)ud;
	static char b[48];
	snprintf(b, sizeof(b), "%.30s", options_desc(i));
	return b;
}

static const char *core_value(int i, void *ud)
{
	(void)ud;
	static char b[24];
	snprintf(b, sizeof(b), "%.14s", options_get(options_key(i)));
	return b;
}

#define MENU_Y0 40

static void menu_header(const char *title, const char *right)
{
	ui_text_ex(libui_font(), 10, 6, 0.5f, C2D_Color32(235, 238, 255, 255), 0, title);
	if (right)
		ui_text_ex(NULL, 310, 9, 0.42f, C2D_Color32(110, 120, 160, 255), C2D_AlignRight, right);
	for (int i = 0; i < 4; i++)
		C2D_DrawRectSolid(10 + i * 18, 26, 0.2f, 16, 2, menu_accent_cols[i]);
}

static const char *cheat_label(int i, void *ud)
{
	(void)ud;
	static char b[48];
	snprintf(b, sizeof(b), "%.30s", cheats_desc(i));
	return b;
}

static const char *cheat_value(int i, void *ud)
{
	(void)ud;
	return cheats_on(i) ? "on" : cheats_forced(i) ? "auto" : "off";
}

// Widescreen: the game's own widescreen code when it has one (nothing gets
// culled at the edges), otherwise the GTE squeeze
static void sync_widescreen(void)
{
	bool want = config_screen_mode() == 2;
	bool native = cheats_widescreen(want);
	config_set_ws_native(want && native);
}

static const char *ctl_label(int i, void *ud)
{
	(void)ud;
	return input_map_key_name(i);
}

static const char *ctl_value(int i, void *ud)
{
	(void)ud;
	return input_ps1_name(input_map_get(i));
}

static void controls_save(void)
{
	for (int i = 0; i < INPUT_REMAP_N; i++) {
		char key[16];
		snprintf(key, sizeof(key), "map_%s", input_map_key_name(i));
		gamecfg_set_int(key, input_map_get(i));
	}
}

static void controls_load(void)
{
	input_map_reset();
	for (int i = 0; i < INPUT_REMAP_N; i++) {
		char key[16];
		snprintf(key, sizeof(key), "map_%s", input_map_key_name(i));
		const char *v = gamecfg_get(key);
		if (v)
			input_map_set(i, atoi(v));
	}
}

static void menu_draw_bottom(void *ud)
{
	menu_t *m = ud;
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, C2D_Color32(6, 9, 24, 255));
	u32 hint = C2D_Color32(110, 120, 160, 255);

	if (m->ctl_page) {
		menu_header("CONTROLS", "this game");
		flist_draw(&m->ctl_fl, INPUT_REMAP_N, m->ctl_sel, 0, MENU_Y0, 320, MENU_ROW_H, CORE_ROWS,
			ctl_label, ctl_value, m, menu_accent_cols[m->ctl_sel & 3]);
		ui_text_ex(NULL, 160, 222, 0.42f, hint, C2D_AlignCenter, "Left/Right change    X reset    B back");
		return;
	}

	if (m->cheat_page) {
		char n[24];
		snprintf(n, sizeof(n), "%d", cheats_count());
		menu_header("CHEATS", n);
		flist_draw(&m->cheat_fl, cheats_count(), m->cheat_sel, 0, MENU_Y0, 320, MENU_ROW_H, CORE_ROWS,
			cheat_label, cheat_value, m, menu_accent_cols[m->cheat_sel & 3]);
		if (!cheats_count())
			ui_text_ex(NULL, 160, 110, 0.5f, C2D_Color32(170, 185, 230, 255), C2D_AlignCenter,
				"No cheats for this game yet");
		ui_text_ex(NULL, 160, 222, 0.42f, hint, C2D_AlignCenter, "A on/off    X add code    B back");
		return;
	}

	if (m->core_page) {
		menu_header("CORE OPTIONS", NULL);
		flist_draw(&m->core_fl, options_count(), m->core_sel, 0, MENU_Y0, 320, MENU_ROW_H, CORE_ROWS,
			core_label, core_value, m, menu_accent_cols[m->core_sel & 3]);
		ui_text_ex(NULL, 160, 222, 0.42f, hint, C2D_AlignCenter, "Left/Right change    B back");
		return;
	}

	static const char *const s3d_names[] = { "off", "screen depth", "real (exp.)" };
	static const char *const screen_names[] = { "4:3", "stretch", "widescreen" };
	memset(m->val, 0, sizeof(m->val));
	if (have_disk && disk.get_num_images)
		snprintf(m->val[M_DISC], 48, "disc %u of %u", menu_disc + 1, disk.get_num_images());
	snprintf(m->val[M_SCREEN], 48, "%s", screen_names[config_screen_mode()]);
	snprintf(m->val[M_3D], 48, "%s", s3d_names[g_cfg.stereo]);
	snprintf(m->val[M_SMOOTH], 48, "%s", g_cfg.smooth ? "on" : "off");
	snprintf(m->val[M_FPS], 48, "%s", g_cfg.show_fps ? "on" : "off");
	snprintf(m->val[M_RES3D], 48, "%s", g_cfg.half3d ? "half (faster)" : "full");
	snprintf(m->val[M_REWIND], 48, "%s", gamecfg_int("rewind", 0) ? (rewind_active() ? "on" : "on (unavailable)") : "off");
	snprintf(m->val[M_AUTOSAVE], 48, "%s", g_cfg.autosave ? "on" : "off");
	int changed = 0;
	for (int i = 0; i < INPUT_REMAP_N; i++)
		changed += !input_map_is_default(i);
	snprintf(m->val[M_CONTROLS], 48, "%s", changed ? "custom" : "default");

	m->nitems = menu_items(m->items, m->settings);
	char clock[16];
	time_t now = time(NULL);
	strftime(clock, sizeof(clock), "%H:%M", gmtime(&now));
	menu_header(m->settings ? "SETTINGS" : "MENU", clock);
	flist_draw(&m->fl, m->nitems, m->sel, 0, MENU_Y0, 320, MENU_ROW_H, MENU_ROWS, menu_label, menu_value, m,
		menu_accent_cols[m->sel & 3]);
	ui_text_ex(NULL, 160, 222, 0.42f, hint, C2D_AlignCenter,
		m->settings ? "Left/Right change    B back" : "A select    B resume");
}

static void menu_open_page(menu_t *m, bool settings)
{
	if (settings && !m->settings)
		m->main_sel = m->sel;
	m->settings = settings;
	m->nitems = menu_items(m->items, settings);
	m->sel = settings ? 0 : m->main_sel;
	m->fl.init = false;
}

// returns false when the user wants to leave the game
static bool menu_run(void)
{
	bottom_backlight(true);
	static menu_t m;   // keep the core options scroll position between visits
	m.core_page = false;
	m.cheat_page = false;
	m.ctl_page = false;
	m.settings = false;
	if (have_disk && disk.get_image_index)
		menu_disc = disk.get_image_index();
	m.main_sel = 0;
	menu_open_page(&m, false);
	sfx_play(SFX_SELECT);

	audio_pause(true);
	srm_save(false);

	while (aptMainLoop()) {
		input_poll();
		u32 k = input_keys_down();

		// touch a row to select it, touch it again to use it
		if (!m.core_page && !m.cheat_page && !m.ctl_page && (k & KEY_TOUCH)) {
			touchPosition tp;
			hidTouchRead(&tp);
			int row = flist_row_at(&m.fl, MENU_Y0, MENU_ROW_H, tp.py);
			if (tp.py >= MENU_Y0 && row >= 0 && row < m.nitems) {
				if (row == m.sel)
					k |= KEY_A;
				else {
					m.sel = row;
					sfx_play(SFX_MOVE);
				}
			}
		}

		if (m.ctl_page) {
			if (k & KEY_B) {
				m.ctl_page = false;
				sfx_play(SFX_BACK);
			}
			if (k & KEY_DOWN) { m.ctl_sel = (m.ctl_sel + 1) % INPUT_REMAP_N; sfx_play(SFX_MOVE); }
			if (k & KEY_UP) { m.ctl_sel = (m.ctl_sel - 1 + INPUT_REMAP_N) % INPUT_REMAP_N; sfx_play(SFX_MOVE); }
			if (k & (KEY_RIGHT | KEY_A)) { input_map_cycle(m.ctl_sel, 1); controls_save(); sfx_play(SFX_MOVE); }
			if (k & KEY_LEFT) { input_map_cycle(m.ctl_sel, -1); controls_save(); sfx_play(SFX_MOVE); }
			if (k & KEY_X) { input_map_reset(); controls_save(); sfx_play(SFX_SELECT); }
		} else if (m.cheat_page) {
			int n = cheats_count();
			if (k & KEY_B) {
				m.cheat_page = false;
				sfx_play(SFX_BACK);
			}
			if (n) {
				if (k & KEY_DOWN) { m.cheat_sel = (m.cheat_sel + 1) % n; sfx_play(SFX_MOVE); }
				if (k & KEY_UP) { m.cheat_sel = (m.cheat_sel - 1 + n) % n; sfx_play(SFX_MOVE); }
				if (k & (KEY_A | KEY_LEFT | KEY_RIGHT)) { cheats_toggle(m.cheat_sel); sfx_play(SFX_SELECT); }
			}
			if (k & KEY_X) {
				const char *msg = cheats_add_dialog();
				if (msg)
					osd("%s", msg);
				sync_widescreen();   // the reload dropped the automatic widescreen code
			}
		} else if (m.core_page) {
			int n = options_count();
			if (k & KEY_B) {
				m.core_page = false;
				sfx_play(SFX_BACK);
			}
			if (n) {
				if (k & KEY_DOWN) { m.core_sel = (m.core_sel + 1) % n; sfx_play(SFX_MOVE); }
				if (k & KEY_UP) { m.core_sel = (m.core_sel - 1 + n) % n; sfx_play(SFX_MOVE); }
				if (k & (KEY_RIGHT | KEY_R | KEY_A)) { options_cycle(m.core_sel, 1); sfx_play(SFX_MOVE); }
				if (k & (KEY_LEFT | KEY_L)) { options_cycle(m.core_sel, -1); sfx_play(SFX_MOVE); }
			}
		} else {
			int n = m.nitems;
			if (k & KEY_DOWN) { m.sel = (m.sel + 1) % n; sfx_play(SFX_MOVE); }
			if (k & KEY_UP) { m.sel = (m.sel - 1 + n) % n; sfx_play(SFX_MOVE); }
			if (k & KEY_B) {
				if (!m.settings)
					break;
				menu_open_page(&m, false);
				sfx_play(SFX_BACK);
			}
			int dir = (k & KEY_RIGHT) ? 1 : (k & KEY_LEFT) ? -1 : (k & KEY_A) ? 1 : 0;
			if (dir && (k & (KEY_LEFT | KEY_RIGHT)) && m.settings)
				sfx_play(SFX_MOVE);
			if (dir) {
				switch (m.items[m.sel]) {
				case M_RESUME: if (k & KEY_A) goto resume; break;
				case M_SAVE:
				case M_LOAD:
					if (k & KEY_A) {
						bool saving = m.items[m.sel] == M_SAVE;
						char prefix[PATH_LEN];
						make_path(prefix, sizeof(prefix), PSX3D_STATE_DIR, "");
						int slot = state_slot;
						if (states_run(saving, prefix, game_title, &slot) >= 0) {
							state_slot = slot;
							if (saving)
								state_save();
							else
								state_load();
							goto resume;
						}
						state_slot = slot;
						m.fl.init = false;
					}
					break;
				case M_DISC: {
					unsigned dn = disk.get_num_images();
					if (k & KEY_A) {
						// swap like a real console: open the lid, change, close
						disk.set_eject_state(true);
						disk.set_image_index(menu_disc);
						disk.set_eject_state(false);
						osd("Disc %u inserted", menu_disc + 1);
						goto resume;
					}
					menu_disc = (menu_disc + dir + dn) % dn;
					sfx_play(SFX_MOVE);
				} break;
				case M_CONTROLS:
					if (k & KEY_A) {
						m.ctl_page = true;
						m.ctl_fl.init = false;
						sfx_play(SFX_SELECT);
					}
					break;
				case M_REWIND: {
					bool on = !gamecfg_int("rewind", 0);
					gamecfg_set_int("rewind", on);
					if (on && !rewind_start())
						osd("Rewind needs a New 3DS and free memory");
					if (!on)
						rewind_stop();
				} break;
				case M_AUTOSAVE: g_cfg.autosave ^= 1; break;
				case M_CHEATS:
					if (k & KEY_A) {
						m.cheat_page = true;
						m.cheat_fl.init = false;
						sfx_play(SFX_SELECT);
					}
					break;
				case M_SETTINGS:
					if (k & KEY_A) {
						menu_open_page(&m, true);
						sfx_play(SFX_SELECT);
					}
					break;
				case M_SCREEN: config_game_set_screen((config_screen_mode() + dir + 3) % 3); sync_widescreen(); break;
				case M_3D: {
					// "real" per-object 3D is experimental: debug builds only
					int modes = g_cfg.debug ? 3 : 2;
					g_cfg.stereo = (g_cfg.stereo + dir + modes) % modes;
					config_sync_stereo();
				} break;
				case M_SMOOTH: g_cfg.smooth ^= 1; break;
				case M_RES3D: g_cfg.half3d ^= 1; psx3d_set_half3d(g_cfg.half3d); break;
				case M_FPS: g_cfg.show_fps ^= 1; break;
				case M_CORE:
					if (k & KEY_A) {
						m.core_page = true;
						sfx_play(SFX_SELECT);
					}
					break;
				case M_RECORD:
					if (k & KEY_A) {
						osd(audio_capture_start() ? "Recording 30s of audio..." : "Could not start recording");
						goto resume;
					}
					break;
				case M_TONE:
					if (k & KEY_A) {
						osd(audio_test_tone_toggle() ? "Test tone ON" : "Test tone off");
						goto resume;
					}
					break;
				case M_RESET: if (k & KEY_A) { retro_reset(); rewind_reset(); goto resume; } break;
				case M_EXIT:
					if (k & KEY_A) {
						config_save();
						return false;
					}
					break;
				}
			}
		}

		if (m.ctl_page)
			flist_update(&m.ctl_fl, m.ctl_sel, INPUT_REMAP_N, CORE_ROWS);
		else if (m.cheat_page)
			flist_update(&m.cheat_fl, m.cheat_sel, cheats_count(), CORE_ROWS);
		else if (m.core_page)
			flist_update(&m.core_fl, m.core_sel, options_count(), CORE_ROWS);
		else
			flist_update(&m.fl, m.sel, m.nitems, MENU_ROWS);
		ui_begin();
		video_present_vsync(true, menu_draw_top, menu_draw_bottom, &m);
	}
	if (!aptShouldClose())
		goto resume;
	return false;

resume:
	sfx_play(SFX_BACK);
	config_save();
	game_frames = 0;   // dim again a moment after resuming
	audio_pause(false);
	return true;
}

// ---------------------------------------------------------------- run loop

// In game the top screen shows only the game. Everything else lives on
// the otherwise black bottom screen.
static void game_draw_bottom(void *ud)
{
	(void)ud;
	u32 dim = C2D_Color32(110, 110, 125, 255), white = C2D_Color32(230, 230, 240, 255);
	C2D_DrawRectSolid(0, 0, 0.05f, 320, 240, C2D_Color32(0, 0, 0, 255));   // in game it's always black

	if (touch_buttons_active())
		touch_buttons_draw();
	if (g_cfg.show_fps)
		ui_text(4, 222, 0.42f, dim, "%.1f fps  %.1fms", fps_measured, emu_ms);
	if (osd_frames > 0) {
		osd_frames--;
		ui_text(4, 204, 0.45f, white, "%s", osd_msg);
	}
	if (!has_audio)
		ui_text(4, 186, 0.42f, C2D_Color32(255, 160, 120, 255),
			"No sound available");
}

// In game the bottom screen goes fully dark (backlight off): it only shows
// the FPS counter. Touch still works, so tapping it opens the menu.
static bool bottom_dark;

static void bottom_backlight(bool on)
{
	if (on == !bottom_dark)
		return;
	// gsp::Lcd allows very few sessions and the HOME Menu needs one: keeping
	// ours open across a HOME press left the HOME Menu waiting forever.
	// Open it only for the switch.
	if (sw_nolcd)
		return;
	Result rc = gspLcdInit();
	if (R_FAILED(rc)) {
		log_printf("bottom backlight: gsp::Lcd unavailable (%08lx)\n", rc);
		return;
	}
	if (on)
		GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTTOM);
	else
		GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
	gspLcdExit();
	bottom_dark = !on;
	log_printf("bottom backlight %s\n", on ? "on" : "off");
}

bool emu_init(void)
{
	retro_set_environment(environment);
	retro_set_video_refresh(video_cb);
	retro_set_audio_sample(audio_sample_cb);
	retro_set_audio_sample_batch(audio_batch_cb);
	retro_set_input_poll(input_poll_cb);
	retro_set_input_state(input_state_cb);
	return true;
}

void emu_deinit(void)
{
	if (core_initialized)
		retro_deinit();
	core_initialized = false;
	free(srm_shadow);
	srm_shadow = NULL;
}

static void set_game_name(const char *path)
{
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	snprintf(game_name, sizeof(game_name), "%s", base);
	char *dot = strrchr(game_name, '.');
	if (dot)
		*dot = 0;
}

// save_name: base name for memory cards and states (NULL = from path);
// title: shown in the menu (NULL = save name)
// The HOME Menu draws on the bottom screen too: light it up while we're
// suspended, dim it again a moment after coming back.
static void game_apt(APT_HookType hook, void *param)
{
	(void)param;
	if (hook == APTHOOK_ONSUSPEND)
		bottom_backlight(true);
	else if (hook == APTHOOK_ONRESTORE)
		game_frames = 0;
}

bool emu_run_game(const char *path, const char *save_name, const char *title)
{
	struct retro_game_info info = { .path = path };
	struct retro_system_av_info av;
	u64 fps_t0;
	int fps_frames = 0, present_count = 0, srm_counter = 0;

	if (save_name)
		snprintf(game_name, sizeof(game_name), "%s", save_name);
	else
		set_game_name(path);
	snprintf(game_title, sizeof(game_title), "%s", title ? title : game_name);
	have_disk = false;
	memset(&disk, 0, sizeof(disk));
	log_printf("loading %s\n", path);
	config_game_begin(game_name);   // before loading: the core reads the widescreen option at load
#ifdef PSX3D_PICA
	// the hardware renderer's textures need the memory the library music used
	music_unload();
	video_gpu_idle();
	covers_release_gpu();
	log_printf("gpu_pica: game buffers %s, %uK GPU memory left\n",
		pica_game_begin(!g_new3ds) ? "ready" : "FAILED", (unsigned)(linearSpaceFree() / 1024));
#endif

	retro_init();
	core_initialized = true;
	if (!retro_load_game(&info)) {
		log_printf("retro_load_game failed\n");
		config_game_end();
		retro_deinit();
		core_initialized = false;
#ifdef PSX3D_PICA
		pica_game_end();
		music_reload();
#endif
		return false;
	}
	// DualShock: starts in digital mode, games switch it to analog themselves,
	// and analog-only games (Ape Escape) need it plugged in
	retro_set_controller_port_device(0, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1));

	retro_get_system_av_info(&av);
	fps_target = av.timing.fps;
	has_audio = audio_init(av.timing.sample_rate);
	srm_load();
	cheats_load(game_name);
	sync_widescreen();
	psx3d_set_half3d(g_cfg.half3d);
	psx3d_prof_on = sw_prof;
	controls_load();
	{
		char prefix[PATH_LEN];
		make_path(prefix, sizeof(prefix), PSX3D_STATE_DIR, "");
		if (g_cfg.autosave && sw_state < 0 && states_resume_prompt(prefix) && state_load_from(STATE_AUTO))
			osd("Continuing where you left off");
	}
	if (gamecfg_int("rewind", 0))
		rewind_start();
	if (sw_state >= 0) {
		state_slot = sw_state;
		log_printf("test: slot %d %s\n", sw_state, state_load_from(sw_state) ? "loaded" : "failed");
	}
	osd("%s", __ctr_svchax ? "Dynarec enabled" : "Interpreter mode (no Luma SVC access) - slow");

	fps_t0 = osGetTime();
	u64 t_run = 0, t_present = 0, t_run_max = 0;
	u64 next_deadline = svcGetSystemTick();
	aptHookCookie apt_cookie;
	aptHook(&apt_cookie, game_apt, NULL);
	while (aptMainLoop()) {
		// Pace by the clock, not by vsync: a heavy frame (Silent Hill
		// alternates ~4ms and ~17ms ones) can borrow time from the next light
		// one instead of always missing a refresh. The GPU still flips the
		// screen only at vblank, so there is no tearing.
		u64 frame_ticks = (u64)(SYSCLOCK_ARM11 / fps_target);
		u64 now_t = svcGetSystemTick();
		bool late = now_t > next_deadline + frame_ticks / 8;   // last frame overran its slot
		late_frames += late;
		if (now_t < next_deadline)
			svcSleepThread((next_deadline - now_t) * 1000000000ULL / SYSCLOCK_ARM11);
		else if (now_t > next_deadline + 3 * frame_ticks)
			next_deadline = now_t;   // far behind (loading, menu): don't race to catch up
		next_deadline += frame_ticks;

		if (abuf_status_cb) {
			unsigned occ = has_audio ? audio_occupancy() : 50;
			// auto frameskip: skip drawing when sound runs low or the frame
			// clock says we are behind (the sound buffer alone rarely shows it)
			abuf_status_cb(true, occ, occ < 20 || late);
		}
		u64 t0 = svcGetSystemTick();
		static bool rewind_hint;
		if (input_rewind() && input_fast_forward())
			input_fast_forward_off();   // rewinding ends fast-forward
		speed_icon = input_rewind() && rewind_active() ? -1 : input_fast_forward() ? 1 : 0;
		if (input_rewind() && rewind_active()) {
			// step back about 6 times a second of game time, shown at 20/s
			static int rw;
			if (++rw >= 3) {
				rw = 0;
				if (!rewind_step())
					osd("Rewound as far as it goes");
			}
			audio_muted = true;
			retro_run();   // show the rewound frame
			audio_muted = false;
		} else {
			if (input_rewind() && !rewind_hint) {
				osd("Rewind is off for this game (Settings)");
				rewind_hint = true;
			}
			if (!input_rewind())
				rewind_hint = false;
			bool ff = input_fast_forward();
			options_fast_forward(ff);
			// fast-forward: up to 3 more frames per shown frame, no sound
			for (int extra = ff ? 3 : 0; extra > 0; extra--) {
				audio_muted = true;
				retro_run();
				rewind_capture();
			}
			audio_muted = false;
			retro_run();
			rewind_capture();
#ifdef PSX3D_PICA
			if (sw_vramdump && --sw_vramdump == 0) {
				void pica_dump_vram(const char *path);
				pica_dump_vram(PSX3D_DIR "/vram.bin");
				log_printf("test: VRAM dumped\n");
			}
#endif
			if (ff)
				next_deadline = svcGetSystemTick();
		}
		{
			// real 3D only costs anything while the slider is up; a little
			// hysteresis so a slider resting near 0 doesn't toggle it
			static int below;
			float slider = slider_get();
			below = slider < 0.02f ? below + 1 : 0;
			if (g_cfg.stereo == 2 && slider >= 0.05f)
				config_set_stereo_active(true);
			else if (g_cfg.stereo != 2 || below > 30)
				config_set_stereo_active(false);
		}
		if (g_cfg.stereo == 2)
			psx3d_depth_frame();   // this frame's depth map is complete
		u64 t1 = svcGetSystemTick();
		fps_frames++;
		t_run += t1 - t0;
		if (t1 - t0 > t_run_max)
			t_run_max = t1 - t0;

		// touch the bottom screen to open the menu
		if (menu_touched()) {
			if (!menu_run())
				break;
			fps_t0 = osGetTime();
			fps_frames = present_count = 0;
			next_deadline = svcGetSystemTick();
		}

		ui_begin();
		t1 = svcGetSystemTick();
		video_present(true, speed_icon ? game_draw_top : NULL, game_draw_bottom, NULL);
		t_present += svcGetSystemTick() - t1;

		if (++game_frames == 120 && g_cfg.bottom_off && !touch_buttons_active() && !g_cfg.show_fps)
			bottom_backlight(false);

		if (++srm_counter >= SRM_CHECK_FRAMES) {
			srm_counter = 0;
			srm_save(false);
		}
		present_count++;
		u64 now = osGetTime();
		if (now - fps_t0 >= 1000) {
			fps_measured = fps_frames * 1000.0f / (now - fps_t0);
			emu_ms = t_run / (float)present_count / CPU_TICKS_PER_MSEC;
			present_ms = t_present / (float)present_count / CPU_TICKS_PER_MSEC;
			// SD writes cost a ~40ms hitch on Old 3DS: per-second stats
			// only when sent to a PC or profiling
			float wait_ms = video_take_wait_ticks() / (float)present_count / CPU_TICKS_PER_MSEC;
			float xfer_ms = video_take_xfer_ticks() / (float)present_count / CPU_TICKS_PER_MSEC;
			if (log_linked() || sw_prof || sw_stats)
				log_printf("fps %.1f  emu %.2fms (max %.2f)  present %.2fms (wait %.2f) xfer %.2f  skip %u late %u cb %d\n",
					fps_measured, emu_ms, t_run_max / (float)CPU_TICKS_PER_MSEC, present_ms,
					wait_ms, xfer_ms, dup_frames, late_frames, abuf_status_cb != NULL);
			dup_frames = late_frames = 0;
			char abuf[160];
			audio_stats(abuf, sizeof(abuf));
			if (log_linked() || sw_prof || sw_stats)
				log_printf("  %s\n", abuf);
#ifdef PSX3D_PICA
			{
				void pica_stats(char *buf, int n);
				char pb[512];
				pica_stats(pb, sizeof(pb));
				if (log_linked() || sw_prof || sw_stats)
					log_printf("  %s\n", pb);
			}
#endif
			if (sw_prof) {
				static const char *const names[6] = { "compile", "inval", "spu", "gpu", "irq", "cdr" };
				char pb[200]; int n = 0;
				for (int k = 0; k < 5; k++) {
					n += snprintf(pb + n, sizeof(pb) - n, " %s %.2fms/%u", names[k],
						psx3d_prof_t[k] / (double)fps_frames / CPU_TICKS_PER_MSEC, psx3d_prof_c[k] / (fps_frames ? fps_frames : 1));
					psx3d_prof_t[k] = 0; psx3d_prof_c[k] = 0;
				}
				log_printf("  prof (per frame):%s gpu words drawn %llu skipped %u\n", pb,
					psx3d_prof_t[5] / (fps_frames ? fps_frames : 1), psx3d_prof_c[5] / (fps_frames ? fps_frames : 1));
				psx3d_prof_t[5] = 0; psx3d_prof_c[5] = 0;
				new_dynarec_print_stats();
			}
			t_run = t_present = t_run_max = 0;
			present_count = 0;
			fps_frames = 0;
			fps_t0 = now;
		}
	}

	aptUnhook(&apt_cookie);
	input_fast_forward_off();
	options_fast_forward(false);
	log_printf("game: leaving\n");
	rewind_stop();
	if (g_cfg.autosave)
		log_printf("auto-save: %s\n", state_save_to(STATE_AUTO) ? "written" : "failed");
	bottom_backlight(true);
	srm_save(false);
	cheats_unload();
	retro_unload_game();
	config_game_end();
	audio_exit();
	retro_deinit();
	core_initialized = false;
#ifdef PSX3D_PICA
	pica_game_end();
	music_reload();
#endif
	return true;
}
