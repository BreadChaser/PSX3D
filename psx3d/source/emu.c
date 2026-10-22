// Hosts the PCSX-ReARMed libretro core: implements the callbacks it needs
// and runs the per-frame loop plus the in-game menu.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "psx3d.h"
#include "statsview.h"
#include "devtools.h"
#include "clip.h"
#include "safefile.h"
#include "statefile.h"
#include "gpustereo.h"
#include <sys/stat.h>
#include <strings.h>
#include <unistd.h>
#include "../../core/plugins/gpulib/psx3d_frameskip.h"
#include "../../core/plugins/gpu_pica/gpu_pica.h"

#define SRM_CHECK_FRAMES 180
#define DISPLAY_HZ 59.831        // 3DS LCD refresh rate   // look for memory card writes every ~3 seconds
#define PACE_DEBT 3ULL            // frames of lateness the pacer keeps when far behind

static char game_name[PATH_LEN];   // save/state file name (disc 1's name)
static char game_title[PATH_LEN];  // what the menu shows
const char *emu_game_title(void) { return game_title; }
// Messages: a queue, so one never wipes out another in the same moment
// (a damaged save, then "Loaded slot 2"). Alerts about the player's data
// (emu_osd_alert) are shown longer and also on the top screen, where the
// player is looking, above everything else.
#define OSD_QUEUE 6
#define OSD_FRAMES 120
#define OSD_ALERT_FRAMES 330
static struct { char text[128]; bool alert, clip; } osd_q[OSD_QUEUE];
static int  osd_n;
static int  osd_frames;
static bool osd_dirty;
static double fps_target = 60.0;
static float fps_measured, emu_ms, present_ms;
static bool core_initialized;
static bool has_audio;
static retro_audio_buffer_status_callback_t abuf_status_cb;
static bool core_auto_frameskip;
static double auto_floor_scale = 1;
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
    snprintf(settings+used,sizeof(settings)-used,"idle=%d\nidleptr=%d\nidledelay=%d\nrenderer=%s\nconsole=%s\napp_version=" PSX3D_VERSION "\ngldbg=%d\ngpu_async=%d\n",
        psx3d_idle_skip,psx3d_idle_pointer,psx3d_idle_delay,"gpu",g_new3ds?"new":"old",
        sw_gldbg,sw_gpu_async);
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

PSX3D_COLD static void osd_push(const char *text, bool alert)
{
	if (osd_n && !strcmp(osd_q[osd_n - 1].text, text))
		return;   // the same message again: once is enough
	if (osd_n == OSD_QUEUE) {   // full: the oldest plain message makes room
		int drop = 0;
		while (drop < osd_n - 1 && osd_q[drop].alert)
			drop++;
		memmove(&osd_q[drop], &osd_q[drop + 1], (osd_n - drop - 1) * sizeof(osd_q[0]));
		osd_n--;
		if (!drop)
			osd_frames = 0;
	}
	snprintf(osd_q[osd_n].text, sizeof(osd_q[0].text), "%s", text);
	osd_q[osd_n].alert = alert;
	osd_q[osd_n].clip = false;
	if (!osd_n++)
		osd_frames = alert ? OSD_ALERT_FRAMES : OSD_FRAMES;
	osd_dirty = true;
	log_printf("osd%s: %s\n", alert ? " (alert)" : "", text);
}

// a frame passed: the shown message counts down, then the next one
static void osd_tick(void)
{
	if (!osd_n || --osd_frames > 0)
		return;
	memmove(&osd_q[0], &osd_q[1], (osd_n - 1) * sizeof(osd_q[0]));
	if (--osd_n)
		osd_frames = osd_q[0].alert ? OSD_ALERT_FRAMES : OSD_FRAMES;
	osd_dirty = true;
}

static void osd(const char *fmt, ...)
{
	char text[128];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);
	osd_push(text, false);
}

void emu_osd(const char *text)
{
	osd_push(text, false);
}

PSX3D_COLD void emu_osd_alert(const char *text)
{
	osd_push(text, true);
}

