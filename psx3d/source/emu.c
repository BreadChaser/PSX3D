// Hosts the PCSX-ReARMed libretro core: implements the callbacks it needs
// and runs the per-frame loop plus the in-game menu.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "psx3d.h"
#include "settings_menu.h"
#include "statsview.h"
#include "devtools.h"
#include "clip.h"
#include <sys/stat.h>
#include "../../core/plugins/gpulib/psx3d_frameskip.h"
#include "../../core/plugins/gpu_pica/gpu_pica.h"

#define SRM_CHECK_FRAMES 180
#define DISPLAY_HZ 59.831        // 3DS LCD refresh rate   // look for memory card writes every ~3 seconds

static char game_name[PATH_LEN];   // save/state file name (disc 1's name)
static char game_title[PATH_LEN];  // what the menu shows
static char osd_msg[128];
static int  osd_frames;
static bool osd_dirty;
static double fps_target = 60.0;
static float fps_measured, emu_ms, present_ms;
static bool core_initialized;
static bool has_audio;
static retro_audio_buffer_status_callback_t abuf_status_cb;
static struct retro_disk_control_callback disk;   // multi-disc games (via .m3u)
static bool have_disk;
static bool clip_game_enabled;
static char clip_base[PATH_LEN];
extern char CdromId[];
bool options_clip_snapshot(char *out, size_t size);
static void clip_game_path(char *out, size_t size)
{
    char title[101]; unsigned n = 0;
    for (const unsigned char *p=(const unsigned char *)game_title; *p && n<100; p++)
        title[n++] = ((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='-') ? *p : '_';
    if (!n) title[n++]='_';
    title[n]=0;
    snprintf(out,size,PSX3D_DIR "/recordings/%s",title);
}
static bool clip_start(void);
static void osd(const char *fmt, ...);
bool emu_record_start(void)
{
    if (!clip_game_enabled || clip_active || clip_replay_session) return false;
    bool ok=clip_start();
    if (ok) osd("Recording test clip: %d s",clip_seconds);
    else osd("Clip failed: %s",clip_status.message);
    return ok;
}
static bool clip_start(void)
{
    char dir[160], path[180];
    static char settings[CLIP_META-256];
    for (int i=0;i<cheats_count();i++)
        if (cheats_on(i) || cheats_forced(i)) {
            snprintf(clip_status.message,sizeof(clip_status.message),"Disable cheats/native widescreen before recording");
            return false;
        }
    if (have_disk && disk.get_eject_state && disk.get_eject_state()) {
        snprintf(clip_status.message,sizeof(clip_status.message),"Insert the game disc before recording");
        return false;
    }
    clip_game_path(dir,sizeof(dir));
    mkdir(PSX3D_DIR "/recordings",0777); mkdir(dir,0777);
    unsigned id;
    for (id=1;id<=999999;id++) {
        struct stat st;
        snprintf(path,sizeof(path),"%s/%u.state",dir,id);
        if (!stat(path,&st)) continue;
        snprintf(path,sizeof(path),"%s/%u.inp",dir,id);
        if (stat(path,&st)) break;
    }
    if (id>999999 || !options_clip_snapshot(settings,sizeof(settings))) {
        snprintf(clip_status.message,sizeof(clip_status.message),"Clip folder full or too many core options");
        return false;
    }
    size_t used=strlen(settings);
    extern int psx3d_idle_skip, psx3d_idle_pointer, psx3d_idle_delay;
    snprintf(settings+used,sizeof(settings)-used,"idle=%d\nidleptr=%d\nidledelay=%d\nrenderer=%s\nconsole=%s\napp_version=" PSX3D_VERSION "\nhalf3d=%d\ngldbg=%d\ngpu_async=%d\n",
        psx3d_idle_skip,psx3d_idle_pointer,psx3d_idle_delay,renderer_is_gpu()?"gpu":"sw",g_new3ds?"new":"old",
        g_cfg.half3d,sw_gldbg,sw_gpu_async);
    snprintf(clip_base,sizeof(clip_base),"%s/%u",dir,id);
    rewind_stop(); input_fast_forward_off(); sw_ff=false;
    return clip_record(clip_base,(unsigned)(clip_seconds*fps_target+0.5),(unsigned)(fps_target*1000+0.5),CdromId,settings);
}

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
	input_touch_disc_log(buf);
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
	osd_dirty = true;
	log_printf("osd: %s\n", osd_msg);
}

static void clip_osd(const char *text)
{
    snprintf(osd_msg,sizeof(osd_msg),"%s",text);
    osd_frames=120; osd_dirty=true;
}
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id);
static __attribute__((noinline)) void clip_frame(void)
{
    clip_run();
    // Capture uses raw gameplay input; normal gameplay must retain the batch gate.
    retro_set_input_state(input_state_cb);
    if (clip_status.frame%60==0 || clip_status.done) {
        char message[128];
        if (clip_status.done) snprintf(message,sizeof(message),"%.127s",clip_status.message);
        else snprintf(message,sizeof(message),"%s: %u s left",clip_status.replay?"Replaying":"Recording",
            (unsigned)((clip_status.frames-clip_status.frame+fps_target-1)/fps_target));
        clip_osd(message);
    }
    // Completed capture releases all buffers but keeps its result in status.
    if (clip_status.done && !clip_status.replay) clip_close();
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

static bool audio_muted;
static u64 audio_ticks;
u64 emu_audio_ticks(void) { return audio_ticks; }
static emu_frontend_sample frontend_sample;
const emu_frontend_sample *emu_frontend_stats(void) { return &frontend_sample; }

static void audio_sample_cb(int16_t l, int16_t r)
{
	if (audio_muted || (sw_ff || (!dev_batch_active() && input_fast_forward()))) return;
	int16_t s[2] = { l, r };
	u64 start = psx3d_skip.timing ? svcGetSystemTick() : 0;
	audio_write(s, 1);
	if (psx3d_skip.timing) audio_ticks += svcGetSystemTick() - start;
}

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
	if (audio_muted || (sw_ff || (!dev_batch_active() && input_fast_forward())))
		return frames;
	u64 start = psx3d_skip.timing ? svcGetSystemTick() : 0;
	size_t written = audio_write(data, frames);
	if (psx3d_skip.timing) audio_ticks += svcGetSystemTick() - start;
	return written;
}

