// Hosts the PCSX-ReARMed libretro core: implements the callbacks it needs
// and runs the per-frame loop plus the in-game menu.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "psx3d.h"

#define SRM_CHECK_FRAMES 180
#define DISPLAY_HZ 59.831        // 3DS LCD refresh rate   // look for memory card writes every ~3 seconds

static char game_name[PATH_LEN];   // file name without extension
static char osd_msg[128];
static int  osd_frames;
static double fps_target = 60.0;
static float fps_measured, emu_ms, present_ms;
static bool core_initialized;
static bool has_audio;
static retro_audio_buffer_status_callback_t abuf_status_cb;

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
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data;
		v->value = options_get(v->key);
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
		return false;
	}
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
	video_frame(data, w, h, pitch);
}

static void audio_sample_cb(int16_t l, int16_t r)
{
	int16_t s[2] = { l, r };
	audio_write(s, 1);
}

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
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

static void state_save(void)
{
	char path[PATH_LEN], ext[16];
	size_t size = retro_serialize_size();
	void *buf = size ? malloc(size) : NULL;

	if (!buf || !retro_serialize(buf, size)) {
		osd(buf ? "Save state failed" : "Not enough memory to save a state");
		free(buf);
		return;
	}
	snprintf(ext, sizeof(ext), ".st%d", state_slot);
	make_path(path, sizeof(path), PSX3D_STATE_DIR, ext);
	osd(write_file(path, buf, size) ? "Saved state %d" : "Could not write state %d", state_slot);
	free(buf);
}

static void state_load(void)
{
	char path[PATH_LEN], ext[16];
	size_t size = retro_serialize_size();
	void *buf = size ? malloc(size) : NULL;

	snprintf(ext, sizeof(ext), ".st%d", state_slot);
	make_path(path, sizeof(path), PSX3D_STATE_DIR, ext);
	if (!buf)
		osd("Not enough memory to load a state");
	else if (read_file(path, buf, size) == 0)
		osd("No state in slot %d", state_slot);
	else if (!retro_unserialize(buf, size))
		osd("State %d is from an incompatible version", state_slot);
	else
		osd("Loaded state %d", state_slot);
	free(buf);
}

// ---------------------------------------------------------------- menu

enum { M_RESUME, M_SAVE, M_LOAD, M_SLOT, M_SCREEN, M_SMOOTH, M_FPS, M_CORE, M_RECORD, M_TONE, M_RESET, M_EXIT, M_COUNT };

// items shown in the menu; the debug ones only with debug=1 in config.ini
static int menu_items(int *items)
{
	int n = 0;
	for (int i = 0; i < M_COUNT; i++) {
		if ((i == M_RECORD || i == M_TONE) && !g_cfg.debug)
			continue;
		items[n++] = i;
	}
	return n;
}

typedef struct {
	int sel;
	bool core_page;
	int core_sel, core_scroll;
} menu_t;


static void menu_draw_top(void *ud)
{
	(void)ud;
	ui_rect(0, 0, 400, 240, C2D_Color32(0, 0, 0, 140));
	ui_text(150, 100, 0.8f, C2D_Color32(255, 255, 255, 255), "Paused");
}

