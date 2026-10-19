// PSX3D - standalone PS1 emulator frontend for Nintendo 3DS.
// Hosts the PCSX-ReARMed libretro core (GPL v2) without RetroArch.
#ifndef PSX3D_H
#define PSX3D_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>
#include "libretro.h"

#define PSX3D_VERSION  "0.3.1"
#define PSX3D_DIR      "sdmc:/3ds/PSX3D"
#define PSX3D_BIOS_DIR PSX3D_DIR "/bios"
#define PSX3D_SAVE_DIR PSX3D_DIR "/saves"
#define PSX3D_STATE_DIR PSX3D_DIR "/states"
#define PSX3D_CONFIG   PSX3D_DIR "/config.ini"
#define PSX3D_LOG      PSX3D_DIR "/log.txt"

#define PATH_LEN 512

// log.c
void log_init(void);
bool log_linked(void);
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
	int  autosave;     // save on exit, offer to continue next time
	int  audio_stretch; // maximum slowdown in percent, 0 disables (0..50)
	int  touch_stick;  // Old 3DS virtual right stick
	int  touch_click_left; // global New 3DS bottom-screen layout: 0 right, 1 left
} psx3d_config;

extern psx3d_config g_cfg;
void config_load(void);
void config_save(void);
void config_reset_defaults(void);
bool config_game_reset(void);
void config_controls_save(void);
void config_controls_defaults(void);
const char *config_core_override(const char *key);
bool config_widescreen(void);
int config_screen_mode(void);
void config_set_screen_mode(int mode);
void config_game_begin(const char *game);
void config_game_set_screen(int mode);
void config_game_end(void);
bool config_in_game(void);
void config_set_ws_native(bool on);
void config_set_widescreen(bool on);
void config_set_core_override(const char *key, const char *value);

// options.c - core option definitions (from SET_VARIABLES) and lookup
void options_register(const struct retro_variable *vars);
const char *options_get(const char *key);
bool options_take_updated(void);
void options_mark_updated(void);
int  options_count(void);
void options_register_v1(const struct retro_core_option_definition *defs);
const char *options_info(int i);
const char *options_impact(int i);
void options_fast_forward(bool on);
const char *options_key(int i);
const char *options_desc(int i);
const char *options_value(int i);
const char *options_cycle(int i, int dir);

// video.c
bool video_init(void);
bool video_game_begin(void);
void video_game_end(void);
bool renderer_is_gpu(void);   // active renderer, independent of pending settings
void video_exit(void);
void *video_framebuffer(unsigned width, unsigned height, size_t *pitch);
void video_frame(const void *data, unsigned width, unsigned height, size_t pitch);
void video_gpu_frame_begin(void);
// Last completed stats interval, main-thread access for remote stats.
// All costs are ms per emulated vblank. Audio is nested in run; present
// includes required offscreen submission on held frames. Misc includes the
// previous interval's reporting/SD writes. Pacing is excluded from work.
typedef struct {
	unsigned emulated, elapsed_ms, presented, held, top, bottom;
	float run_ms, present_ms, audio_ms, misc_ms, pace_ms;
	float spu_ms, spu_wait_ms, pica_ms;
	float phase_ms[8], draw_fraction;
	unsigned skip_level, drawn, skipped;
} emu_frontend_sample;
const emu_frontend_sample *emu_frontend_stats(void);

void video_hold(void);
bool video_display_changed(void);
void video_present_screens(bool show_game, bool repaint_top, void (*draw_top)(void *),
	void (*draw_bottom)(void *), void *ud);
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
void video_take_gpu_stats(float *gpu, u64 *gap);
u64 video_take_heap_flush_ticks(void);
void video_cache_targeted(bool enabled);
u8 video_cache_command_list(const void *commands, u32 bytes, u8 flags);
u8 video_cache_frame_end(u8 flags);
extern bool sw_gpu_flush, sw_gpu_async, sw_nogputhread;
// Private extension implemented by our citro3d render queue.
#define VIDEO_FRAME_OVERLAP BIT(7)
void video_gpu_wait_previous(void);
bool video_gpu_has_previous(void);
u64 video_take_pending_wait_ticks(void);