static void input_poll_cb(void) { input_poll(); }

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
	return dev_batch_active() ? 0 : input_state(port, device, index, id);
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

	if (!mem || !size || clip_replay_session)
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

	if (!buf)
		log_printf("state: cannot allocate %u KiB to save slot %d\n", (unsigned)(size / 1024), slot);
	if (!buf || !retro_serialize(buf, size)) {
		osd(buf ? "Save state failed" : "Not enough memory to save a state");
		free(buf);
		return false;
	}
	state_ext(ext, sizeof(ext), slot);
	make_path(path, sizeof(path), PSX3D_STATE_DIR, ext);
	if (write_file(path, buf, size)) {
		make_path(path, sizeof(path), PSX3D_STATE_DIR, "");
		free(buf); // the thumbnail needs heap too, after the state is on disk
		buf = NULL;
		if (!state_thumb_write(path, slot))
			log_printf("state: saved slot %d without a thumbnail\n", slot);
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
	if (!buf) {
		log_printf("state: cannot allocate %u KiB to load slot %d\n", (unsigned)(size / 1024), slot);
		osd("Not enough memory to load a state");
	} else if (read_file(path, buf, size) == 0)
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

bool emu_debug_state(int slot, bool save)
{
	if (!core_initialized || slot < 0 || slot > 9)
		return false;
	bool ok = save ? state_save_to(slot) : state_load_from(slot);
	if (ok)
		sw_state = state_slot = slot;
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

enum { M_RESUME, M_CLIP, M_SAVE, M_LOAD, M_DISC, M_CHEATS, M_SETTINGS, M_RESET, M_EXIT,
       M_DISPLAY, M_PERFORMANCE, M_CONTROL_GROUP, M_SOUND, M_GENERAL, M_ADVANCED,
       M_SCREEN, M_SMOOTH, M_3D, M_FPS,
       M_CONTROLS, M_CLICK_SIDE, M_AUDIO, M_SOUNDS, M_MUSIC, M_AUTOSAVE, M_REWIND,
       M_DEFAULTS, M_CORE, M_RECORD, M_TONE, M_COUNT };
static const int menu_sections[M_COUNT] = {
	[M_SCREEN] = SETTINGS_DISPLAY, [M_SMOOTH] = SETTINGS_DISPLAY, [M_3D] = SETTINGS_DISPLAY,
	[M_FPS] = SETTINGS_PERFORMANCE, [M_REWIND] = SETTINGS_GENERAL,
	[M_CONTROLS] = SETTINGS_CONTROLS, [M_CLICK_SIDE] = SETTINGS_CONTROLS,
	[M_AUDIO] = SETTINGS_SOUND, [M_SOUNDS] = SETTINGS_SOUND, [M_MUSIC] = SETTINGS_SOUND,
	[M_AUTOSAVE] = SETTINGS_GENERAL, [M_DEFAULTS] = SETTINGS_GENERAL,
	[M_CORE] = SETTINGS_ADVANCED, [M_RECORD] = SETTINGS_ADVANCED, [M_TONE] = SETTINGS_ADVANCED,
};

// Headers are inserted only when a section has at least one visible entry.
static int menu_items(int *items, bool settings)
{
	int n = 0;
	if (!settings) {
		for (int i = 0; i < M_DISPLAY; i++) {
			if (i == M_CLIP && (!clip_game_enabled || clip_active || clip_replay_session)) continue;
			if (i == M_DISC && !(have_disk && disk.get_num_images() > 1)) continue;
			items[n++] = i;
		}
		return n;
	}
	for (int section = SETTINGS_DISPLAY; section < SETTINGS_SECTIONS; section++) {
		bool added_header = false;
		for (int i = M_SCREEN; i < M_COUNT; i++) {
			if (menu_sections[i] != section) continue;
			if ((i == M_RECORD || i == M_TONE) && !g_cfg.debug) continue;
			if (i == M_REWIND && !game_capabilities().rewind) continue;
			if (i == M_3D && !g_new3ds) continue;
			if (i == M_CLICK_SIDE && (!g_new3ds || g_cfg.show_fps == 2 ||
			    (!touch_buttons_active() && g_cfg.show_fps != 1))) continue;
			if (!added_header) { items[n++] = M_DISPLAY + section - 1; added_header = true; }
			items[n++] = i;
		}
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
	else if (m && m->ctl_page)
		ui_help_panel(152, "Controls", NULL, input_controls_help());
	else if (m && m->settings && !m->ctl_page && !m->cheat_page && m->sel < m->nitems)
		ui_help_for(152, menu_names[m->items[m->sel]]);
}

static const char *const menu_names[M_COUNT] = {
	[M_RESUME] = "Resume", [M_CLIP] = "Record test clip", [M_SAVE] = "Save state", [M_LOAD] = "Load state", [M_DISC] = "Swap disc", [M_CHEATS] = "Cheats",
	[M_SETTINGS] = "Settings", [M_RESET] = "Reset game", [M_EXIT] = "Exit to library",
	[M_DISPLAY] = "DISPLAY", [M_PERFORMANCE] = "PERFORMANCE", [M_CONTROL_GROUP] = "CONTROLS",
	[M_SOUND] = "SOUND", [M_GENERAL] = "GENERAL", [M_ADVANCED] = "ADVANCED",
	[M_SCREEN] = "Screen mode", [M_3D] = "3D",
	[M_SMOOTH] = "Smoothing",
	[M_FPS] = "Show FPS", [M_CONTROLS] = "Controls...", [M_REWIND] = "Rewind",
	[M_CLICK_SIDE] = "Bottom-screen buttons",
	[M_AUTOSAVE] = "Continue where I left off", [M_CORE] = "Core options...", [M_RECORD] = "Debug: record audio",
	[M_AUDIO] = "Smooth audio when slow", [M_SOUNDS] = "Menu sounds", [M_MUSIC] = "Library music", [M_TONE] = "Debug: test tone", [M_DEFAULTS] = "Reset to defaults",
};

static bool menu_header(int i, void *ud)
{
	int item = ((menu_t *)ud)->items[i];
	return item >= M_DISPLAY && item <= M_ADVANCED;
}

static const char *menu_label(int i, void *ud)
{
	menu_t *m = ud;
	return menu_names[m->items[i]];
}

static const char *menu_value(int i, void *ud)
{
	menu_t *m = ud;
	int item = m->items[i];
	if (item >= M_DISPLAY && item <= M_ADVANCED) return NULL;
	if (item == M_CONTROLS || item == M_CORE || item == M_SETTINGS)
		return ">";
	return m->val[item];
}

static const char *core_label(int i, void *ud)
{
	(void)ud;
	return options_desc(i);
}

static const char *core_value(int i, void *ud)
{
	(void)ud;
	return options_value(i);
}

#define MENU_Y0 40

static int menu_row_at(menu_t *m, float py)
{
	int row = flist_row_at(&m->fl, MENU_Y0, MENU_ROW_H, py);
	if (py < MENU_Y0 || py >= MENU_Y0 + MENU_ROWS * MENU_ROW_H ||
	    row < 0 || row >= m->nitems || menu_header(row, m)) return -1;
	return row;
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

static void menu_draw_bottom(void *ud)
{
	menu_t *m = ud;
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, C2D_Color32(6, 9, 24, 255));
	u32 hint = C2D_Color32(110, 120, 160, 255);

	if (m->ctl_page) {
		ui_menu_header("CONTROLS", "this game");
		flist_draw(&m->ctl_fl, input_controls_count(), m->ctl_sel, 0, MENU_Y0, 320, MENU_ROW_H, CORE_ROWS,
			input_control_label, input_control_value, m, menu_accent_cols[m->ctl_sel & 3]);
		ui_text_ex(NULL, 160, 222, 0.42f, hint, C2D_AlignCenter, "Left/Right change    X reset    B back");
		return;
	}

	if (m->cheat_page) {
		char n[24];
		snprintf(n, sizeof(n), "%d", cheats_count());
		ui_menu_header("CHEATS", n);
		flist_draw(&m->cheat_fl, cheats_count(), m->cheat_sel, 0, MENU_Y0, 320, MENU_ROW_H, CORE_ROWS,
			cheat_label, cheat_value, m, menu_accent_cols[m->cheat_sel & 3]);
		if (!cheats_count())
			ui_text_ex(NULL, 160, 110, 0.5f, C2D_Color32(170, 185, 230, 255), C2D_AlignCenter,
				"No cheats for this game yet");
		ui_text_ex(NULL, 160, 222, 0.42f, hint, C2D_AlignCenter, "A on/off    X add code    B back");
		return;
	}

	if (m->core_page) {
		ui_menu_header("Core options", "all games");
		flist_draw(&m->core_fl, options_count(), m->core_sel, 0, MENU_Y0, 320, MENU_ROW_H, CORE_ROWS,
			core_label, core_value, m, menu_accent_cols[m->core_sel & 3]);
		ui_text_ex(NULL, 160, 222, 0.42f, hint, C2D_AlignCenter, "Left/Right change    B back");
		return;
	}

	static const char *const s3d_names[] = { "Off", "Screen depth", "Full 3D (test)" };
	static const char *const screen_names[] = { "4:3", "Stretch", "Widescreen" };
	memset(m->val, 0, sizeof(m->val));
	if (have_disk && disk.get_num_images)
		snprintf(m->val[M_DISC], 48, "disc %u of %u", menu_disc + 1, disk.get_num_images());
	snprintf(m->val[M_SCREEN], 48, "%s", screen_names[config_screen_mode()]);
	snprintf(m->val[M_3D], 48, "%s", s3d_names[g_cfg.stereo == 2 ? 1 : g_cfg.stereo]);
	snprintf(m->val[M_SMOOTH], 48, "%s", g_cfg.smooth ? "On" : "Off");
	snprintf(m->val[M_FPS], 48, "%s", g_cfg.show_fps == 2 ? "Detailed" : g_cfg.show_fps ? "FPS" : "Off");
	snprintf(m->val[M_AUDIO], 48, "%s", g_cfg.audio_stretch ? "On" : "Off");
	snprintf(m->val[M_SOUNDS], 48, "%s", g_cfg.sounds ? "On" : "Off");
	snprintf(m->val[M_MUSIC], 48, "%s", g_cfg.music ? "On" : "Off");
	snprintf(m->val[M_REWIND], 48, "%s",
		gamecfg_int("rewind", 0) ? (rewind_active() ? "On" : "On (unavailable)") : "Off");
	snprintf(m->val[M_CLICK_SIDE], 48, "%s", g_cfg.touch_click_left ? "Left side" : "Right side");
	snprintf(m->val[M_AUTOSAVE], 48, "%s", g_cfg.autosave ? "On" : "Off");
	int changed = !g_new3ds && g_cfg.touch_stick != -1;
	for (int i = 0; i < INPUT_REMAP_N; i++)
		changed += !input_map_is_default(i);
	for (int i = 0; i < INPUT_HOTKEY_N; i++) changed += !input_hotkey_is_default(i);
	snprintf(m->val[M_CONTROLS], 48, "%s", changed ? "custom" : "default");

	// Rebuilt by the input loop, preserving the selected entry across visibility changes.
	char clock[16];
	time_t now = time(NULL);
	strftime(clock, sizeof(clock), "%H:%M", gmtime(&now));
	ui_menu_header(m->settings ? "Settings" : "MENU", m->settings ? "in game" : clock);
	flist_draw_sections(&m->fl, m->nitems, m->sel, 0, MENU_Y0, 320, MENU_ROW_H, MENU_ROWS, menu_label, menu_value, m,
		menu_accent_cols[m->sel & 3], m->settings ? menu_header : NULL);
	ui_text_ex(NULL, 160, 222, 0.42f, hint, C2D_AlignCenter,
		m->settings ? "A select    Left/Right change    B back" : "A select    B resume");
}

static void menu_open_page(menu_t *m, bool settings)
{
	if (settings && !m->settings)
		m->main_sel = m->sel;
	m->settings = settings;
	m->nitems = menu_items(m->items, settings);
	m->sel = settings ? settings_find(m->items, m->nitems, M_SCREEN, M_DISPLAY, M_ADVANCED) : m->main_sel;
	m->fl.init = false;
}

static bool game_menu_open;

// returns false when the user wants to leave the game
static bool menu_run(int initial_screen)
{
	if (initial_screen == DEV_UI_RESUME) return true;
	game_menu_open = true;
	input_gameplay(false);
	bottom_backlight(true);
	static menu_t m;   // keep the core options scroll position between visits
	m.core_page = false;
	m.cheat_page = false;
	m.ctl_page = false;
	m.settings = false;
	if (have_disk && disk.get_image_index)
		menu_disc = disk.get_image_index();
	m.main_sel = 0;
	menu_open_page(&m, initial_screen == DEV_UI_SETTINGS);
	sfx_play(SFX_SELECT);

	audio_pause(true);
	srm_save(false);

	while (aptMainLoop()) {
		if (dev_tick(true)) {
			game_menu_open = false;
			return false;
		}
		int screen = dev_ui_take();
		if (screen == DEV_UI_RESUME) goto resume;
		if (screen == DEV_UI_SETTINGS || screen == DEV_UI_PAUSE)
			menu_open_page(&m, screen == DEV_UI_SETTINGS);
		if (initial_screen == DEV_UI_STATES || screen == DEV_UI_STATES) {
			initial_screen = DEV_UI_NONE;
			char prefix[PATH_LEN];
			make_path(prefix, sizeof(prefix), PSX3D_STATE_DIR, "");
			int slot = dev_ui_slot;
			states_run(false, prefix, game_title, &slot);
			m.fl.init = false;
			continue;
		}
		input_poll();
		u32 k = input_keys_down();

		// touch a row to select it, touch it again to use it
		if (!m.core_page && !m.cheat_page && !m.ctl_page && (k & KEY_TOUCH)) {
			touchPosition tp;
			hidTouchRead(&tp);
			int row = menu_row_at(&m, tp.py);
			if (row >= 0) {
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
			if (k & KEY_DOWN) { m.ctl_sel = (m.ctl_sel + 1) % input_controls_count(); sfx_play(SFX_MOVE); }
			if (k & KEY_UP) { m.ctl_sel = (m.ctl_sel - 1 + input_controls_count()) % input_controls_count(); sfx_play(SFX_MOVE); }
			if (k & (KEY_RIGHT | KEY_A)) { input_control_cycle(m.ctl_sel, 1); config_controls_save(); sfx_play(SFX_MOVE); }
			if (k & KEY_LEFT) { input_control_cycle(m.ctl_sel, -1); config_controls_save(); sfx_play(SFX_MOVE); }
			if ((k & KEY_X) && ui_confirm("RESET CONTROLS?", "Use global controls for this game?", true)) {
				config_controls_defaults(); sfx_play(SFX_SELECT);
			}
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
			if (m.core_sel >= n) m.core_sel = n ? n - 1 : 0;
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
			if (k & KEY_DOWN) { m.sel = settings_step(m.items, n, m.sel, 1, M_DISPLAY, M_ADVANCED); sfx_play(SFX_MOVE); }
			if (k & KEY_UP) { m.sel = settings_step(m.items, n, m.sel, -1, M_DISPLAY, M_ADVANCED); sfx_play(SFX_MOVE); }
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
				case M_CLIP: if ((k & KEY_A) && emu_record_start()) goto resume; break;
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
				case M_CLICK_SIDE:
					g_cfg.touch_click_left ^= 1;
					input_touch_reset();
					break;
				case M_REWIND: {
					if (!game_capabilities().rewind) break;
					bool on = !gamecfg_int("rewind", 0);
					gamecfg_set_int("rewind", on);
					if (on && !rewind_start())
						osd("Rewind is unavailable: not enough free memory");
					if (!on)
						rewind_stop();
				} break;
				case M_AUTOSAVE: g_cfg.autosave ^= 1; break;
				case M_AUDIO: g_cfg.audio_stretch = g_cfg.audio_stretch ? 0 : 50; break;
				case M_SOUNDS: g_cfg.sounds ^= 1; break;
				case M_MUSIC: g_cfg.music ^= 1; break;
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
					int modes = 2;
					g_cfg.stereo = (g_cfg.stereo + dir + modes) % modes;
					config_sync_stereo();
				} break;
				case M_SMOOTH: g_cfg.smooth ^= 1; break;
				case M_FPS: statsview_cycle(); break;
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
				case M_DEFAULTS:
					if ((k & KEY_A) && ui_confirm("RESET THIS GAME?", "Remove this game's settings and control overrides?", true)) {
						if (config_game_reset()) {
							rewind_stop(); sync_widescreen();
							osd("Game settings reset to global defaults");
						} else osd("Could not remove game settings");
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
			flist_update(&m.ctl_fl, m.ctl_sel, input_controls_count(), CORE_ROWS);
		else if (m.cheat_page)
			flist_update(&m.cheat_fl, m.cheat_sel, cheats_count(), CORE_ROWS);
		else if (m.core_page) {
			int n = options_count();
			if (m.core_sel >= n) m.core_sel = n ? n - 1 : 0;
			flist_update(&m.core_fl, m.core_sel, n, CORE_ROWS);
		} else {
			int selected = m.items[m.sel];
			m.nitems = menu_items(m.items, m.settings);
			m.sel = settings_find(m.items, m.nitems, selected, M_DISPLAY, M_ADVANCED);
			flist_update(&m.fl, m.sel, m.nitems, MENU_ROWS);
		}
		ui_begin();
		video_present_vsync(true, menu_draw_top, menu_draw_bottom, &m);
	}
	if (!aptShouldClose())
		goto resume;
	return false;

resume:
	game_menu_open = false;
	input_gameplay(true);
	sfx_play(SFX_BACK);
	config_save();
	game_frames = 0;   // dim again a moment after resuming
	input_touch_reset();
	audio_pause(false);
	return true;
}

// ---------------------------------------------------------------- run loop

// In game the top screen shows only the game. Everything else lives on
// the otherwise black bottom screen.
static void game_draw_bottom(void *ud)
{
	(void)ud;
	u32 white = C2D_Color32(230, 230, 240, 255);
	C2D_DrawRectSolid(0, 0, 0.05f, 320, 240, C2D_Color32(0, 0, 0, 255));   // in game it's always black

	if (g_cfg.show_fps == 2) {
		ui_rect(125, 184, 70, 30, C2D_Color32(28, 30, 42, 255));
		ui_text(142, 191, .45f, white, "MENU");
	} else if (touch_buttons_active() || g_cfg.show_fps == 1)
		touch_buttons_draw();
	statsview_draw();
	if (osd_frames > 0) {
		ui_text(4, 204, 0.45f, white, "%s", osd_msg);
	}
	if (!has_audio)
		ui_text(4, 186, 0.42f, C2D_Color32(255, 160, 120, 255),
			"No sound available");
}

// Without a touch panel or FPS counter the bottom backlight can turn off.
// Touch still works, so tapping it opens the menu.
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
	if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP) dev_interrupt();
	// performance counters (test tool) off whenever the system takes over
	if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP || hook == APTHOOK_ONEXIT)
		pmu_stop();
	else if ((hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP) && sw_pmu0 >= 0)
		pmu_start(sw_pmu0, sw_pmu1 >= 0 ? sw_pmu1 : 0);
	if (hook == APTHOOK_ONSUSPEND) {
		input_gameplay(false);
		bottom_backlight(true);
	} else if (hook == APTHOOK_ONRESTORE) {
		game_frames = 0;
		input_touch_reset();
		input_gameplay(!game_menu_open);
	}
}

static bool run_game(const char *path, const char *save_name, const char *title)
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
	clip_close();
	memset(&clip_status,0,sizeof(clip_status));
	clip_game_enabled=clip_enabled;
	if (clip_requested>=0 && sw_state>=0) { log_printf("clip: replay= and state= are mutually exclusive\n"); return false; }
	if (clip_requested>=0) {
		char dir[160]; clip_game_path(dir,sizeof(dir));
		snprintf(clip_base,sizeof(clip_base),"%s/%d",dir,clip_requested);
		if (!clip_open(clip_base,NULL)) { log_printf("clip: %s\n",clip_status.message); return false; }
	}
	game_frames = 0;
	memset(&psx3d_skip, 0, sizeof(psx3d_skip));
	psx3d_skip.class_timing = sw_pmuclass;
	psx3d_skip.timing = dev_enabled() || sw_stats || sw_prof || log_linked();
	audio_ticks = 0;
	memset(&frontend_sample, 0, sizeof(frontend_sample));
	have_disk = false;
	memset(&disk, 0, sizeof(disk));
	log_printf("loading %s\n", path);
	config_game_begin(game_name);   // before loading: the core reads the widescreen option at load
	log_printf("renderer: GPU\n");
	if (!video_game_begin()) {
		log_printf("renderer initialization failed\n");
		config_game_end();
		clip_close();
		return false;
	}
	config_sync_stereo();

	retro_init();
	core_initialized = true;
	if (!retro_load_game(&info)) {
		log_printf("retro_load_game failed\n");
		config_game_end();
		retro_deinit();
		core_initialized = false;
		video_game_end();
		clip_close();
		return false;
	}
	// DualShock: starts in digital mode, games switch it to analog themselves,
	// and analog-only games (Ape Escape) need it plugged in
	retro_set_controller_port_device(0, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1));

	retro_get_system_av_info(&av);
	fps_target = av.timing.fps;
	has_audio = audio_init(av.timing.sample_rate);
	srm_load();
	if (clip_replay_session) {
		cheats_unload();
		retro_cheat_reset();
		config_set_ws_native(false);
	} else {
		cheats_load(game_name);
		sync_widescreen();
	}
	psx3d_prof_on = sw_prof;

	{
		char prefix[PATH_LEN];
		make_path(prefix, sizeof(prefix), PSX3D_STATE_DIR, "");
		if (clip_requested<0 && !dev_enabled() && g_cfg.autosave && sw_state < 0 && states_resume_prompt(prefix) && state_load_from(STATE_AUTO))
			osd("Continuing where you left off");
	}
	if (clip_requested<0 && game_capabilities().rewind && gamecfg_int("rewind", 0))
		rewind_start();
	bool initial_state_ok = true;
	clip_set_live_input(input_poll_cb,input_state);
	if (clip_requested>=0) {
		char expected[64]; snprintf(expected,sizeof(expected),"serial=%s\n",CdromId);
		initial_state_ok=!strncmp(clip_metadata(),expected,strlen(expected)) && clip_restore();
		sw_ff=false; input_fast_forward_off();
		log_printf("clip: replay %d %s\n",clip_requested,initial_state_ok?"loaded":clip_status.message);
	}
	if (sw_state >= 0) {
		state_slot = sw_state;
		log_printf("test: slot %d %s\n", sw_state, (initial_state_ok = state_load_from(sw_state)) ? "loaded" : "failed");
	}
	osd("%s", __ctr_svchax ? "Dynarec enabled" : "Interpreter mode (no Luma SVC access) - slow");

	if (sw_pmu0 >= 0)
		pmu_start(sw_pmu0, sw_pmu1 >= 0 ? sw_pmu1 : 0);
	dev_game_started(game_title, initial_state_ok);
	statsview_begin(game_title);
	fps_t0 = osGetTime();
	game_menu_open = false;
	input_gameplay(true);
	if (dev_batch_active()) input_fast_forward_off();
	u64 t_run = 0, t_present = 0, t_run_max = 0;
	int acap_frames = 0;
	psx3d_skip_controller skip_control = {0};
	psx3d_skip_state skip_previous = psx3d_skip;
	u64 pace_ticks = 0, misc_ticks = 0;
	unsigned held = 0, presented = 0, emulated = 0, top_paints = 0, bottom_paints = 0;
	bool previous_special = false;
	int previous_speed_icon = 0;
	u64 next_deadline = svcGetSystemTick();
	u64 stats_deadline = next_deadline + SYSCLOCK_ARM11;
	aptHookCookie apt_cookie;
	aptHook(&apt_cookie, game_apt, NULL);
	while (initial_state_ok && aptMainLoop()) {
		if (dev_enabled() && dev_tick(true)) break;
		// A disc swap may revoke a capability. Free its history at a safe point.
		if (!game_capabilities().rewind && rewind_active()) rewind_stop();
		u64 work_start = psx3d_skip.timing ? svcGetSystemTick() : 0;
		u64 frame_pace = 0;
		// Pace by the clock, not by vsync: a heavy frame (Silent Hill
		// alternates ~4ms and ~17ms ones) can borrow time from the next light
		// one instead of always missing a refresh. The GPU still flips the
		// screen only at vblank, so there is no tearing.
		u64 frame_ticks = (u64)(SYSCLOCK_ARM11 / fps_target);
		u64 now_t = svcGetSystemTick();
		if (!game_frames) { // resume/suspend/menu: start a fresh timing estimate
			memset(&skip_control, 0, sizeof(skip_control));
			psx3d_skip.phase = psx3d_skip.streak = 0;
			next_deadline = now_t;
		}
		bool late = now_t > next_deadline + frame_ticks / 8;   // last frame overran its slot
		late_frames += late;
		if (previous_special != ((sw_ff || (!dev_batch_active() && input_fast_forward())) || input_rewind())) {
			next_deadline = now_t;
			memset(&skip_control, 0, sizeof(skip_control));
			psx3d_skip.phase = psx3d_skip.streak = 0;
		}
		previous_special = (sw_ff || (!dev_batch_active() && input_fast_forward())) || input_rewind();
		if (!(sw_ff || (!dev_batch_active() && input_fast_forward())) && now_t < next_deadline) {
			svcSleepThread((next_deadline - now_t) * 1000000000ULL / SYSCLOCK_ARM11);
			frame_pace = svcGetSystemTick() - now_t;
			pace_ticks += frame_pace;
		}
		else if (now_t > next_deadline + 3 * frame_ticks)
			next_deadline = now_t;   // far behind (loading, menu): don't race to catch up
		next_deadline += frame_ticks;

		const char *skip_type = sw_rtfs && renderer_is_gpu() && !previous_special
			? options_get("pcsx_rearmed_frameskip_type") : NULL;
		psx3d_skip.enabled = skip_type && !strcmp(skip_type, "auto");
		skip_control.max_level = sw_rtfscap == 5 ? 7 : 6;
		psx3d_skip.max_skips = sw_rtfscap == 5 ? 5 : 4;
		psx3d_skip.level = sw_rtfslevel >= 0 ? sw_rtfslevel : skip_control.level;
		psx3d_skip.numerator = sw_rtfsdense ? (skip_control.dense_numerator ? skip_control.dense_numerator : 60) : 0;
		psx3d_skip.denominator = sw_rtfsdense ? 60 : 0;
		if (!psx3d_skip.enabled) psx3d_skip.phase = psx3d_skip.streak = 0;
		if (abuf_status_cb) {
			unsigned occ = has_audio ? audio_occupancy() : 50;
			// auto frameskip: skip drawing when sound runs low or the frame
			// clock says we are behind (the sound buffer alone rarely shows it)
			abuf_status_cb(true, occ, occ < 20 || late);
		}
		u64 t0 = svcGetSystemTick();
		unsigned prims_before = psx3d_skip.render_prims, skipped_before = psx3d_skip.words_skipped;
		static bool rewind_hint;
		bool rewinding = !dev_batch_active() && input_rewind();
		bool ff = sw_ff || (!dev_batch_active() && input_fast_forward());
		speed_icon = rewinding && rewind_active() ? -1 : ff ? 1 : 0;
		if (rewinding && rewind_active()) {
			// step back about 6 times a second of game time, shown at 20/s
			static int rw;
			if (++rw >= 3) {
				rw = 0;
				if (!rewind_step())
					osd("Rewound as far as it goes");
			}
			audio_muted = true;
			retro_run();
			emulated++;
			statsview_frame();   // count the emulated vblank, including rewind/FF
			audio_muted = false;
		} else {
			if (rewinding && !rewind_hint) {
				osd("Enable Rewind in Settings first");
				rewind_hint = true;
			}
			if (!rewinding)
				rewind_hint = false;
			options_fast_forward(ff);
			// fast-forward: up to 3 more frames per shown frame, no sound
			for (int extra = ff ? 3 : 0; extra > 0; extra--) {
				audio_muted = true;
				pmu_begin();
				retro_run();
				pmu_end();
				emulated++;
				statsview_frame();
				rewind_capture();
			}
			audio_muted = ff;
			pmu_begin();
			if (clip_active) clip_frame(); else retro_run();
			pmu_end();
			emulated++;
			statsview_frame();
			audio_muted = false;
			rewind_capture();
			if (sw_acap && ++acap_frames == 60)   // same emulated frame every run
				audio_hash_start(44100 * 10);
			if (renderer_is_gpu() && sw_vramdump && --sw_vramdump == 0) {
				void pica_dump_vram(const char *path);
				pica_dump_vram(PSX3D_DIR "/vram.bin");
				log_printf("test: VRAM dumped\n");
			}
			if (ff)
				next_deadline = svcGetSystemTick();
		}
		pica_thread_sync();   // before menus, UI buffers, stereo and stats touch renderer/frontend state
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
		u64 t1 = svcGetSystemTick();
		fps_frames++;
		u64 run_ticks = t1 - t0;
		t_run += run_ticks;
		if (t1 - t0 > t_run_max)
			t_run_max = t1 - t0;

		// touch the bottom screen to open the menu
		if (!dev_batch_active() && statsview_touch()) {
			bottom_backlight(true);
		}
		if (!dev_batch_active() && (dev_ui_pending() || menu_touched())) {
			int screen = dev_ui_take();
			if (!menu_run(screen))
				break;
			fps_t0 = osGetTime();
			fps_frames = present_count = 0;
			t_run = t_present = t_run_max = 0;
			dup_frames = late_frames = 0;
			statsview_begin(game_title);
			next_deadline = svcGetSystemTick();
			stats_deadline = next_deadline + SYSCLOCK_ARM11;
			memset(&skip_control, 0, sizeof(skip_control));
			psx3d_skip.phase = psx3d_skip.streak = 0;
			held = presented = emulated = 0;
			pace_ticks = audio_ticks = misc_ticks = 0;
			top_paints = bottom_paints = 0;
			skip_previous = psx3d_skip;
			continue;
		}

		// Query once after worker sync. A held picture needs no top target,
		// transfer or C2D pass; a bottom-only change leaves top scanout alone.
		bool changed = video_display_changed() || !sw_frontend;
		static float last_slider;
		float slider = slider_get();
		bool repaint_top = changed || game_frames < 2 || speed_icon ||
			speed_icon != previous_speed_icon || slider != last_slider;
		previous_speed_icon = speed_icon;
		last_slider = slider;
		// Scan control layout on shown frames and touch press/drag/release.
		// Input itself is still polled by every retro_run, including FF.
		static bool was_touching;
		bool touching = (hidKeysHeld() & KEY_TOUCH) != 0;
		bool touch_changed = false;
		if (repaint_top || touching || was_touching)
			touch_changed = input_touch_changed();
		was_touching = touching;
		bool stats_changed = statsview_redraw(osd_frames);
		bool repaint = !sw_frontend || game_frames < 2 || stats_changed || touch_changed || osd_dirty;
		osd_dirty = false;
		if (repaint_top || repaint) ui_begin();
		t1 = svcGetSystemTick();
		if (repaint_top || repaint) {
			video_present_screens(true, repaint_top, speed_icon ? game_draw_top : NULL,
				repaint ? game_draw_bottom : NULL, NULL);
			presented++;
			top_paints += repaint_top;
			bottom_paints += repaint;
		} else { video_hold(); held++; }
		u64 end_work = svcGetSystemTick();
		u64 compose_ticks = end_work - t1;
		t_present += compose_ticks;

		++game_frames;
		if (osd_frames > 0) --osd_frames;
		if (repaint_top || repaint || game_frames == 120) {
			// Capability/layout changes invalidate the next shown frame.
			if (!game_capabilities().rewind && rewind_active()) rewind_stop();
			if (touch_buttons_active() || g_cfg.show_fps || osd_frames)
				bottom_backlight(true);
			else if (game_frames >= 120 && g_cfg.bottom_off)
				bottom_backlight(false);
		}

		if (++srm_counter >= SRM_CHECK_FRAMES) {
			srm_counter = 0;
			srm_save(false);
		}
		present_count++;
		if (psx3d_skip.timing)
			misc_ticks += svcGetSystemTick() - work_start - frame_pace - run_ticks - compose_ticks;
		u64 report_start = psx3d_skip.timing ? svcGetSystemTick() : 0;
		u64 now = end_work >= stats_deadline ? osGetTime() : fps_t0;
		if (now - fps_t0 >= 1000 || (clip_replay_session && clip_status.done)) {
			if (clip_replay_session && clip_status.done) { now=osGetTime(); if (now<=fps_t0) now=fps_t0+1; }
			fps_measured = fps_frames * 1000.0f / (now - fps_t0);
			double per_frame_ms = 1.0 / (emulated ? emulated : 1) / CPU_TICKS_PER_MSEC;
			frontend_sample = (emu_frontend_sample){
				.emulated = emulated, .elapsed_ms = now - fps_t0,
				.presented = presented, .held = held, .top = top_paints, .bottom = bottom_paints,
				.run_ms = t_run * per_frame_ms, .present_ms = t_present * per_frame_ms,
				.audio_ms = audio_ticks * per_frame_ms, .misc_ms = misc_ticks * per_frame_ms,
				.pace_ms = pace_ticks * per_frame_ms,
				.pica_ms = (unsigned)(psx3d_skip.gpu_main_ticks - skip_previous.gpu_main_ticks) * per_frame_ms,
				.spu_ms = (unsigned)(psx3d_skip.spu_main_ticks - skip_previous.spu_main_ticks) * per_frame_ms,
				.spu_wait_ms = (unsigned)(psx3d_skip.spu_wait_ticks - skip_previous.spu_wait_ticks) * per_frame_ms,
				.draw_fraction = !psx3d_skip.enabled ? 1 : psx3d_skip.denominator ? (float)psx3d_skip.numerator / psx3d_skip.denominator : (float)psx3d_skip_draw(psx3d_skip.level) / psx3d_skip_period(psx3d_skip.level),
				.skip_level = psx3d_skip.level, .drawn = psx3d_skip.flips_drawn - skip_previous.flips_drawn,
				.skipped = psx3d_skip.flips_skipped - skip_previous.flips_skipped };
			for (int k=0; k<8; k++) frontend_sample.phase_ms[k] = (unsigned)(psx3d_skip.phase_ticks[k] - skip_previous.phase_ticks[k]) * per_frame_ms;
			if (log_linked() || sw_prof || sw_stats)
				log_printf("  frontend emu %u elapsed %ums top %u bottom %u present %.3fms audio %.3fms misc %.3fms pace %.3fms\n",
					frontend_sample.emulated, frontend_sample.elapsed_ms, top_paints, bottom_paints,
					frontend_sample.present_ms, frontend_sample.audio_ms, frontend_sample.misc_ms, frontend_sample.pace_ms);
			emu_ms = t_run / (float)present_count / CPU_TICKS_PER_MSEC;
			present_ms = t_present / (float)present_count / CPU_TICKS_PER_MSEC;
			if (statsview_collecting()) {
				statsview_sample sample = { .fps = fps_measured, .target = fps_target,
				 .emu = emu_ms, .max = t_run_max / (float)CPU_TICKS_PER_MSEC, .present = present_ms,
				 .skipped = dup_frames, .late = late_frames, .elapsed = now - fps_t0 };
				// SD writes cost a ~40ms hitch on Old 3DS: per-second stats
				// only when sent to a PC or profiling
				float wait_ms = video_take_wait_ticks() / (float)present_count / CPU_TICKS_PER_MSEC;
				sample.wait = wait_ms;
				float xfer_ms = video_take_xfer_ticks() / (float)present_count / CPU_TICKS_PER_MSEC;
				sample.xfer = xfer_ms;
				if (log_linked() || sw_prof || sw_stats)
					log_printf("fps %.1f  emu %.2fms (max %.2f)  present %.2fms (wait %.2f) xfer %.2f  dupe %u late %u cb %d\n",
						fps_measured, emu_ms, t_run_max / (float)CPU_TICKS_PER_MSEC, present_ms,
						wait_ms, xfer_ms, dup_frames, late_frames, abuf_status_cb != NULL);
				if (log_linked() || sw_prof || sw_stats) {
					double frame_ms = 1.0 / (emulated ? emulated : 1) / CPU_TICKS_PER_MSEC;
					log_printf("  cadence ff %d emu %u game %.1f%% drawn %u skipped %u drawfps %.1f present %u held %u level %u avg %.2fms run %.2fms compose %.2fms pace %.2fms audio %.2fms spu %.2fms (sync %.2f) worker %.2fms/%u steal %u words %u/%u\n",
						speed_icon > 0, emulated, emulated * 100000.0 / (now - fps_t0) / fps_target,
						psx3d_skip.flips_drawn - skip_previous.flips_drawn,
						psx3d_skip.flips_skipped - skip_previous.flips_skipped,
						(psx3d_skip.pictures - skip_previous.pictures) * 1000.0 / (now - fps_t0),
						presented, held, skip_control.level, skip_control.average,
						t_run * frame_ms, t_present * frame_ms,
						pace_ticks * frame_ms, audio_ticks * frame_ms,
						(unsigned)(psx3d_skip.spu_main_ticks - skip_previous.spu_main_ticks) * frame_ms,
						(unsigned)(psx3d_skip.spu_wait_ticks - skip_previous.spu_wait_ticks) * frame_ms,
						__atomic_exchange_n(&psx3d_spu_worker_ticks, 0, __ATOMIC_RELAXED) * frame_ms,
						__atomic_exchange_n(&psx3d_spu_worker_items, 0, __ATOMIC_RELAXED),
						psx3d_skip.spu_stolen - skip_previous.spu_stolen,
						psx3d_skip.words_drawn - skip_previous.words_drawn,
						psx3d_skip.words_skipped - skip_previous.words_skipped);
				}
				dup_frames = late_frames = 0;
				{
					float gms; u64 gap;
					video_take_gpu_stats(&gms, &gap);
					sample.gpu = gms / present_count;
					u64 qw = video_take_pending_wait_ticks();
					float heap_ms = video_take_heap_flush_ticks() / (float)present_count / CPU_TICKS_PER_MSEC;
					sample.heap = heap_ms;
					sample.queue = qw / (float)present_count / CPU_TICKS_PER_MSEC;
					sample.gap = gap / (float)present_count / CPU_TICKS_PER_MSEC;
					if (log_linked() || sw_prof || sw_stats)
						log_printf("  heap flush %.3fms, queue wait %.3fms\n", heap_ms, qw / (float)present_count / CPU_TICKS_PER_MSEC);
					if (log_linked() || sw_prof || sw_stats)
						log_printf("  gpu %.2fms/frame, frame end to next begin %.2fms\n", gms / present_count,
							gap / (float)present_count / CPU_TICKS_PER_MSEC);
				}
				char abuf[256], renderer[1024] = "", pb[512] = "";
				double cycles, ev0, ev1;
				pmu_sample(&cycles, &ev0, &ev1);
				audio_stats(abuf, sizeof(abuf));
				sample.audio = abuf; sample.renderer = renderer;
				if (log_linked() || sw_prof || sw_stats)
					log_printf("  %s\n", abuf);
				if (renderer_is_gpu()) {
					void pica_stats(char *buf, int n);
					pica_stats(renderer, sizeof(renderer));
					if (log_linked() || sw_prof || sw_stats)
						log_printf("  %s\n", renderer);
					void pica_probe(char *buf, int n);
					if (dev_enabled() || log_linked() || sw_prof || sw_stats) {
						pica_probe(pb, sizeof(pb));   // what reached the screen
						if (log_linked() || sw_prof || sw_stats) log_printf("  %s; top fb %d\n", pb, video_probe_top());
					}
				}
				if (dev_enabled()) dev_sample(&sample, pb, video_probe_top(), cycles, ev0, ev1);
				pmu_report();
				statsview_update(&sample);
			}
			dup_frames = late_frames = 0;
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
				{
					// the emulator's timed events, by type (PSXINT_*), over 0.05ms
					extern unsigned long long psx3d_irq_t[32];
					extern unsigned int psx3d_irq_c[32];
					n = 0;
					for (int k = 0; k < 32; k++) {
						double ms = psx3d_irq_t[k] / (double)(fps_frames ? fps_frames : 1) / CPU_TICKS_PER_MSEC;
						if (ms >= 0.05)
							n += snprintf(pb + n, sizeof(pb) - n, " %d:%.2fms/%u", k, ms,
								psx3d_irq_c[k] / (fps_frames ? fps_frames : 1));
						psx3d_irq_t[k] = 0; psx3d_irq_c[k] = 0;
					}
					log_printf("  events (per frame):%s\n", pb);
				}
				new_dynarec_print_stats();
			}
			t_run = t_present = t_run_max = 0;
			skip_previous = psx3d_skip;
			pace_ticks = audio_ticks = misc_ticks = 0;
			top_paints = bottom_paints = 0;
			held = presented = emulated = 0;
			present_count = 0;
			fps_frames = 0;
			fps_t0 = now;
			stats_deadline = end_work + SYSCLOCK_ARM11;
		}
		if (psx3d_skip.timing) misc_ticks += svcGetSystemTick() - report_start;
		if (psx3d_skip.enabled && sw_rtfsdense)
			psx3d_skip_observe_dense(&skip_control, (svcGetSystemTick() - t0) / (double)CPU_TICKS_PER_MSEC,
				1000.0 / fps_target, psx3d_skip.render_prims != prims_before ? 0 :
				psx3d_skip.words_skipped != skipped_before ? 1 : 2);
		else if (psx3d_skip.enabled && sw_rtfsmodel)
			psx3d_skip_observe_model(&skip_control, (svcGetSystemTick() - t0) / (double)CPU_TICKS_PER_MSEC,
				1000.0 / fps_target, psx3d_skip.render_prims != prims_before ? 0 :
				psx3d_skip.words_skipped != skipped_before ? 1 : 2);
		else if (psx3d_skip.enabled)
			(sw_rtfswindow ? psx3d_skip_observe_window : psx3d_skip_observe)(&skip_control, (svcGetSystemTick() - t0) / (double)CPU_TICKS_PER_MSEC,
				1000.0 / fps_target);
		if (clip_replay_session && clip_status.done) break;
	}

	psx3d_skip.enabled = 0;
	aptUnhook(&apt_cookie);
	input_fast_forward_off();
	input_gameplay(false);
	options_fast_forward(false);
	pmu_stop();
	log_printf("game: leaving\n");
	rewind_stop();
	if (!clip_replay_session && !dev_enabled() && g_cfg.autosave)
		log_printf("auto-save: %s\n", state_save_to(STATE_AUTO) ? "written" : "failed");
	bottom_backlight(true);
	srm_save(false);
	cheats_unload();
	retro_unload_game();
	config_game_end();
	audio_exit();
	retro_deinit();
	core_initialized = false;
	video_game_end();
	clip_close();
	return true;
}

bool emu_run_game(const char *path, const char *save_name, const char *title)
{
	covers_stop_wait();
	bool ok = run_game(path, save_name, title);
	dev_game_ended(ok);
	return ok;
}
