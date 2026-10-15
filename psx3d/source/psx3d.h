// PSX3D - standalone PS1 emulator frontend for Nintendo 3DS.
// Hosts the PCSX-ReARMed libretro core (GPL v2) without RetroArch.
#ifndef PSX3D_H
#define PSX3D_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>
#include "libretro.h"

#define PSX3D_VERSION  "0.1.0"
#define PSX3D_DIR      "sdmc:/3ds/PSX3D"
#define PSX3D_BIOS_DIR PSX3D_DIR "/bios"
#define PSX3D_SAVE_DIR PSX3D_DIR "/saves"
#define PSX3D_STATE_DIR PSX3D_DIR "/states"
#define PSX3D_CONFIG   PSX3D_DIR "/config.ini"
#define PSX3D_LOG      PSX3D_DIR "/log.txt"

#define PATH_LEN 512

// log.c
void log_init(void);
void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_close(void);

// config.c - frontend settings plus core option overrides
typedef struct {
	char last_dir[PATH_LEN];
	char games_dir[PATH_LEN];   // Y in the browser jumps here
	char last_title[256];       // library: game to select on start
	int  smooth;       // bilinear filtering
	int  show_fps;
	int  stereo;       // 3D slider: 0 off, 1 screen depth, 2 real (per-object)
	int  debug;        // show debug tools in the menu
	int  stretch;      // stretch the picture to 400x240
	int  sounds;       // interface sounds
	int  music;        // library music
	int  bottom_off;   // backlight off on the bottom screen while playing
} psx3d_config;

extern psx3d_config g_cfg;
void config_load(void);
void config_save(void);
const char *config_core_override(const char *key);
bool config_widescreen(void);
int config_screen_mode(void);
void config_set_screen_mode(int mode);
void config_set_widescreen(bool on);
void config_set_core_override(const char *key, const char *value);

// options.c - core option definitions (from SET_VARIABLES) and lookup
void options_register(const struct retro_variable *vars);
const char *options_get(const char *key);
bool options_take_updated(void);
void options_mark_updated(void);
int  options_count(void);
const char *options_key(int i);
const char *options_desc(int i);
const char *options_cycle(int i, int dir);

// video.c
bool video_init(void);
void video_exit(void);
void *video_framebuffer(unsigned width, unsigned height, size_t *pitch);
void video_frame(const void *data, unsigned width, unsigned height, size_t pitch);
void video_present(bool show_game, void (*draw_top)(void *),
	void (*draw_bottom)(void *), void *ud);
void video_present_vsync(bool show_game, void (*draw_top)(void *),
	void (*draw_bottom)(void *), void *ud);
C3D_RenderTarget *video_bottom_target(void);
extern void (*video_pre_draw)(void);
extern int ui_eye;
extern float ui_parallax;
extern bool ui_wants_3d;
// x shift for a UI layer at depth d (>0 behind the screen, <0 in front)
static inline float ui_depth(float d) { return ui_eye * d * ui_parallax; }
u64 video_take_wait_ticks(void);
u64 video_take_xfer_ticks(void);
bool video_snapshot(u16 *out, int w, int h);

// savestates.c - save state browser with screenshots
bool state_thumb_write(const char *prefix, int slot);
int states_run(bool saving, const char *prefix, const char *title, int *slot);

// stereo.c - real 3D from the core's depth map
bool stereo_init(void);
void stereo_exit(void);
bool stereo_prepare(float max_px);
void stereo_draw(C3D_Tex *tex, float x0, float dw, unsigned frame_w, unsigned frame_h, int eye, float max_px);
void config_sync_stereo(void);
void config_set_stereo_active(bool on);
void stereo_draw_preview(float x0, float y0, float cell);
const char *stereo_stats(void);
void psx3d_depth_frame(void);   // in the core

// audio.c
bool audio_init(double sample_rate);
void audio_exit(void);
size_t audio_write(const int16_t *data, size_t frames);
void audio_pause(bool paused);
void audio_stats(char *buf, size_t len);
unsigned audio_occupancy(void);
bool audio_capture_start(void);
bool audio_test_tone_toggle(void);

// input.c
void input_poll(void);
int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id);
u32 input_keys_down(void);
u32 input_keys_held(void);
bool touch_buttons_active(void);
bool menu_touched(void);
void touch_buttons_draw(void);

// ui.c - text drawing on the bottom screen
void ui_init(void);
void ui_exit(void);
void ui_text(float x, float y, float size, u32 color, const char *fmt, ...);
void ui_rect(float x, float y, float w, float h, u32 color);
void ui_begin(void);
void ui_text_ex(C2D_Font font, float x, float y, float size, u32 color, u32 flags, const char *s);

#define FLIST_MAX 2048
typedef struct {
	bool init;
	float hl, hl_v;               // highlighted row (fractional), spring
	float scroll, scroll_v;       // first visible row (fractional), spring
	float scroll_target;
	float lift[FLIST_MAX];        // 0..1, eases toward 1 for the selected row
	float t;
} flist_t;
void flist_update(flist_t *f, int sel, int n, int rows);
void flist_draw(const flist_t *f, int n, int sel, float x, float y0, float w, float row_h, int rows,
	const char *(*label)(int, void *), const char *(*value)(int, void *), void *ud, u32 accent);
int flist_row_at(const flist_t *f, float y0, float row_h, float py);

// library.c - one entry per game, clean titles, discs grouped
typedef struct {
	char *title;        // "Metal Gear Solid"
	char *cover_key;    // No-Intro name of disc 1, used to find box art
	char *group;        // scan-time only
	u32 dir_id;         // scan-time only
	int ndiscs;
	char *discs[8];
	int disc_no[8];
} game_t;
bool library_load(const char *root, bool rescan);
int library_count(void);
const game_t *library_get(int i);
const char *library_launch_path(const game_t *g, char *buf, size_t len);

// covers.c - box art
#define COVER_SIZE 128
void covers_init(void);
void covers_exit(void);
C3D_Tex *covers_get(int game);
void covers_reset(void);
void covers_focus(int game);
void covers_pump(void);
bool covers_busy(void);
bool covers_stop(void);
void covers_stop_wait(void);
void covers_resume(void);

// uisound.c - interface sounds and library music
enum { SFX_MOVE, SFX_SELECT, SFX_BACK, SFX_LAUNCH, SFX_COUNT };
void uisound_init(void);
void uisound_exit(void);
void sfx_play(int which);
void music_play(bool on);
void music_update(void);

// libui.c - the game library screen
void libui_init(void);
C2D_Font libui_font(void);
void libui_exit(void);
int libui_run(char *out_path, size_t len);
void libui_select_title(const char *title);
const game_t *libui_picked(void);

// browser.c - returns true and fills path when a game is chosen
bool browser_run(char *out_path, size_t out_len);

// emu.c
bool emu_init(void);
void emu_deinit(void);
bool emu_run_game(const char *path, const char *save_name, const char *title);

extern int __ctr_svchax;
extern bool sw_nonet, sw_nosound, sw_noirrst, sw_noaa, sw_nolcd, sw_nocovers;
extern bool g_new3ds;

#endif