static void menu_draw_bottom(void *ud)
{
	menu_t *m = ud;
	u32 white = C2D_Color32(255, 255, 255, 255), hl = C2D_Color32(70, 90, 160, 255);
	u32 grey = C2D_Color32(170, 170, 180, 255);

	if (m->core_page) {
		ui_text(8, 4, 0.5f, white, "Core options  (L/R: change, B: back)");
		for (int i = 0; i < 12 && m->core_scroll + i < options_count(); i++) {
			int idx = m->core_scroll + i;
			float y = 24 + i * 17;
			const char *desc = options_desc(idx);
			if (idx == m->core_sel)
				ui_rect(0, y, 320, 17, hl);
			ui_text(6, y, 0.42f, white, "%.34s", desc);
			ui_text(224, y, 0.42f, grey, "%.14s", options_get(options_key(idx)));
		}
		return;
	}

	const char *labels[M_COUNT];
	char slot[32], screen[48], smooth[32], fps[32];
	snprintf(slot, sizeof(slot), "State slot: %d", state_slot);
	snprintf(screen, sizeof(screen), "Screen: %s", config_widescreen() ? "widescreen (3D games)" : "4:3");
	snprintf(smooth, sizeof(smooth), "Smoothing: %s", g_cfg.smooth ? "on" : "off");
	snprintf(fps, sizeof(fps), "Show FPS: %s", g_cfg.show_fps ? "on" : "off");
	labels[M_RESUME] = "Resume";
	labels[M_SAVE] = "Save state";
	labels[M_LOAD] = "Load state";
	labels[M_SLOT] = slot;
	labels[M_SCREEN] = screen;
	labels[M_SMOOTH] = smooth;
	labels[M_FPS] = fps;
	labels[M_CORE] = "Core options...";
	labels[M_RECORD] = "Debug: record 30s of audio";
	labels[M_TONE] = "Debug: audio test tone";
	labels[M_RESET] = "Reset game";
	labels[M_EXIT] = "Exit to game list";

	int items[M_COUNT], n = menu_items(items);
	ui_text(8, 4, 0.5f, grey, "%.40s", game_name);
	for (int i = 0; i < n; i++) {
		float y = 22 + i * 18;
		if (i == m->sel)
			ui_rect(0, y, 320, 18, hl);
		ui_text(12, y + 1, 0.5f, white, "%s", labels[items[i]]);
	}
}

// returns false when the user wants to leave the game
static bool menu_run(void)
{
	static menu_t m;   // keep the core options scroll position between visits
	m.core_page = false;
	m.sel = 0;
	int items[M_COUNT], nitems = menu_items(items);

	audio_pause(true);
	srm_save(false);

	while (aptMainLoop()) {
		input_poll();
		u32 k = input_keys_down();

		if (m.core_page) {
			int n = options_count();
			if (k & KEY_B)
				m.core_page = false;
			if (n) {
				if (k & KEY_DOWN) m.core_sel = (m.core_sel + 1) % n;
				if (k & KEY_UP) m.core_sel = (m.core_sel - 1 + n) % n;
				if (k & (KEY_RIGHT | KEY_R | KEY_A)) options_cycle(m.core_sel, 1);
				if (k & (KEY_LEFT | KEY_L)) options_cycle(m.core_sel, -1);
				if (m.core_sel < m.core_scroll) m.core_scroll = m.core_sel;
				if (m.core_sel >= m.core_scroll + 12) m.core_scroll = m.core_sel - 11;
			}
		} else {
			if (k & KEY_DOWN) m.sel = (m.sel + 1) % nitems;
			if (k & KEY_UP) m.sel = (m.sel - 1 + nitems) % nitems;
			if (k & KEY_B)
				break;
			int dir = (k & KEY_RIGHT) ? 1 : (k & KEY_LEFT) ? -1 : (k & KEY_A) ? 1 : 0;
			if (dir) {
				switch (items[m.sel]) {
				case M_RESUME: if (k & KEY_A) goto resume; break;
				case M_SAVE: if (k & KEY_A) { state_save(); goto resume; } break;
				case M_LOAD: if (k & KEY_A) { state_load(); goto resume; } break;
				case M_SLOT: state_slot = (state_slot + dir + 10) % 10; break;
				case M_SCREEN: config_set_widescreen(!config_widescreen()); break;
				case M_SMOOTH: g_cfg.smooth ^= 1; break;
				case M_FPS: g_cfg.show_fps ^= 1; break;
				case M_CORE: if (k & KEY_A) m.core_page = true; break;
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
				case M_RESET: if (k & KEY_A) { retro_reset(); goto resume; } break;
				case M_EXIT:
					if (k & KEY_A) {
						config_save();
						return false;
					}
					break;
				}
			}
		}

		ui_begin();
		video_present(true, menu_draw_top, menu_draw_bottom, &m);
	}
	if (!aptShouldClose())
		goto resume;
	return false;

resume:
	config_save();
	audio_pause(false);
	return true;
}

// ---------------------------------------------------------------- run loop