int video_probe_top(void);
// core (patches/0002): real 3D renders each eye into its own VRAM
void psx3d_set_strength(float px);
void psx3d_set_half3d(int on);
// core profiling (libpcsxcore/psx3d_prof.h)
extern unsigned long long psx3d_prof_t[6];
extern unsigned int psx3d_prof_c[6];
extern int psx3d_prof_on;
void new_dynarec_print_stats(void);
extern bool sw_frontend, sw_floataudio, sw_ff;
extern int sw_rtfscap, sw_rtfslevel;
extern bool sw_rtfs, sw_rtfswindow, sw_rtfsmodel;
extern bool sw_prof, sw_stats, sw_o3ds, sw_fs1, sw_fs3, sw_noskip, sw_nosputhread, sw_acap;
extern int sw_state, sw_gldbg, sw_vramdump;
extern unsigned pica_prims;
extern bool pica_texmask, pica_texmemo;
extern int psx3d_skipgate;
extern int sw_pmu0, sw_pmu1;
extern char sw_clock[8], sw_cdra[8], sw_opt[8][48];
// pmu.c - ARM11 performance counters (test tool)
void pmu_start(int ev0, int ev1);
void pmu_begin(void);
void pmu_end(void);
extern bool sw_pmuphase;
extern void (*pica_pmu)(int end);
void pmu_renderer(double values[4]);
extern bool sw_pmuclass, sw_rtfsdense;
u64 emu_audio_ticks(void);
void pmu_category_detail(double values[21]);
void pmu_categories(double cycles[3], unsigned counts[3]);
void pmu_report(void);
void pmu_stop(void);
void psx3d_stats(float *zc, int *n3d, int *n2d, int *dmin, int *dmax, int *zmin, int *zmax);
const uint16_t *psx3d_right_get(int *x, int *y, int *w, int *h, int *rgb24);
bool video_snapshot(u16 *out, int w, int h);

// csnd_out.c - sound without DSP firmware
bool csnd_acquire(void);
void csnd_release(void);
int csnd_channel(int n);
double csnd_real_rate(u32 rate);
void csnd_setup(int chn, bool loop, u32 rate, float vol, float pan, const s16 *data, u32 bytes);
void csnd_set_volume(int chn, float vol, float pan);

// rewind.c - per game, off by default
bool rewind_start(void);
void rewind_stop(void);
bool rewind_active(void);
void rewind_capture(void);
bool rewind_step(void);
void rewind_reset(void);

// gamecfg.c - per-game settings (sdmc:/3ds/PSX3D/games/<game>.ini)
void gamecfg_load(const char *game);
void gamecfg_end(void);
bool gamecfg_reset(void);
void gamecfg_unset(const char *key);
const char *gamecfg_get(const char *key);
int gamecfg_int(const char *key, int def);
void gamecfg_set(const char *key, const char *value);
void gamecfg_set_int(const char *key, int v);

// cheats.c
void cheats_load(const char *game);
void cheats_unload(void);
int cheats_count(void);
const char *cheats_desc(int i);
bool cheats_on(int i);
void cheats_toggle(int i);
bool cheats_widescreen(bool on);
const char *cheats_add_dialog(void);
bool cheats_forced(int i);

// dspdump.c
bool dsp_firmware_ensure(void);

// savestates.c - save state browser with screenshots
#define STATE_AUTO -1
bool state_thumb_write(const char *prefix, int slot);
bool states_resume_prompt(const char *prefix);
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
void audio_hash_start(u32 frames);
bool audio_test_tone_toggle(void);