// the clip countdown replaces its own previous line instead of queueing
PSX3D_COLD static void clip_osd(const char *text)
{
    if (osd_n && osd_q[osd_n-1].clip) {
        snprintf(osd_q[osd_n-1].text,sizeof(osd_q[0].text),"%s",text);
        if (osd_n==1) osd_frames=OSD_FRAMES;
        osd_dirty=true;
        return;
    }
    osd_push(text,false);
    if (osd_n) osd_q[osd_n-1].clip=true;
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
		if (!strcmp(v->key, "pcsx_rearmed_frameskip_type")) {
			// Normalize credit to the core's fixed 15-picture unit. Scaling
			// wall time by N/15 gives exactly the N-picture threshold/cap,
			// without changing core text, state layout, debt or skip decisions.
			auto_floor_scale = game_auto_floor() / 15.0;
			core_auto_frameskip = v->value && !strcmp(v->value, "auto");
		}
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

static bool osd_alert_up(void) { return osd_n && osd_q[0].alert; }

// An alert about the player's data: a band across the top of the game
PSX3D_COLD static void alert_draw_top(void)
{
	float dx = ui_depth(-0.7f);
	C2D_DrawRectSolid(0, 0, 0.9f, 400, 30, C2D_Color32(20, 8, 4, 225));
	C2D_DrawRectSolid(0, 30, 0.9f, 400, 2, C2D_Color32(255, 170, 60, 255));
	ui_text_ex(NULL, 200 + dx, 8, 0.45f, C2D_Color32(255, 225, 170, 255), C2D_AlignCenter,
		ui_fit(osd_q[0].text, 0.45f, 390));
}

// ▶▶ or ◀◀ in the top right corner while fast-forward or rewind is held
static void game_draw_top(void *ud)
{
	(void)ud;
	if (osd_alert_up())
		alert_draw_top();
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
/* Bounded truncation is intentional; preserve the validated release code layout. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
	snprintf(out, len, "%s/%s%s", dir, game_name, ext);
#pragma GCC diagnostic pop
}

// What a good state file is (safefile.h): its exact size and the header the
// core looks at first. That is all a "good" backup means.
static bool state_head_ok(const void *head)
{
	const char *h = head;
	if (!strncmp(h, "RASTATE", 7))
		h += 16;
	return !strncmp(h, "STv4 PCSX", 9);
}

safe_format_t emu_state_format(void)
{
	return (safe_format_t){ retro_serialize_size(), 32, state_head_ok };
}

// ---------------------------------------------------------------- disc identity

// The discs of this launch by file name (a library playlist lists them all;
// a disc started from the browser is the only one): a state records its
// disc by name, so it finds that disc again however the game was started.
#define DISC_NAMES 8
static char disc_names[DISC_NAMES][128];
static unsigned disc_names_n;

PSX3D_COLD static const char *base_name(const char *path)
{
	const char *b = strrchr(path, '/');
	return b ? b + 1 : path;
}

PSX3D_COLD static void disc_names_init(const char *path)
{
	disc_names_n = 0;
	size_t len = strlen(path);
	if (len < 4 || strcasecmp(path + len - 4, ".m3u")) {
		snprintf(disc_names[disc_names_n++], sizeof(disc_names[0]), "%s", base_name(path));
		return;
	}
	FILE *f = fopen(path, "r");
	char line[PATH_LEN];
	while (f && disc_names_n < DISC_NAMES && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!line[0] || line[0] == '#')
			continue;
		const char *b = base_name(line), *bs = strrchr(b, '\\');
		if (snprintf(disc_names[disc_names_n], sizeof(disc_names[0]), "%s", bs ? bs + 1 : b) < (int)sizeof(disc_names[0]))
			disc_names_n++;
		else
			disc_names[disc_names_n][0] = 0, disc_names_n++;   // too long to match: never mapped
	}
	if (f)
		fclose(f);
}

PSX3D_COLD static unsigned disc_count(void)
{
	return have_disk && disk.get_num_images ? disk.get_num_images() : 1;
}

PSX3D_COLD static void disc_query(char *name, size_t len, unsigned *index, unsigned *count)
{
	unsigned i = have_disk && disk.get_image_index ? disk.get_image_index() : 0;
	*index = i;
	*count = disc_count();
	snprintf(name, len, "%s", i < disc_names_n && disc_names_n == *count ? disc_names[i] : "");
}

// -1: the state's disc is not in this launch; -2: can't tell (use its index)
PSX3D_COLD static int disc_map(const char *name, unsigned index, unsigned count)
{
	(void)index; (void)count;
	if (!disc_names_n || disc_names_n != disc_count())
		return -2;
	for (unsigned i = 0; i < disc_names_n; i++)
		if (!strcasecmp(disc_names[i], name))
			return (int)i;
	return -1;
}

// ---------------------------------------------------------------- states

static void sta_retire(const char *bak, void *ctx);

// States: written crash-safe, the previous version kept as .bak, with the
// checksum sealed into the trailer first (statefile.h). The Continue
// point's older versions go into the save backups (history.h).
PSX3D_COLD static bool write_file(const char *path, void *data, size_t size, bool continue_point)
{
	safe_format_t fmt = emu_state_format();
	if (continue_point)
		fmt.retire = sta_retire;
	state_seal(data, size);
	return fmt.size == size && safe_write(path, &fmt, data, true);
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

// The Continue point could not be kept when the player chose Start game:
// it is never written over this session (the player's old spot wins).
static bool continue_frozen;

static bool state_save_to(int slot)
{
	char path[PATH_LEN], ext[16];
	size_t size = retro_serialize_size();
	void *buf = size ? malloc(size) : NULL;
	bool ok = false;

	if (slot == STATE_AUTO && continue_frozen)
		return false;
	if (!buf)
		log_printf("state: cannot allocate %u KiB to save slot %d\n", (unsigned)(size / 1024), slot);
	state_reason = slot == STATE_AUTO ? STATE_WHY_EXIT : STATE_WHY_MANUAL;
	if (!buf || !retro_serialize(buf, size)) {
		state_reason = STATE_WHY_MANUAL;
		osd(buf ? "Save state failed" : "Not enough memory to save a state");
		free(buf);
		return false;
	}
	state_reason = STATE_WHY_MANUAL;
	state_ext(ext, sizeof(ext), slot);
	make_path(path, sizeof(path), PSX3D_STATE_DIR, ext);
	if (write_file(path, buf, size, slot == STATE_AUTO)) {
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

#define NO_SLOT (-99)   // a backup file: no slot thumbnail to repair

// Load the state file path (or its backup, or an unfinished copy); slot:
// whose thumbnail to bring back with a repaired backup (NO_SLOT: none)
PSX3D_COLD static bool state_load_file(const char *path, int slot)
{
	safe_format_t fmt = emu_state_format();
	safe_info_t info;
	unsigned skip = 0;
	bool ok = false, found = false, version = false, changed = false, no_memory = false, other_disc = false;

	// the newest good copy the core accepts; one buffer at a time (Old 3DS)
	for (;;) {
		void *buf = safe_load(path, &fmt, skip, &info);
		found |= info.rejected;
		no_memory |= info.failed;
		if (!buf)
			break;
		found = true;
		skip |= (2u << info.from) - 1;   // this copy and the ones before it
		ok = retro_unserialize(buf, fmt.size);
		if (!ok) {
			log_printf("state: %s (%s) not loaded (%d)\n", path, info.from == SAFE_CURRENT ? "file" :
				info.from == SAFE_BACKUP ? "backup" : "unfinished copy", state_last_result);
			version |= state_last_result == STATE_VERSION;
			changed |= state_last_result == STATE_PARTLY;
			other_disc |= state_last_result == STATE_OTHER_DISC;
			free(buf);
			if (changed || other_disc)
				break;   // the backup is from the same disc, and the game must restart now
			continue;
		}
		if (info.from != SAFE_CURRENT) {
			log_printf("state: %s damaged, loaded the %s\n", path, info.from == SAFE_BACKUP ? "backup" : "unfinished copy");
			bool fixed = safe_repair(path, &fmt, buf, info.from);
			if (!fixed)
				log_printf("state: could not repair %s\n", path);
			if (slot != NO_SLOT) {
				char prefix[PATH_LEN];
				make_path(prefix, sizeof(prefix), PSX3D_STATE_DIR, "");
				state_thumb_restore(prefix, slot);
			}
			emu_osd_alert(fixed ? "That save was damaged: loaded its backup and repaired the file" :
				"That save was damaged: loaded its backup, could not fix the file");
		}
		free(buf);
		break;
	}
	if (ok)
		rewind_reset();
	else if (changed) {
		// never play on in a half-restored game (it could save its card)
		log_printf("state: game was partly restored, restarting it\n");
		retro_reset();
		rewind_reset();
		emu_osd_alert("That save is damaged: the game was restarted");
	} else if (other_disc)
		emu_osd_alert("That save is for a disc that is not part of this game here");
	else if (no_memory)
		osd("Not enough memory to load a state");
	else if (!found)
		osd(slot >= 0 ? "Slot %d is empty" : "That save is gone", slot);
	else if (version)
		osd("That save is from an incompatible version");
	else
		emu_osd_alert("That save is damaged and has no good backup");
	return ok;
}

static bool state_load_from(int slot)
{
	char path[PATH_LEN], ext[16];
	state_ext(ext, sizeof(ext), slot);
	make_path(path, sizeof(path), PSX3D_STATE_DIR, ext);
	return state_load_file(path, slot);
}

// ---------------------------------------------------------------- save backups

// Owner's design (devtools/ui-mock/SAVE-BACKUP-DESIGN.md, "Recommended"):
// every risky action first puts the game (or the slot) it would destroy into
// the ring of automatic saves (history.h), and the pause menu offers one
// Undo / Redo of the latest one.
static void autosave_wait(void);
static bool save_take(const char *who, void **state, size_t *state_size, u16 **thumb, u64 *thumb_ms);

// A state file and how to find it again: the 3DS can't read file times, so
// a state is known by its trailer's time and id (0 for states made before)
typedef struct { hist_entry_t e; unsigned id; } save_ref_t;

PSX3D_COLD static unsigned file_id(const char *path, unsigned *saved)
{
	unsigned reason, when, id;
	state_file_info(path, &reason, &when, &id);
	if (saved)
		*saved = when;
	return id;
}

PSX3D_COLD static bool ref_of(const char *path, hist_kind_t kind, save_ref_t *r)
{
	struct stat st;
	unsigned saved;
	memset(r, 0, sizeof(*r));
	if (stat(path, &st) || snprintf(r->e.path, sizeof(r->e.path), "%s", path) >= (int)sizeof(r->e.path))
		return false;   // a name too long for an Undo target: no Undo
	r->e.kind = kind;
	r->e.size = (long)st.st_size;
	r->id = file_id(path, &saved);
	r->e.when = saved;
	return true;
}

// a ring entry as a reference (moved there just now)
PSX3D_COLD static save_ref_t ref_entry(const hist_entry_t *e)
{
	save_ref_t r = { *e, file_id(e->path, NULL) };
	return r;
}

// the time a state file was saved (0: unknown, the entry gets "now")
PSX3D_COLD static time_t saved_at(const char *path)
{
	unsigned saved = 0;
	file_id(path, &saved);
	return saved;
}

// The Continue point's version falling off its backup joins the ring
// ("timer" or "exit" from its trailer), with its thumbnail.
PSX3D_COLD static void sta_retire(const char *bak, void *ctx)
{
	(void)ctx;
	char thm[PATH_LEN + 16];
	size_t len = strlen(bak);
	if (len < 4 || continue_frozen)
		return;
	snprintf(thm, sizeof(thm), "%.*s.thm.bak", (int)(len - 4), bak);
	unsigned reason, saved, id;
	state_file_info(bak, &reason, &saved, &id);
	history_adopt(game_name, reason == STATE_WHY_TIMER ? HIST_TIMER : HIST_EXIT, ".sta", bak, thm, saved, NULL);
}

// The game as it is now, into the ring before a risky action. On Old 3DS a
// Continue auto-save up to REUSE_SECONDS old is used instead of waiting
// 2-3 s for a fresh one (owner's choice), unless it is not_this.
#define REUSE_SECONDS (5 * 60)
PSX3D_COLD static bool safety_copy(hist_kind_t kind, const save_ref_t *not_this, save_ref_t *out)
{
	char path[PATH_LEN];
	if (clip_replay_session || dev_remote_game())
		return false;
	autosave_wait();
	if (!g_new3ds) {
		save_ref_t sta;
		time_t now = time(NULL);
		make_path(path, sizeof(path), PSX3D_STATE_DIR, ".sta");
		if (ref_of(path, HIST_TIMER, &sta) && sta.id && sta.e.when && now >= sta.e.when &&
		    now - sta.e.when <= REUSE_SECONDS && !(not_this && not_this->id == sta.id)) {
			log_printf("backups: %s uses the Continue auto-save from %ld s ago\n", history_kind_name(kind),
				(long)(now - sta.e.when));
			*out = sta;
			return true;
		}
	}
	void *state;
	size_t size;
	u16 *thumb;
	u64 t0 = osGetTime();
	if (!save_take("safety copy", &state, &size, &thumb, NULL))
		return false;
	bool ok = history_next(game_name, kind, ".sta", 0, path, sizeof(out->e.path)) && write_file(path, state, size, false);
	free(state);
	if (ok && thumb) {
		char thm[PATH_LEN + 8];
		snprintf(thm, sizeof(thm), "%s.thm", path);
		state_thumb_save_file(thm, thumb);
	}
	free(thumb);
	if (ok) {
		history_added(game_name, path);
		ok = ref_of(path, kind, out);
	}
	log_printf("backups: safety copy (%s) %s in %llu ms\n", history_kind_name(kind), ok ? "written" : "FAILED",
		osGetTime() - t0);
	return ok;
}

typedef enum { UNDO_NONE, UNDO_LOAD, UNDO_SAVE, UNDO_RESET, UNDO_START, UNDO_DELETE, UNDO_RESTORE } undo_what_t;
static const char *const undo_names[] = { "", "load", "save", "reset", "start game", "delete", "restore" };
static struct {
	undo_what_t what;
	bool redo;             // the row offers "Redo ..."
	int slot;              // save, delete: the slot
	save_ref_t target;     // what the row brings back
	u64 at;                // when the action happened
} undo;

PSX3D_COLD static void undo_set(undo_what_t what, int slot, const save_ref_t *target)
{
	undo.what = what;
	undo.redo = false;
	undo.slot = slot;
	undo.target = *target;
	undo.at = osGetTime();
}

// where the target is now: the Continue point may have moved on since
// (to its backup, then into the ring); found by its trailer's id
PSX3D_COLD static bool undo_resolve(save_ref_t *r)
{
	char bak[sizeof(r->e.path) + 8];
	*r = undo.target;
	if (!access(r->e.path, F_OK) && (!r->id || file_id(r->e.path, NULL) == r->id))
		return true;
	if (!r->id)
		return false;
	snprintf(bak, sizeof(bak), "%s.bak", r->e.path);
	if (!access(bak, F_OK) && file_id(bak, NULL) == r->id &&
	    snprintf(r->e.path, sizeof(r->e.path), "%s", bak) < (int)sizeof(r->e.path))
		return true;
	hist_entry_t *list = malloc(HIST_RING * sizeof(*list));
	int n = list ? history_list(game_name, false, list, HIST_RING) : 0;
	bool found = false;
	for (int i = 0; i < n && !found; i++)
		if (file_id(list[i].path, NULL) == r->id) {
			r->e = list[i];
			found = true;
		}
	free(list);
	return found;
}

PSX3D_COLD static void slot_paths(int slot, char *path, char *thm, size_t len)
{
	char ext[16];
	state_ext(ext, sizeof(ext), slot);
	make_path(path, len, PSX3D_STATE_DIR, ext);
	snprintf(thm, len + 8, "%s.thm", path);
}

// a slot's own backup and temp files: an older version would show through
PSX3D_COLD static void slot_leftovers(const char *path)
{
	static const char *const ext[] = { ".bak", ".tmp", ".rep", ".thm.bak", ".thm.tmp" };
	char other[PATH_LEN + 16];
	for (size_t i = 0; i < sizeof(ext) / sizeof(ext[0]); i++) {
		snprintf(other, sizeof(other), "%s%s", path, ext[i]);
		remove(other);
	}
}

// Undo (or Redo) the latest risky action; true when the game changed
PSX3D_COLD static bool undo_run(void)
{
	save_ref_t target, now;
	hist_entry_t moved;
	char path[PATH_LEN], thm[PATH_LEN + 8];
	const char *what = undo_names[undo.what];
	bool redo = undo.redo;
	autosave_wait();
	if (!undo_resolve(&target)) {
		osd("Can't %s: that save is no longer kept", redo ? "redo" : "undo");
		undo.what = UNDO_NONE;
		return false;
	}
	if (undo.what == UNDO_SAVE || undo.what == UNDO_DELETE) {
		slot_paths(undo.slot, path, thm, sizeof(path));
		bool slot_used = !access(path, F_OK);
		// the slot's current file swaps places with the kept one
		if (slot_used && !history_adopt(game_name, undo.what == UNDO_SAVE ? HIST_SAVE : HIST_DELETE,
				strrchr(path, '.'), path, thm, saved_at(path), &moved)) {
			osd("Can't %s %s: could not keep the slot's save", redo ? "redo" : "undo", what);
			return false;
		}
		if (slot_used)
			now = ref_entry(&moved);
		bool back = !(undo.what == UNDO_DELETE && redo);   // redo delete: the slot stays empty
		if (back && !history_take(&target.e, path)) {
			if (slot_used)
				history_take(&moved, path);
			osd("Can't %s %s", redo ? "redo" : "undo", what);
			return false;
		}
		slot_leftovers(path);
		// what Redo brings back: the slot's previous file (now in the ring),
		// or the file that just came back into the empty slot
		if (!slot_used && !ref_of(path, undo.what == UNDO_SAVE ? HIST_SAVE : HIST_DELETE, &now))
			now = target;
		undo.target = now;
		undo.redo = !redo;
		osd("%s %s of slot %d", redo ? "Redid" : "Undid", what, undo.slot);
		return false;
	}
	bool have_now = safety_copy(HIST_RESTORE, &target, &now);
	if (!have_now)
		emu_osd_alert("Could not keep a copy of the game as it is now");
	if (!state_load_file(target.e.path, NO_SLOT))
		return false;
	if (have_now)
		undo.target = now;
	else
		undo.what = UNDO_NONE;
	undo.redo = !redo;
	osd(redo ? "Redid %s" : "Undid %s. Redo it in the menu", what);
	return true;
}

// Load slot state_slot: the game as it is now goes into the ring first
PSX3D_COLD static void state_load(void)
{
	save_ref_t copy;
	bool kept = safety_copy(HIST_LOAD, NULL, &copy);
	if (!state_load_from(state_slot))
		return;
	if (kept) {
		undo_set(UNDO_LOAD, -1, &copy);
		osd("Loaded slot %d. Undo it in the menu", state_slot);
	} else
		osd("Loaded slot %d", state_slot);
}

// Save to slot state_slot: a save already there goes into the ring first
PSX3D_COLD static void slot_save_over(void)
{
	char path[PATH_LEN], thm[PATH_LEN + 8];
	hist_entry_t old;
	autosave_wait();
	slot_paths(state_slot, path, thm, sizeof(path));
	bool kept = !access(path, F_OK) && !clip_replay_session && !dev_remote_game() &&
		history_adopt(game_name, HIST_SAVE, strrchr(path, '.'), path, thm, saved_at(path), &old);
	if (!state_save_to(state_slot)) {
		if (kept)
			history_take(&old, path);   // the old save back where it was
		osd("Could not write slot %d", state_slot);
		return;
	}
	if (kept) {
		save_ref_t r = ref_entry(&old);
		undo_set(UNDO_SAVE, state_slot, &r);
		osd("Saved to slot %d. The old save is in the backups", state_slot);
	} else
		osd("Saved to slot %d", state_slot);
}

// An automatic save from the ring: the game as it is now is kept first
PSX3D_COLD static void ring_restore(const hist_entry_t *e)
{
	save_ref_t copy, target = ref_entry(e);
	bool kept = safety_copy(HIST_RESTORE, &target, &copy);
	if (!state_load_file(e->path, NO_SLOT))
		return;
	if (kept) {
		undo_set(UNDO_RESTORE, -1, &copy);
		osd("Went back to an older save. Undo it in the menu");
	}
}

// A memory card copy back in: the game restarts (it keeps its own view of
// the card); the card as it was is kept in the backups
PSX3D_COLD static void card_put_back(const hist_entry_t *e)
{
	save_ref_t copy;
	bool kept = safety_copy(HIST_RESET, NULL, &copy);
	if (!cards_put_back(e)) {
		emu_osd_alert("Could not put that memory card back");
		return;
	}
	retro_reset();
	rewind_reset();
	if (kept)
		undo_set(UNDO_RESET, -1, &copy);
	osd("Memory card put back: the game restarted");
}

// Start game at "Continue?": the Continue point (and its backup) go into
// the ring, so a misclick is undone from the pause menu
PSX3D_COLD static void continue_declined(void)
{
	char sta[PATH_LEN], thm[PATH_LEN + 8], bak[PATH_LEN + 8], bthm[PATH_LEN + 16], tmp[PATH_LEN + 8];
	hist_entry_t e;
	unsigned reason, saved, id;
	if (clip_replay_session || dev_remote_game())
		return;
	make_path(sta, sizeof(sta), PSX3D_STATE_DIR, ".sta");
	snprintf(thm, sizeof(thm), "%s.thm", sta);
	snprintf(bak, sizeof(bak), "%s.bak", sta);
	snprintf(bthm, sizeof(bthm), "%s.thm.bak", sta);
	snprintf(tmp, sizeof(tmp), "%s.tmp", sta);
	if (!access(bak, F_OK)) {   // older first: the declined point is the newest entry
		state_file_info(bak, &reason, &saved, &id);
		history_adopt(game_name, reason == STATE_WHY_TIMER ? HIST_TIMER : HIST_EXIT, ".sta", bak, bthm, saved, NULL);
	}
	bool kept = !access(sta, F_OK) ? history_adopt(game_name, HIST_START, ".sta", sta, thm, saved_at(sta), &e) :
		!access(tmp, F_OK) && history_adopt(game_name, HIST_START, ".sta", tmp, NULL, saved_at(tmp), &e);
	if (kept) {
		save_ref_t r = ref_entry(&e);
		undo_set(UNDO_START, -1, &r);
		osd("Started fresh. Your old spot is kept: Undo in the menu");
	} else if (!access(sta, F_OK) || !access(bak, F_OK)) {
		continue_frozen = true;   // never written over: the old spot wins
		emu_osd_alert("Could not move your old spot to Backups: it is kept, auto-save is off this time");
	}
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

// ---------------------------------------------------------------- menu

enum { M_RESUME, M_UNDO, M_CLIP, M_SAVE, M_LOAD, M_DISC, M_CHEATS, M_SETTINGS, M_RESET, M_EXIT, M_COUNT };
typedef struct {
	int sel, items[M_COUNT], nitems;
	flist_t fl;
	char disc[48];
	char undo[40], undo_age[24];
} menu_t;
static const char *const menu_names[] = {"Resume", "Undo", "Record test clip", "Save state", "Load state", "Swap disc",
										 "Cheats", "Settings",	 "Reset game", "Exit to library"};
static const u32 menu_accent_cols[] = {0xff3c1ee6, 0xff14cdfa, 0xff78be1e, 0xffe66e28};
static void menu_draw_top(void *ud)
{
	(void)ud;
	C2D_DrawRectSolid(0, 0, 0.6f, 400, 240, C2D_Color32(2, 4, 16, 170));
	float dx = ui_depth(-0.8f);
	ui_text_ex(libui_font(), 201 + dx, 87, 1.1f, C2D_Color32(0, 0, 0, 150), C2D_AlignCenter, "PAUSED");
	ui_text_ex(libui_font(), 200 + dx, 86, 1.1f, C2D_Color32(255, 255, 255, 255), C2D_AlignCenter, "PAUSED");
	ui_text_ex(NULL, 200 + dx, 136, 0.55f, C2D_Color32(170, 185, 230, 255), C2D_AlignCenter, game_title);
	for (int i = 0; i < 4; i++)
		C2D_DrawRectSolid(140 + i * 30 + dx, 128, 0.65f, 28, 2, menu_accent_cols[i]);
}
static const char *menu_label(int i, void *ud)
{
	menu_t *m = ud;
	return m->items[i] == M_UNDO ? m->undo : menu_names[m->items[i]];
}
static const char *menu_value(int i, void *ud)
{
	menu_t *m = ud;
	int item = m->items[i];
	if (item == M_UNDO)
		return m->undo_age;
	// Every entry that opens another screen shows the same arrow.
	return item == M_DISC ? m->disc : item == M_SAVE || item == M_LOAD || item == M_CHEATS || item == M_SETTINGS ? ">" : NULL;
}
static void menu_draw_bottom(void *ud)
{
	menu_t *m = ud;
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, C2D_Color32(6, 9, 24, 255));
	ui_menu_header("Menu", ui_fit(game_title, .42f, 150));
	if (have_disk && disk.get_num_images)
		snprintf(m->disc, sizeof(m->disc), "Disc %u of %u", menu_disc + 1, disk.get_num_images());
	// Eight rows fit, so Exit stays visible when Swap disc is listed.
	flist_draw(&m->fl, m->nitems, m->sel, 0, 40, 320, 22, 8, menu_label, menu_value, m,
			   menu_accent_cols[m->sel & 3]);
	ui_text_ex(NULL, 160, 222, 0.42f, C2D_Color32(110, 120, 160, 255), C2D_AlignCenter,
			   m->items[m->sel] == M_DISC ? "Left/Right choose disc    A insert    B resume"
										  : "A select    B resume");
}
static bool game_menu_open;
static bool autosave_is_due(void);
static void autosave_start(const char *why);

// the rows (the Undo row shows the latest risky action and its age)
PSX3D_COLD static void menu_build(menu_t *m)
{
	m->nitems = 0;
	for (int i = 0; i < M_COUNT; i++) {
		if (i == M_UNDO) {   // the latest risky action, its age
			if (undo.what == UNDO_NONE || clip_replay_session || dev_remote_game())
				continue;
			u64 min = (osGetTime() - undo.at) / 60000;
			snprintf(m->undo, sizeof(m->undo), "%s %s", undo.redo ? "Redo" : "Undo", undo_names[undo.what]);
			if (min < 1)
				snprintf(m->undo_age, sizeof(m->undo_age), "just now");
			else
				snprintf(m->undo_age, sizeof(m->undo_age), "%llu min ago", min);
		}
		if (i == M_CLIP && (!clip_game_enabled || clip_active || clip_replay_session)) continue;
		if (i == M_DISC && !(have_disk && disk.get_num_images() > 1))
			continue;
		m->items[m->nitems++] = i;
	}
}

static void continue_prompt(const char *prefix);

// Returns false when the user wants to leave the game.
static bool menu_run(int initial_screen)
{
	if (initial_screen == DEV_UI_RESUME)
		return true;
	game_menu_open = true;
	input_gameplay(false);
	bottom_backlight(true);
	menu_t m = {0};
	menu_build(&m);
	if (have_disk && disk.get_image_index)
		menu_disc = disk.get_image_index();
	sfx_play(SFX_SELECT);
	audio_pause(true);
	cards_save_now();
	if (autosave_is_due())
		autosave_start("pause menu");
	if (initial_screen == DEV_UI_SETTINGS && settings_ui_run(true, NULL))
		goto resume;
	while (aptMainLoop()) {
		if (dev_tick(true)) {
			game_menu_open = false;
			return false;
		}
		int screen = dev_ui_take();
		if (screen == DEV_UI_RESUME)
			goto resume;
		if (screen == DEV_UI_CONTINUE || initial_screen == DEV_UI_CONTINUE) {
			// Remote test of the continue prompt (normally shown only at game start).
			char prefix[PATH_LEN];
			make_path(prefix, sizeof(prefix), PSX3D_STATE_DIR, "");
			continue_prompt(prefix);
			goto resume;
		}
		if (screen == DEV_UI_SETTINGS && settings_ui_run(true, NULL))
			goto resume;
		if (initial_screen == DEV_UI_STATES || screen == DEV_UI_STATES) {
			initial_screen = DEV_UI_NONE;
			char prefix[PATH_LEN];
			make_path(prefix, sizeof(prefix), PSX3D_STATE_DIR, "");
			int slot = dev_ui_slot;
			states_pick_t pick = { .page = STATES_PAGE_SLOTS };
			states_run(false, prefix, game_title, &slot, &pick);
			m.fl.init = false;
			continue;
		}
		input_poll();
		u32 k = ui_header_back(input_keys_down());
		if (k & KEY_TOUCH) {
			touchPosition tp;
			hidTouchRead(&tp);
			int row = flist_row_at(&m.fl, 40, 22, tp.py);
			if (tp.py >= 40 && tp.py < 216 && row >= 0 && row < m.nitems) {
				if (row == m.sel)
					k |= KEY_A;
				else {
					m.sel = row;
					sfx_play(SFX_MOVE);
				}
			}
		}
		if (k & KEY_B)
			break;
		if (k & KEY_DOWN) {
			m.sel = (m.sel + 1) % m.nitems;
			sfx_play(SFX_MOVE);
		}
		if (k & KEY_UP) {
			m.sel = (m.sel + m.nitems - 1) % m.nitems;
			sfx_play(SFX_MOVE);
		}
		int dir = k & KEY_LEFT ? -1 : k & (KEY_RIGHT | KEY_A) ? 1 : 0;
		if (dir)
			switch (m.items[m.sel]) {
			case M_CLIP:
				if ((k & KEY_A) && emu_record_start()) goto resume;
				break;
			case M_RESUME:
				if (k & KEY_A)
					goto resume;
				break;
			case M_UNDO:
				if (k & KEY_A) {
					undo_run();
					goto resume;
				}
				break;
			case M_SAVE:
			case M_LOAD:
				if (k & KEY_A) {
					bool saving = m.items[m.sel] == M_SAVE;
					char prefix[PATH_LEN];
					make_path(prefix, sizeof(prefix), PSX3D_STATE_DIR, "");
					int slot = state_slot;
					states_pick_t pick = { .page = STATES_PAGE_SLOTS };
					states_deleted.done = false;
					int page = states_run(saving, prefix, game_title, &slot, &pick);
					if (states_deleted.done) {
						save_ref_t r = ref_entry(&states_deleted.entry);
						undo_set(UNDO_DELETE, states_deleted.slot, &r);
					}
					if (page >= 0) {
						state_slot = slot;
						if (page == STATES_PAGE_SLOTS && saving)
							slot_save_over();
						else if (page == STATES_PAGE_SLOTS)
							state_load();
						else if (page == STATES_PAGE_RING)
							ring_restore(&pick.entry);
						else
							card_put_back(&pick.entry);
						goto resume;
					}
					state_slot = slot;
					m.fl.init = false;
					if (states_deleted.done) {
						menu_build(&m);
						m.sel = 0;
					}
				}
				break;
			case M_DISC: {
				unsigned dn = disk.get_num_images();
				if (k & KEY_A) {
					disk.set_eject_state(true);
					disk.set_image_index(menu_disc);
					disk.set_eject_state(false);
					osd("Disc %u inserted", menu_disc + 1);
					goto resume;
				}
				menu_disc = (menu_disc + dir + dn) % dn;
				sfx_play(SFX_MOVE);
			} break;
			case M_CHEATS:
				if ((k & KEY_A) && settings_ui_cheats())
					goto resume;
				break;
			case M_SETTINGS:
				if ((k & KEY_A) && settings_ui_run(true, NULL))
					goto resume;
				break;
			case M_RESET:
				if ((k & KEY_A) && ui_confirm_action("Reset game?",
						"Restart the game from the beginning. The game as it is now is kept in the backups.",
						"A  Reset game", true)) {
					save_ref_t copy;
					bool kept = safety_copy(HIST_RESET, NULL, &copy);
					retro_reset();
					rewind_reset();
					if (kept) {
						undo_set(UNDO_RESET, -1, &copy);
						osd("Game reset. Undo it in the menu");
					}
					goto resume;
				}
				break;
			case M_EXIT:
				if (k & KEY_A) {
					config_save();
					game_menu_open = false;
					return false;
				}
				break;
			}
		flist_update(&m.fl, m.sel, m.nitems, 8);
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
	game_frames = 0;
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
	if (osd_n) {   // above the touch pads and the FPS counter
		C2D_DrawRectSolid(0, 200, 0.9f, 320, 22, C2D_Color32(0, 0, 0, 200));
		ui_text_ex(NULL, 4, 204, 0.45f, osd_q[0].alert ? C2D_Color32(255, 200, 120, 255) : white, 0,
			ui_fit(osd_q[0].text, 0.45f, 312));
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
	state_stream_install();
	state_disc_query = disc_query;
	state_disc_map = disc_map;
	return true;
}

void emu_deinit(void)
{
	if (core_initialized)
		retro_deinit();
	core_initialized = false;
	cards_end();
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

// Close in the HOME Menu (also Power off from there) wakes us without the
// GPU: the HOME Menu keeps it for its "Closing software" screen. A state
// needs the GPU (its drawing is read back, the thumbnail is a GPU copy), so
// the auto-save is taken on the way to HOME and written only if we are then
// closed. Coming back drops it.
static void autosave_wait(void);
static void autosave_failed(const char *what);
static void *home_state;
static size_t home_state_size;
static u16 *home_thumb;

static void home_save_drop(void)
{
	free(home_state);
	free(home_thumb);
	home_state = NULL;
	home_thumb = NULL;
}

// State and thumbnail of this moment into RAM (needs the GPU)
static bool save_take(const char *who, void **state, size_t *state_size, u16 **thumb, u64 *thumb_ms)
{
	size_t size = retro_serialize_size();
	void *buf = size ? malloc(size) : NULL;
	// why, for the save backups' labels (the trailer keeps it)
	state_reason = !strcmp(who, "periodic") ? STATE_WHY_TIMER : !strcmp(who, "safety copy") ? STATE_WHY_MANUAL : STATE_WHY_EXIT;
	bool ok = buf && retro_serialize(buf, size);
	state_reason = STATE_WHY_MANUAL;
	if (!ok) {
		log_printf("%s: no auto-save (%s)\n", who, buf ? "state failed" : "no memory");
		free(buf);
		return false;
	}
	*state = buf;
	*state_size = size;
	u64 t0 = osGetTime();
	*thumb = state_thumb_take();
	if (thumb_ms)
		*thumb_ms = osGetTime() - t0;
	return true;
}

static void home_save_take(void)
{
	home_save_drop();
	if (clip_replay_session || dev_remote_game() || continue_frozen)
		return;
	u64 t0 = osGetTime();
	if (save_take("home", &home_state, &home_state_size, &home_thumb, NULL))
		log_printf("home: auto-save taken in %llums\n", osGetTime() - t0);
}

// State + thumbnail to <slot> (crash-safe, previous version kept as backup)
static bool save_put(int slot, void *state, size_t size, const u16 *thumb)
{
	char path[PATH_LEN], ext[16];
	if (slot == STATE_AUTO && continue_frozen)
		return false;
	state_ext(ext, sizeof(ext), slot);
	make_path(path, sizeof(path), PSX3D_STATE_DIR, ext);
	if (!write_file(path, state, size, slot == STATE_AUTO))
		return false;
	make_path(path, sizeof(path), PSX3D_STATE_DIR, "");
	if (!(thumb && state_thumb_save(path, slot, thumb)))
		log_printf("state: saved slot %d without a thumbnail\n", slot);
	return true;
}

static bool home_save_write(void)
{
	autosave_wait();
	bool ok = home_state && save_put(STATE_AUTO, home_state, home_state_size, home_thumb);
	home_save_drop();
	return ok;
}

// Auto-save while playing, so a crash, flat battery or power cut loses at
// most AUTOSAVE_MAX_MS of play. Once AUTOSAVE_MIN_MS have passed it waits for
// a moment where a short pause can't be seen (the game saved to its memory
// card, the disc is loading, a still or black screen, the pause menu); at
// AUTOSAVE_MAX_MS it saves anyway. The main thread only takes the state into
// RAM (the unavoidable part: it needs the GPU); a low-priority thread writes
// it to the SD card while the game goes on.
#define AUTOSAVE_MIN_MS (210 * 1000)
#define AUTOSAVE_MAX_MS (300 * 1000)
#define AUTOSAVE_RETRY_MS (60 * 1000)   // after a failed auto-save
static struct {
	Thread thread;
	volatile bool done;
	void *state;
	size_t size;
	u16 *thumb;
	int slot;
	bool ok;
	const char *why;
	u64 snapshot_ms, thumb_ms, write_ms;
	// moment detection, only while due
	u64 last;           // osGetTime of the last auto-save
	bool due;
	u32 cd_reads;
	int still, dark, probe;
} autosave;

// File reads, i.e. disc image reads while a game runs (linker wrap, also
// called by the CD read-ahead thread: a rough count is all it is used for)
static unsigned cd_reads;
size_t __real_fread(void *ptr, size_t size, size_t n, FILE *f);
size_t __wrap_fread(void *ptr, size_t size, size_t n, FILE *f)
{
	cd_reads++;
	return __real_fread(ptr, size, n, f);
}

static u64 autosave_min_ms(void)
{
	return sw_autosave_test > 0 ? sw_autosave_test * 1000ULL : AUTOSAVE_MIN_MS;
}

static void autosave_writer(void *arg)
{
	(void)arg;
	u64 t0 = osGetTime();
	autosave.ok = save_put(autosave.slot, autosave.state, autosave.size, autosave.thumb);
	autosave.write_ms = osGetTime() - t0;
	__atomic_store_n(&autosave.done, true, __ATOMIC_RELEASE);
}

static void autosave_free(void)
{
	free(autosave.state);
	free(autosave.thumb);
	autosave.state = NULL;
	autosave.thumb = NULL;
}

// Collect a finished write (wait: block until it is finished)
static void autosave_collect(bool wait)
{
	if (!autosave.thread || (!wait && !__atomic_load_n(&autosave.done, __ATOMIC_ACQUIRE)))
		return;
	threadJoin(autosave.thread, U64_MAX);
	threadFree(autosave.thread);
	autosave.thread = NULL;
	log_printf("periodic auto-save (%s): snapshot %llu ms (thumbnail %llu ms), write %llu ms%s\n", autosave.why,
		autosave.snapshot_ms, autosave.thumb_ms, autosave.write_ms, autosave.ok ? "" : ", FAILED");
	if (!autosave.ok)
		autosave_failed("write");
	autosave_free();
}

static void autosave_wait(void)
{
	autosave_collect(true);
}

static bool autosave_is_due(void)
{
	return osGetTime() - autosave.last >= autosave_min_ms();
}

// a Continue auto-save was just written some other way (sleep)
static void autosave_reset(void)
{
	autosave.last = osGetTime();
	autosave.due = false;
}

// the progress is not on the SD card yet: try again in a minute, and say so
static void autosave_failed(const char *what)
{
	u64 min = autosave_min_ms();
	autosave.last = osGetTime() - (min > AUTOSAVE_RETRY_MS ? min - AUTOSAVE_RETRY_MS : 0);
	autosave.due = false;
	log_printf("auto-save failed (%s): trying again in %d s\n", what, AUTOSAVE_RETRY_MS / 1000);
	osd(!strcmp(what, "write") ? "Auto-save failed. Is the SD card full?" : "Auto-save failed: not enough memory");
}

static void autosave_start(const char *why)
{
	bool test = sw_autosave_test > 0;   // test runs save to spare slot 9
	if (autosave.thread || (!test && (dev_remote_game() || continue_frozen)) || clip_replay_session || clip_active ||
	    dev_batch_active() || sw_ff || input_fast_forward() || input_rewind())
		return;
	autosave_reset();
	u64 t0 = osGetTime();
	if (!save_take("periodic", &autosave.state, &autosave.size, &autosave.thumb, &autosave.thumb_ms)) {
		autosave_failed("snapshot");   // not every frame: in a minute
		return;
	}
	autosave.snapshot_ms = osGetTime() - t0;
	autosave.slot = test ? 9 : STATE_AUTO;
	autosave.why = why;
	autosave.done = false;
	// Lowest priority on the game's core: it runs while the game waits for
	// the next frame; the SD card work itself happens in the system.
	autosave.thread = threadCreate(autosave_writer, NULL, 16 * 1024, 0x3F, -2, false);
	if (!autosave.thread) {   // no thread: write now rather than lose it
		autosave_writer(NULL);
		log_printf("periodic auto-save (%s): no thread, wrote in %llu ms\n", why, autosave.write_ms);
		if (!autosave.ok)
			autosave_failed("write");
		autosave_free();
	}
	game_frames = 0;   // the frame timing starts over after the pause
}

// Every SRM_CHECK_FRAMES: due yet? card_saved: the game just saved.
static void autosave_check(bool card_saved)
{
	autosave_collect(false);
	u64 since = osGetTime() - autosave.last;
	bool test = sw_autosave_test > 0;
	if (since >= (test ? 2 * autosave_min_ms() : AUTOSAVE_MAX_MS))
		autosave_start("timer");
	else if (since >= autosave_min_ms()) {
		if (card_saved)
			autosave_start("memory card saved");
		else if (!autosave.due) {
			autosave.due = true;
			autosave.still = autosave.dark = autosave.probe = 0;
			autosave.cd_reads = cd_reads;
		}
	}
}

// Every frame while due: a still or black screen for half a second.
// new_picture: the top screen got a new game picture (also movie frames).
static void autosave_frame(bool new_picture)
{
	if (new_picture) {
		autosave.still = 0;
		autosave.cd_reads = cd_reads;
	} else
		autosave.still++;
	if (++autosave.probe >= 15) {   // top screen brightness, 0-255
		autosave.probe = 0;
		int lum = video_probe_top();
		// near black only: dark games (Silent Hill) must not count
		autosave.dark = lum >= 0 && lum < 3 ? autosave.dark + 15 : 0;
	}
	if (autosave.still >= 30)
		autosave_start(cd_reads != autosave.cd_reads ? "disc loading" : "still screen");
	else if (autosave.dark >= 30)
		autosave_start("black screen");
}

// Closing the lid: write the Continue auto-save and the memory card before
// the console sleeps, in case the battery runs flat. The GPU is still ours.
static void sleep_save(void)
{
	u64 t0 = osGetTime();
	autosave_wait();
	cards_save_now();
	if (clip_replay_session || dev_remote_game() || continue_frozen || !gspHasGpuRight())
		return;
	void *state;
	size_t size;
	u16 *thumb;
	if (!save_take("sleep", &state, &size, &thumb, NULL))
		return;
	bool ok = save_put(STATE_AUTO, state, size, thumb);
	if (ok)
		autosave_reset();   // a failed one stays due
	free(state);
	free(thumb);
	log_printf("sleep: auto-save %s in %llu ms\n", ok ? "written" : "FAILED", osGetTime() - t0);
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
		autosave_wait();   // no SD file may stay open across the HOME Menu
		cards_save_now();  // the game's own saves first: they matter most
		home_save_take();
	} else if (hook == APTHOOK_ONSLEEP) {
		sleep_save();
	} else if (hook == APTHOOK_ONWAKEUP) {
		game_frames = 0;
	} else if (hook == APTHOOK_ONRESTORE) {
		home_save_drop();
		game_frames = 0;
		input_touch_reset();
		input_gameplay(!game_menu_open);
	}
}

// "Continue?": A loads the Continue point; Y opens the save backups (an
// older one); B starts the game and keeps the Continue point in the ring.
PSX3D_COLD static void continue_prompt(const char *prefix)
{
	for (;;) {
		int r = states_resume_prompt(prefix);
		if (r == RESUME_CONTINUE) {
			if (state_load_from(STATE_AUTO))
				osd("Continuing where you left off");
			return;
		}
		if (r == RESUME_START) {
			continue_declined();
			return;
		}
		if (r != RESUME_OLDER)
			return;
		states_pick_t pick = { .page = STATES_PAGE_RING };
		int slot = 0;
		if (states_run(false, prefix, game_title, &slot, &pick) == STATES_PAGE_RING && state_load_file(pick.entry.path, NO_SLOT)) {
			osd("Loaded an older auto-save");
			return;
		}
	}
}

// A memory card that could not be read: ask before the game runs on an
// empty stand-in. false: the player quits.
PSX3D_COLD static bool card_trouble_dialog(void)
{
	char label[PATH_LEN], msg[400];
	card_trouble_t t;
	while ((t = cards_trouble(label, sizeof(label))) != CARD_TROUBLE_NONE) {
		const char *name = base_name(label);
		snprintf(msg, sizeof(msg), t == CARD_TROUBLE_MEMORY ?
			"%s could not be read: not enough memory. Try again, or play without saving to it." :
			"%s is damaged and has no good backup. Try again, or play with an empty card (the damaged file is kept).",
			name);
		if (ui_confirm_choice("Memory card problem", msg, "A  Try again", "B  Other choices", false)) {
			cards_retry();
			continue;
		}
		if (!ui_confirm_choice("Memory card problem", t == CARD_TROUBLE_MEMORY ?
				"Play without saving to this memory card? Nothing the game saves on it is kept this time." :
				"Play with an empty memory card? The damaged file stays on the SD card as .bad.",
				t == CARD_TROUBLE_MEMORY ? "A  Play without saving" : "A  Use an empty card", "B  Quit", false))
			return false;
		cards_accept();
		if (t == CARD_TROUBLE_MEMORY)
			emu_osd_alert("Memory card is not saved this time (it could not be read)");
	}
	return true;
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
	saveid_begin(path, save_name, game_name, sizeof(game_name));
	disc_names_init(path);
	continue_frozen = false;
	memset(&undo, 0, sizeof(undo));
	osd_n = osd_frames = 0;
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
	core_auto_frameskip = false;
	memset(&psx3d_skip, 0, sizeof(psx3d_skip));
	psx3d_skip.class_timing = sw_pmuclass;
	psx3d_skip.timing = dev_enabled() || sw_stats || sw_prof || log_linked();
	audio_ticks = 0;
	memset(&frontend_sample, 0, sizeof(frontend_sample));
	have_disk = false;
	memset(&disk, 0, sizeof(disk));
	log_printf("loading %s\n", path);
	{   // before loading: the core reads the widescreen option at load. A
		// name another game owns: no per-game settings yet ('?' never names a file)
		char cfg[PATH_LEN + 8];
		snprintf(cfg, sizeof(cfg), saveid_provisional ? "%s [?]" : "%s", game_name);
		config_game_begin(cfg);
	}
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
	input_controller_apply(false);   // Controller: Analog starts the pad in analog mode

	retro_get_system_av_info(&av);
	fps_target = av.timing.fps;
	has_audio = audio_init(av.timing.sample_rate);
	{
		// another game's saves are never read or written (saveid.c)
		bool created;
		if (saveid_confirm(CdromId, game_name, sizeof(game_name), &created) || saveid_provisional) {
			config_game_end();
			config_game_begin(game_name);
			options_mark_updated();
		}
		if (created)
			osd("Saves kept apart from another game with the same file name");
		if (saveid_unrecorded)
			emu_osd_alert("Could not record which game owns these saves. Is the SD card full?");
	}
	{
		char srm[PATH_LEN];
		make_path(srm, sizeof(srm), PSX3D_SAVE_DIR, ".srm");
		cards_begin(srm);
	}
	if (clip_replay_session || dev_remote_game())
		cards_accept();   // tests never wait for an answer (and never write over it)
	else if (!card_trouble_dialog()) {
		log_printf("game: quit at the memory card question\n");
		retro_unload_game();
		cards_end();
		config_game_end();
		audio_exit();
		retro_deinit();
		core_initialized = false;
		video_game_end();
		clip_close();
		return true;
	}
	if (clip_replay_session) {
		cheats_unload();
		retro_cheat_reset();
	} else
		cheats_load(game_name);
	if (sw_wsscreen >= 0) config_set_screen_mode(sw_wsscreen);
	config_ws_sync();
	psx3d_prof_on = sw_prof;

	{
		char prefix[PATH_LEN];
		make_path(prefix, sizeof(prefix), PSX3D_STATE_DIR, "");
		// Remote-started games skip continue and the save on exit, so test runs
		// never touch the player's place. Local play always offers continue.
		if (clip_requested<0 && !dev_remote_game() && sw_state < 0)
			continue_prompt(prefix);
	}
	if (clip_requested<0 && game_capabilities().rewind && config_rewind_on())
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
	// Only the slow case is worth a message on the clean game screen.
	if (!__ctr_svchax)
		osd("%s", slow_mode_notice(true));

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
	psx3d_skip_state skip_previous = psx3d_skip;
	u64 pace_ticks = 0, misc_ticks = 0;
	unsigned held = 0, presented = 0, emulated = 0, top_paints = 0, bottom_paints = 0;
	u64 picture_last = 0;
	double picture_sum = 0, picture_square = 0;
	unsigned picture_intervals = 0;
	bool previous_special = false;
	int previous_speed_icon = 0;
	// Pacer self-check: frames dropped after stalls longer than the catch-up
	// window, and how many seconds in a row speed stayed low with idle time.
	u64 stall_ticks = 0;
	unsigned stalls = 0, slow_seconds = 0, catch_up = 3;
	u64 next_deadline = svcGetSystemTick();
	u64 stats_deadline = next_deadline + SYSCLOCK_ARM11;
	u64 smart_clock = next_deadline;
	aptHookCookie apt_cookie;
	aptHook(&apt_cookie, game_apt, NULL);
	autosave_reset();
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
			psx3d_skip.budget_debt = psx3d_skip.frame_budget = psx3d_skip.picture_credit = 0;
			picture_last = picture_intervals = 0; picture_sum = picture_square = 0;
			psx3d_skip.phase = psx3d_skip.streak = 0;
			next_deadline = now_t;
			smart_clock = now_t;
		}
		bool late = now_t > next_deadline + frame_ticks / 8;   // last frame overran its slot
		late_frames += late;
		if (previous_special != ((sw_ff || (!dev_batch_active() && input_fast_forward())) || input_rewind())) {
			next_deadline = now_t;
			psx3d_skip.budget_debt = psx3d_skip.frame_budget = psx3d_skip.picture_credit = 0;
			picture_last = picture_intervals = 0; picture_sum = picture_square = 0;
			psx3d_skip.phase = psx3d_skip.streak = 0;
			smart_clock = now_t;
		}
		previous_special = (sw_ff || (!dev_batch_active() && input_fast_forward())) || input_rewind();
		if (!(sw_ff || (!dev_batch_active() && input_fast_forward())) && now_t < next_deadline) {
			svcSleepThread((next_deadline - now_t) * 1000000000ULL / SYSCLOCK_ARM11);
			frame_pace = svcGetSystemTick() - now_t;
			pace_ticks += frame_pace;
		}
		else if (now_t > next_deadline + catch_up * frame_ticks) {
			stall_ticks += now_t - next_deadline;
			stalls++;
			// Far behind (loading, menu): don't race to catch up, but keep a
			// few frames of lateness. Scenes that alternate heavy and light
			// frames below full speed then run the light ones on instead of
			// sleeping while the game is still behind (at most 50 ms catch-up).
			next_deadline = now_t - PACE_DEBT * frame_ticks;
		}
		next_deadline += frame_ticks;

		// The core queries this option on load and after options_mark_updated().
		// Reuse its resolved value instead of walking option/config tables on
		// every vblank (including Off) and evicting Old 3DS's hot core code.
		psx3d_skip.enabled = !previous_special && core_auto_frameskip;
		psx3d_skip.max_skips = 4; // internal fairness bound
		psx3d_skip.level = psx3d_skip.numerator = psx3d_skip.denominator = 0;
		psx3d_skip.smart = psx3d_skip.enabled;
		if (!psx3d_skip.enabled) psx3d_skip.phase = psx3d_skip.streak = 0;
		if (abuf_status_cb) {
			unsigned occ = has_audio ? audio_occupancy() : 50;
			// auto frameskip: skip drawing when sound runs low or the frame
			// clock says we are behind (the sound buffer alone rarely shows it)
			abuf_status_cb(true, occ, occ < 20 || late);
		}
		u64 t0 = svcGetSystemTick();
		static bool rewind_hint;
		bool rewinding = !dev_batch_active() && input_rewind();
		bool ff = sw_ff || (!dev_batch_active() && input_fast_forward());
		speed_icon = rewinding && rewind_active() ? -1 : ff ? 1 : 0;
		if (rewinding && rewind_active()) {
			// step back about 6 times a second of game time, shown at 20/s
			static int rw;
			if (++rw >= 3) {
				rw = 0;
				int step = rewind_step();
				if (step < 0 && state_last_result == STATE_PARTLY) {
					// never play on in a half-restored game (it could save its card)
					retro_reset();
					rewind_reset();
					emu_osd_alert("Rewind failed: the game was restarted");
				} else if (step < 0)
					emu_osd_alert("Rewind failed: the game goes on from here");
				else if (!step)
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
		gpustereo_frame();   // real 3D only costs anything while the slider is up
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
			smart_clock = next_deadline;
			stats_deadline = next_deadline + SYSCLOCK_ARM11;
			psx3d_skip.budget_debt = psx3d_skip.frame_budget = psx3d_skip.picture_credit = 0;
			picture_last = picture_intervals = 0; picture_sum = picture_square = 0;
			psx3d_skip.phase = psx3d_skip.streak = 0;
			held = presented = emulated = 0;
			pace_ticks = audio_ticks = misc_ticks = 0;
			top_paints = bottom_paints = 0;
			skip_previous = psx3d_skip;
			continue;
		}

		// Query once after worker sync. A held picture needs no top target,
		// transfer or C2D pass; a bottom-only change leaves top scanout alone.
		bool changed = video_display_changed();
		static float last_slider;
		float slider = slider_get();
		static bool alert_was_up;
		bool alert_up = osd_alert_up();
		bool repaint_top = changed || game_frames < 2 || speed_icon || alert_up || alert_up != alert_was_up ||
			speed_icon != previous_speed_icon || slider != last_slider;
		alert_was_up = alert_up;
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
		bool stats_changed = statsview_redraw(osd_n);
		bool repaint = game_frames < 2 || stats_changed || touch_changed || osd_dirty;
		osd_dirty = false;
		if (repaint_top || repaint) ui_begin();
		t1 = svcGetSystemTick();
		if (repaint_top || repaint) {
			video_present_screens(true, repaint_top, speed_icon || alert_up ? game_draw_top : NULL,
				repaint ? game_draw_bottom : NULL, NULL);
			presented++;
			top_paints += repaint_top;
			bottom_paints += repaint;
		} else { video_hold(); held++; }
		u64 end_work = svcGetSystemTick();
		if (repaint_top && dev_enabled()) {
			if (picture_last) {
				double gap = (end_work - picture_last) / (double)CPU_TICKS_PER_MSEC;
				picture_sum += gap; picture_square += gap * gap; picture_intervals++;
			}
			picture_last = end_work;
		}
		u64 compose_ticks = end_work - t1;
		t_present += compose_ticks;

		++game_frames;
		osd_tick();
		if (repaint_top || repaint || game_frames == 120) {
			// Capability/layout changes invalidate the next shown frame.
			if (!game_capabilities().rewind && rewind_active()) rewind_stop();
			if (touch_buttons_active() || g_cfg.show_fps || osd_n)
				bottom_backlight(true);
			else if (game_frames >= 120 && g_cfg.bottom_off)
				bottom_backlight(false);
		}

		if (++srm_counter >= SRM_CHECK_FRAMES) {
			srm_counter = 0;
			autosave_check(cards_check());
		} else if (autosave.due)
			autosave_frame(repaint_top);
		present_count++;
		if (psx3d_skip.timing)
			misc_ticks += svcGetSystemTick() - work_start - frame_pace - run_ticks - compose_ticks;
		u64 report_start = psx3d_skip.timing ? svcGetSystemTick() : 0;
		u64 now = end_work >= stats_deadline ? osGetTime() : fps_t0;
		if (now - fps_t0 >= 1000 || (clip_replay_session && clip_status.done)) {
			log_hold(true);   // the report's lines go to the SD card in one write
			if (clip_replay_session && clip_status.done) { now=osGetTime(); if (now<=fps_t0) now=fps_t0+1; }
			fps_measured = fps_frames * 1000.0f / (now - fps_t0);
			double per_frame_ms = 1.0 / (emulated ? emulated : 1) / CPU_TICKS_PER_MSEC;
			unsigned flip_drawn = psx3d_skip.flips_drawn - skip_previous.flips_drawn;
			unsigned flip_skipped = psx3d_skip.flips_skipped - skip_previous.flips_skipped;
			frontend_sample = (emu_frontend_sample){
				.emulated = emulated, .elapsed_ms = now - fps_t0,
				.presented = presented, .held = held, .top = top_paints, .bottom = bottom_paints,
				.run_ms = t_run * per_frame_ms, .present_ms = t_present * per_frame_ms,
				.audio_ms = audio_ticks * per_frame_ms, .misc_ms = misc_ticks * per_frame_ms,
				.pace_ms = pace_ticks * per_frame_ms,
				.picture_mean = picture_intervals ? picture_sum / picture_intervals : 0,
				.picture_square = picture_intervals ? picture_square / picture_intervals : 0,
				.picture_intervals = picture_intervals,
				.pica_ms = (unsigned)(psx3d_skip.gpu_main_ticks - skip_previous.gpu_main_ticks) * per_frame_ms,
				.spu_ms = (unsigned)(psx3d_skip.spu_main_ticks - skip_previous.spu_main_ticks) * per_frame_ms,
				.spu_wait_ms = (unsigned)(psx3d_skip.spu_wait_ticks - skip_previous.spu_wait_ticks) * per_frame_ms,
				.draw_fraction = psx3d_skip.smart ? (flip_drawn + flip_skipped ? (float)flip_drawn / (flip_drawn + flip_skipped) : 1) : !psx3d_skip.enabled ? 1 : psx3d_skip.denominator ? (float)psx3d_skip.numerator / psx3d_skip.denominator : (float)psx3d_skip_draw(psx3d_skip.level) / psx3d_skip_period(psx3d_skip.level),
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
						presented, held, 0u, 0.0,
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
			{
				// Self-check: below full speed while the pacer still sleeps means
				// time is lost to the timing logic, not to a slow console. After
				// two such seconds, start fresh timing (fps from the core, skip
				// estimates), and if stalls dropped the time, catch up longer ones.
				double speed = emulated * 100000.0 / (now - fps_t0) / fps_target;
				double idle_ms = pace_ticks / (double)(emulated ? emulated : 1) / CPU_TICKS_PER_MSEC;
				slow_seconds = speed < 99.0 && idle_ms > 1.5 && !speed_icon ? slow_seconds + 1 : 0;
				if (slow_seconds >= 2) {
					struct retro_system_av_info info;
					retro_get_system_av_info(&info);
					if (info.timing.fps > 1.0)
						fps_target = info.timing.fps;
					if (stalls && catch_up < 8)
						catch_up = 8;
					log_printf("pacer: %.1f%% speed with %.1fms/frame idle, %u stalls dropped %.0fms; fresh timing (%.3f fps, catch-up %u frames)\n",
						speed, idle_ms, stalls, stall_ticks / (double)CPU_TICKS_PER_MSEC, fps_target, catch_up);
					psx3d_skip.budget_debt = psx3d_skip.frame_budget = psx3d_skip.picture_credit = 0;
					psx3d_skip.phase = psx3d_skip.streak = 0;
					next_deadline = svcGetSystemTick();
					smart_clock = next_deadline;
					slow_seconds = 0;
				}
				stall_ticks = 0;
				stalls = 0;
			}
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
			picture_sum = picture_square = 0; picture_intervals = 0;
			pace_ticks = audio_ticks = misc_ticks = 0;
			top_paints = bottom_paints = 0;
			held = presented = emulated = 0;
			present_count = 0;
			fps_frames = 0;
			fps_t0 = now;
			stats_deadline = end_work + SYSCLOCK_ARM11;
			log_hold(false);
		}
		if (psx3d_skip.timing) misc_ticks += svcGetSystemTick() - report_start;
		if (psx3d_skip.enabled) {
			u64 end = svcGetSystemTick(), wall = end - smart_clock;
			smart_clock = end;
			psx3d_smart_observe(&psx3d_skip, (wall - frame_pace) / (double)CPU_TICKS_PER_MSEC,
				1000.0 / fps_target, wall / (double)CPU_TICKS_PER_MSEC * auto_floor_scale);
		}
		if (clip_replay_session && clip_status.done) break;
	}

	psx3d_skip.enabled = 0;
	aptUnhook(&apt_cookie);
	input_fast_forward_off();
	input_gameplay(false);
	options_fast_forward(false);
	pmu_stop();
	// Closed from the HOME Menu: no GPU (see home_state). Stop the sound and
	// keep what needs no GPU. Tearing the core and renderer down would wait
	// for the GPU forever, so they stay as they are; the system ends us.
	if (!gspHasGpuRight()) {
		log_resume();
		audio_exit();
		log_printf("game: closed from the HOME Menu\n");
		autosave_wait();
		cards_save_now();   // before the 4.4 MB state: the game's own saves first
		if (!clip_replay_session && !dev_remote_game())
			log_printf("auto-save: %s\n", home_save_write() ? "written" : "none");
		home_save_drop();
		config_game_end();
		clip_close();
		core_initialized = false;   // emu_deinit must not tear the core down
		return true;
	}
	home_save_drop();
	autosave_wait();
	log_printf("game: leaving\n");
	rewind_stop();
	cards_save_now();   // before the 4.4 MB state: the game's own saves first
	if (!clip_replay_session && !dev_remote_game() && !continue_frozen)
		log_printf("auto-save: %s\n", state_save_to(STATE_AUTO) ? "written" : "failed");
	bottom_backlight(true);
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