static void game_draw_top(void *ud)
{
	(void)ud;
	u32 white = C2D_Color32(255, 255, 255, 255);
	if (g_cfg.show_fps)
		ui_text(2, 0, 0.45f, white, "%.1f fps  emu %.1fms", fps_measured, emu_ms);
	if (osd_frames > 0) {
		osd_frames--;
		ui_rect(0, 222, 400, 18, C2D_Color32(0, 0, 0, 160));
		ui_text(4, 223, 0.45f, white, "%s", osd_msg);
	}
}

static void game_draw_bottom(void *ud)
{
	(void)ud;
	ui_text(70, 100, 0.6f, C2D_Color32(140, 140, 160, 255), "Tap screen for menu");
	if (!has_audio)
		ui_text(20, 180, 0.42f, C2D_Color32(255, 160, 120, 255),
			"No sound: dump DSP firmware (dspfirm.cdc)");
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

bool emu_run_game(const char *path)
{
	struct retro_game_info info = { .path = path };
	struct retro_system_av_info av;
	u64 fps_t0;
	int fps_frames = 0, present_count = 0, srm_counter = 0;

	set_game_name(path);
	log_printf("loading %s\n", path);

	retro_init();
	core_initialized = true;
	if (!retro_load_game(&info)) {
		log_printf("retro_load_game failed\n");
		retro_deinit();
		core_initialized = false;
		return false;
	}
	retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);

	retro_get_system_av_info(&av);
	fps_target = av.timing.fps;
	has_audio = audio_init(av.timing.sample_rate);
	srm_load();
	osd("%s", __ctr_svchax ? "Dynarec enabled" : "Interpreter mode (no Luma SVC access) - slow");

	fps_t0 = osGetTime();
	u64 t_run = 0, t_present = 0, t_run_max = 0;
	u64 next_deadline = svcGetSystemTick();
	while (aptMainLoop()) {
		// Pace by the clock, not by vsync: a heavy frame (Silent Hill
		// alternates ~4ms and ~17ms ones) can borrow time from the next light
		// one instead of always missing a refresh. The GPU still flips the
		// screen only at vblank, so there is no tearing.
		u64 frame_ticks = (u64)(SYSCLOCK_ARM11 / fps_target);
		u64 now_t = svcGetSystemTick();
		if (now_t < next_deadline)
			svcSleepThread((next_deadline - now_t) * 1000000000ULL / SYSCLOCK_ARM11);
		else if (now_t > next_deadline + 3 * frame_ticks)
			next_deadline = now_t;   // far behind (loading, menu): don't race to catch up
		next_deadline += frame_ticks;

		if (abuf_status_cb) {
			unsigned occ = has_audio ? audio_occupancy() : 50;
			abuf_status_cb(has_audio, occ, occ < 20);
		}
		u64 t0 = svcGetSystemTick();
		retro_run();
		u64 t1 = svcGetSystemTick();
		fps_frames++;
		t_run += t1 - t0;
		if (t1 - t0 > t_run_max)
			t_run_max = t1 - t0;

		// touch the bottom screen to open the menu
		if (input_keys_down() & KEY_TOUCH) {
			if (!menu_run())
				break;
			fps_t0 = osGetTime();
			fps_frames = present_count = 0;
			next_deadline = svcGetSystemTick();
		}

		ui_begin();
		t1 = svcGetSystemTick();
		video_present(true, game_draw_top, game_draw_bottom, NULL);
		t_present += svcGetSystemTick() - t1;

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
			log_printf("fps %.1f  emu %.2fms (max %.2f)  present %.2fms (wait %.2f) xfer %.2f\n",
				fps_measured, emu_ms, t_run_max / (float)CPU_TICKS_PER_MSEC, present_ms,
				video_take_wait_ticks() / (float)present_count / CPU_TICKS_PER_MSEC,
				video_take_xfer_ticks() / (float)present_count / CPU_TICKS_PER_MSEC);
			char abuf[160];
			audio_stats(abuf, sizeof(abuf));
			log_printf("  %s\n", abuf);
			t_run = t_present = t_run_max = 0;
			present_count = 0;
			fps_frames = 0;
			fps_t0 = now;
		}
	}

	srm_save(false);
	retro_unload_game();
	audio_exit();
	retro_deinit();
	core_initialized = false;
	return true;
}