// input.c
void input_poll(void);
int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id);
u32 input_keys_down(void);
u32 input_keys_held(void);
bool touch_buttons_active(void);
bool menu_touched(void);
bool input_stats_touched(void);
bool input_fast_forward(void);
void input_fast_forward_off(void);
bool input_rewind(void);
#define INPUT_PHYSICAL_N 10
#define INPUT_REMAP_N 16
#define INPUT_TOUCH_N (INPUT_REMAP_N - INPUT_PHYSICAL_N)
int input_controls_count(void);
int input_control_slot(int row);
const char *input_controls_help(void);
enum { INPUT_BIND_PRESET = -2, INPUT_BIND_HIDE = -1 };
enum { INPUT_FF_TOGGLE = 16, INPUT_FF_HOLD, INPUT_REWIND_HOLD };
#define INPUT_HOTKEY_N 2
int input_hotkey_get(int i);
bool input_hotkey_is_default(int i);
void input_hotkey_set(int i, int combo);
void input_hotkey_load(int i, const char *name);
const char *input_hotkey_key_name(int i);
const char *input_hotkey_combo_name(int combo);
void input_gameplay(bool enabled);
const char *input_map_key_name(int i);
const char *input_ps1_name(int id);
int input_map_get(int i);
int input_map_raw(int i);
int input_map_factory(int i);
int input_stick_binding(void);
void input_stick_set(int binding);
bool input_map_is_default(int i);
void input_map_set(int i, int id);
void input_map_cycle(int i, int dir);
void input_map_reset(void);
void touch_buttons_draw(void);
enum { TOUCH_CONTROLS_AUTO, TOUCH_CONTROLS_ON, TOUCH_CONTROLS_OFF };
void input_touch_begin(void);
void input_touch_disc_log(const char *message);
void input_touch_reset(void);
bool input_touch_changed(void);
const char *input_control_label(int i, void *ud);
const char *input_control_value(int i, void *ud);
u32 input_control_color(int row);
void input_control_cycle(int i, int dir);

// presets.c: curated per-console serial profiles. Boolean/policy fields use
// -1 to inherit the uncurated console defaults. Renderer selects the per-console default.
typedef struct {
	int fast_forward, rewind, touch_controls;
	int needs_l2r2, needs_l3r3, needs_right_stick;
	int touch_bindings[INPUT_TOUCH_N]; // -2 inherits needs_* shorthand; -1 hides.
	// Add future renderer/frameskip/clock parameters with explicit inheritance.
} game_preset_options_t;
typedef struct {
	const char *serial;
	game_preset_options_t old3ds, new3ds;
} game_preset_t;
// Variadic initializer allows designated fields and comma-separated values.
#define GAME_PRESET_SAME(disc, ...) { .serial = disc, .old3ds = __VA_ARGS__, .new3ds = __VA_ARGS__ }
game_preset_options_t game_capabilities(void);
void game_presets_reset(void);
void game_presets_disc_log(const char *message);

// ui.c - text drawing on the bottom screen
void ui_init(void);
void ui_exit(void);
void ui_text(float x, float y, float size, u32 color, const char *fmt, ...);
void ui_rect(float x, float y, float w, float h, u32 color);
void ui_begin(void);
bool ui_confirm(const char *title, const char *message, bool show_game);
void ui_text_ex(C2D_Font font, float x, float y, float size, u32 color, u32 flags, const char *s);
void ui_text_pixels(C2D_Font font, float x, float baseline, float pixels, float spacing,
	float width, u32 color, u32 align, const char *s);
bool ui_input_symbol(int id, float x, float y, float size, u32 color);
u32 ui_input_color(int id);
void ui_menu_header(const char *title, const char *right);
void ui_button_text(float x, float y, float pixels, float spacing, u32 color, int id);

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
void flist_draw_sections(const flist_t *f, int n, int sel, float x, float y0, float w, float row_h, int rows,
	const char *(*label)(int, void *), const char *(*value)(int, void *), void *ud, u32 accent,
	bool (*header)(int, void *));
int flist_row_at(const flist_t *f, float y0, float row_h, float py);
void flist_snap(flist_t *f, int sel, int n, int rows);
void ui_help_panel(float y0, const char *title, const char *impact, const char *text);
void ui_help_for(float y0, const char *name);

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
int library_series_count(void);
int library_game_series(int game);          // -1 if not in a series
const char *library_series_name(int s);
int library_series_size(int s);
int library_series_game(int s, int k);      // k-th game of series s, in order
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
void music_unload(void);
void covers_release_gpu(void);
void video_gpu_idle(void);
void music_reload(void);
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
extern float sw_slider;
extern bool sw_showright, sw_sbs;
float slider_get(void);
extern bool sw_csnd, sw_nonet, sw_nosound, sw_noirrst, sw_noaa, sw_aa, sw_nolcd, sw_nocovers;
extern bool g_new3ds;
extern int g_syscore_pct;

#endif
