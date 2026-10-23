// PSX3D - standalone PS1 emulator frontend for Nintendo 3DS.
// Hosts the PCSX-ReARMed libretro core (GPL v2) without RetroArch.
#ifndef PSX3D_H
#define PSX3D_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>
#include "libretro.h"
#include "safefile.h"
#include "history.h"
#include "theme.h"

#define PSX3D_VERSION  "0.5.1"
// Optional private test identity; the release reports its human version.
#ifndef PSX3D_BUILD
#define PSX3D_BUILD "release-v051-rc4"
#endif
#define PSX3D_DIR      "sdmc:/3ds/PSX3D"
#define PSX3D_BIOS_DIR PSX3D_DIR "/bios"
#define PSX3D_SAVE_DIR PSX3D_DIR "/saves"
#define PSX3D_STATE_DIR PSX3D_DIR "/states"
#define PSX3D_CONFIG   PSX3D_DIR "/config.ini"
#define PSX3D_LOG      PSX3D_DIR "/log.txt"

#define PATH_LEN 512

// Code that runs only for saves, menus and messages: linked after all other
// frontend code (core-first.ld), so it never moves the per-frame code.
#define PSX3D_COLD __attribute__((noinline, section(".psx3d_cold.text")))

// log.c
void log_init(void);
bool log_linked(void);
void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_close(void);
void log_resume(void);
void log_hold(bool hold);

// config.c - frontend settings plus core option overrides
typedef struct {
	char last_dir[PATH_LEN];
	char games_dir[PATH_LEN];   // Y in the browser jumps here
	char last_title[256];       // library: game to select on start
	int  smooth;       // bilinear filtering
	int  theme;        // menu colours (theme.c), index into the theme table
	int  show_fps;
	int  stereo;       // 3D slider: 0 off, 1 screen depth, 2 real (per-object)
	int  debug;        // show debug tools in the menu
	int  stretch;      // stretch the picture to 400x240
	int  sounds;       // interface sounds
	int  music;        // library music
	int  bottom_off;   // backlight off on the bottom screen while playing
	int  audio_stretch; // maximum slowdown in percent (forced on, defaults.ini)
	int  touch_stick;  // Old 3DS virtual right stick
	int  touch_click_left; // global New 3DS bottom-screen layout: 0 right, 1 left
	int  controller;   // all-games Controller: 0 digital, 1 analog (DualShock at start)
	int  library_view; // library Sort: 0 A-Z, 1 recently played, 2 most played, 3 favourites
	// main menu Continue: the last game played here (home.c)
	char last_game[256];        // its title ("" = name from the path)
	char last_game_save[256];   // save name it was started with ("" = from the path)
	char last_game_state[256];  // save name it ended up with (states/<this>.sta)
	char last_game_path[PATH_LEN];
} psx3d_config;

extern psx3d_config g_cfg;
void config_user_changed(const char *key);
void config_set_user_core_override(const char *key, const char *value);
int input_hotkey_factory(int slot);
void config_load(void);
void config_save(void);
void config_reset_defaults(void);
bool config_game_reset(void);
bool config_game_clear(const char *key);
bool config_game_overridden(const char *key);
int config_controller(void);     // effective Controller for this game (1 = analog)
bool config_rewind_on(void);     // Rewind is on for the running game
void config_game_reset_advanced(const char *const *keys, int n);
bool config_control_overridden(int row);
const char *input_control_key(int row);
void config_control_clear(int row);
bool settings_ui_run(bool game, void (*draw_top)(void *));
bool settings_ui_cheats(void);
bool settings_ui_take_folders(void);   // Settings > Games folder: open the folder browser
bool settings_ui_active(void);
bool settings_ui_debug_capture(void);
bool settings_ui_debug_key(const char *name);
bool input_remote_key(const char *name);
void config_controls_save(void);
void config_controls_defaults(void);
const char *config_core_override(const char *key);
bool config_widescreen(void);
int config_screen_mode(void);
void config_set_screen_mode(int mode);
void config_game_begin(const char *game);
void config_game_set_screen(int mode);
void config_game_end(void);
void config_game_store(void);
bool config_in_game(void);
bool config_ws_fix(void);
void config_ws_sync(void);
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
int options_index(const char *key);
const char *options_game_key(const char *key);
const char *options_desc(int i);
const char *options_value(int i);
const char *options_cycle(int i, int dir);

// video.c
bool video_init(void);
#define VIDEO_CMDBUF_BYTES 0x100000   // GPU command list bytes reserved by C3D_Init (c3d_abi.h checks it)
#define VIDEO_CMD_ROOM 4096          // command words free before any draw (cmdroom.c)
extern bool c3d_abi_failed;   // renderqueue.c, set when the citro3d private ABI does not match
bool video_game_begin(void);
void video_game_end(void);
bool renderer_is_gpu(void);   // GPU game buffers active; false in the library
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
	double picture_mean, picture_square;
	unsigned picture_intervals;
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
extern bool sw_ff;
extern bool sw_prof, sw_stats, sw_o3ds, sw_fs1, sw_fs3, sw_noskip, sw_nosputhread, sw_acap;
extern int sw_state, sw_gldbg, sw_vramdump, sw_autosave_test;
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
extern bool sw_pmuclass;
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
int rewind_step(void);   // 1 done, 0 used up, -1 failed (see rewind.c)
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
void cheats_disc_changed(void);
extern bool sw_wsbundle;
extern int sw_wsscreen;
const char *cheats_add_dialog(void);
bool cheats_forced(int i);

// dspdump.c
bool dsp_firmware_ensure(void);

// cards.c - memory cards on the SD card, crash-safe
void cards_begin(const char *srm_path);   // a game starts: read its cards
bool cards_check(void);                   // write changed cards; true if one was
void cards_save_now(void);                // leaving, sleep, HOME, pause menu
void cards_end(void);
// A card that could not be loaded when the game started (the game then runs
// on an empty stand-in): emu.c asks the player before the game runs.
typedef enum { CARD_TROUBLE_NONE, CARD_TROUBLE_MEMORY, CARD_TROUBLE_DAMAGED } card_trouble_t;
card_trouble_t cards_trouble(char *label, size_t len);   // the first one, its name
bool cards_retry(void);                   // read them again; true: all loaded now
void cards_accept(void);                  // play on (a damaged file stays as .bad)
int card_saves(const void *card, size_t size);   // saves on a PS1 card image
// save backups of the cards (history.h): which group/ext a card's copies use
bool cards_backup_group(int i, char *group, size_t glen, char *ext, size_t elen, const char **label);
bool cards_put_back(const hist_entry_t *copy);   // then restart the game
int cards_saves_now(int i);
void emu_osd(const char *text);           // emu.c: short message in the game
void emu_osd_alert(const char *text);     // ... about the player's data: unmissable

// saveid.c - which game a save name belongs to (saves/<name>.id)
// Before loading path: the save name to start with. base: the library's name
// for the game (NULL from the folder browser).
void saveid_begin(const char *path, const char *base, char *name, size_t len);
// After loading, before any save is read or written: the game's own save name,
// given the serial the core read (CdromId); records it. true when it differs
// from saveid_begin's. *created: a new name was just made for this game.
bool saveid_confirm(const char *serial, char *name, size_t len, bool *created);
// saveid_begin: the name is another game's (its settings must not apply
// before saveid_confirm); saveid_confirm: the owner record could not be written
extern bool saveid_provisional, saveid_unrecorded;

// savestates.c - save state browser with screenshots
#define STATE_AUTO -1
safe_format_t emu_state_format(void);   // emu.c: what a good state file is
bool state_thumb_write(const char *prefix, int slot);
u16 *state_thumb_take(void);
bool state_thumb_save(const char *prefix, int slot, const u16 *px);
void state_thumb_restore(const char *prefix, int slot);
bool state_thumb_save_file(const char *thm_path, const u16 *px);
// "Continue?" at game start
enum { RESUME_NONE, RESUME_START, RESUME_CONTINUE, RESUME_OLDER };
int states_resume_prompt(const char *prefix);
// The state browser. Loading has pages: save slots, the save backups' ring
// of automatic saves, memory card copies (L/R). Returns the page of the
// pick (STATES_PAGE_SLOTS: the slot is *slot, else pick->entry) or -1.
// pick->page on entry: the page to start on.
enum { STATES_PAGE_SLOTS, STATES_PAGE_RING, STATES_PAGE_CARDS };
typedef struct { int page; hist_entry_t entry; } states_pick_t;
int states_run(bool saving, const char *prefix, const char *title, int *slot, states_pick_t *pick);
// a slot deleted in the browser went into the ring as this entry
typedef struct { bool done; int slot; hist_entry_t entry; } states_deleted_t;
extern states_deleted_t states_deleted;
bool states_auto_peek(const char *prefix, u32 *saved, C3D_Tex *tex, bool *tex_ok);
void states_thumb_draw(C3D_Tex *t, float x, float y, float w, float h, float z);
void states_when_text(char *out, size_t len, u32 saved);

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
void input_controller_apply(bool changed);
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

// presets.c: curated per-game data files (romfs:/presets/*.ini, docs/presets.md),
// resolved per console. Fields use -1 to inherit the uncurated console defaults.
// needs_l3r3/needs_right_stick: shown only while the game uses analog mode.
#define NEEDS_ANALOG 2
typedef struct {
	int fast_forward, rewind, touch_controls;
	int needs_l2r2, needs_l3r3, needs_right_stick;
	int touch_bindings[INPUT_TOUCH_N]; // -2 inherits needs_* shorthand; -1 hides.
	int frameskip; // -1: console default Auto, 0: Off, 1: Auto.
	int widescreen; // WS_* policy (widescreen.h); default offered when a fix exists.
	// PS1 CPU clock in % (pcsx_rearmed_psxclock), 0 inherits. Applied only with
	// PSX3D_PS1_OVERCLOCK_PRESETS; never over a core database override.
	int psxclock;
	int auto_floor; // pictures/s; absent = 15
	// Technical settings in preset_schema.def order (word index, -1 unset).
	int controller, alt_flip, gpu_slow_llists, cd_turbo, nostalls, nolddelay;
	int gteregsunneeded, nogteflags, nosmccheck, icache_emulation, exception_emulation, nocompathacks;
	int tested_with; // defaults_version these values were tested with; -1 untested
} game_preset_options_t;
typedef struct {
	const char *serial;
	game_preset_options_t old3ds, new3ds;
	bool curated; // the preset owns the game's option list (frameskip hidden)
} game_preset_t;
// Variadic initializer allows designated fields and comma-separated values.
#define GAME_PRESET_SAME(disc, ...) { .serial = disc, .old3ds = __VA_ARGS__, .new3ds = __VA_ARGS__, .curated = true }
void game_presets_init(const char *builtin_dir, const char *sd_dir);
bool game_presets_validate(const char *builtin_dir, const char *dir, int version, unsigned files);
void game_presets_moved(const char *sd_dir);
int game_presets_pack(void);
int game_preset_version(void);
const char *game_preset_source(void);
game_preset_options_t game_capabilities(void);
bool game_preset_curated(void); // legacy metadata only; never gates options
bool game_preset_sets(const char *key);
const char *game_preset_evidence(const char *key);
int game_auto_floor(void);
bool game_preset_tested(void);   // this console's values were tested (tested_with)
// The preset's value for a technical core option, as a core value; NULL when unset.
const char *game_preset_core_value(const char *core_key);
// defaults.ini (docs/settings.md section 6): this console's default and forced
// values, in the preset vocabulary. defaults_core() maps them to core values.
void defaults_init(const char *path);
int defaults_version(void);
const char *defaults_get(const char *key);
const char *defaults_forced(const char *key);
const char *defaults_core(const char *core_key, bool *forced);
int defaults_word(const char *key, const char *words, int fallback);
void game_presets_reset(void);
void game_presets_invalidate(void);
void game_presets_disc_log(const char *message);
#ifdef PSX3D_PS1_OVERCLOCK_PRESETS
void game_presets_core_log(const char *message);
#endif

// ui.c - text drawing on the bottom screen
void ui_init(void);
void ui_exit(void);
void ui_text(float x, float y, float size, u32 color, const char *fmt, ...);
void ui_rect(float x, float y, float w, float h, u32 color);
void ui_begin(void);
bool ui_confirm(const char *title, const char *message, bool show_game);
const char *ui_fit(const char *s, float size, float width);
bool ui_confirm_action(const char *title, const char *message, const char *action, bool show_game);
bool ui_confirm_choice(const char *title, const char *message, const char *action, const char *back, bool show_game);
void ui_text_ex(C2D_Font font, float x, float y, float size, u32 color, u32 flags, const char *s);
void ui_text_pixels(C2D_Font font, float x, float baseline, float pixels, float spacing,
	float width, u32 color, u32 align, const char *s);
bool ui_input_symbol(int id, float x, float y, float size, u32 color);
u32 ui_input_color(int id);
void ui_menu_header(const char *title, const char *right);
u32 ui_header_back(u32 keys);   // header tap -> KEY_B
// touch button bar replacing the prompt lines (ui.c): B leftmost, A rightmost
// each tab shows an icon over a one-word caption; no icon: B back, A check
enum { UI_ICON_NONE, UI_ICON_STAR, UI_ICON_SORT, UI_ICON_RESCAN, UI_ICON_LEFT, UI_ICON_RIGHT, UI_ICON_FOLDER,
	UI_ICON_BACK, UI_ICON_PLAY, UI_ICON_CHECK, UI_ICON_CROSS, UI_ICON_PLUS, UI_ICON_TRASH, UI_ICON_SAVE,
	UI_ICON_LOAD, UI_ICON_RESET, UI_ICON_UP, UI_ICON_OPEN, UI_ICON_CHANGE, UI_ICON_TOGGLE, UI_ICON_DISC, UI_ICON_EXIT };
typedef struct { u32 key; const char *label; u8 icon; bool on; } ui_button_t;
#define UI_BAR_MAX 6
void ui_bar_draw(const ui_button_t *b, int n);           // bottom screen, y 208..240
u32 ui_bar_input(const ui_button_t *b, int n, u32 keys);  // tap -> its key; presses light it
int ui_bar_parse(const char *hint, ui_button_t *out, int max);   // "A open    B back"
enum { UI_COL_BAR, UI_COL_RULE, UI_COL_NOTICE, UI_COL_STAR };
u32 ui_bar_color(int which);
void ui_star(float cx, float cy, float r, u32 c, u32 hollow, float z);
void ui_notice(const char *s, int frames);
void ui_first_letter(char *out, const char *title);   // out[8]: one whole UTF-8 letter   // top screen, fades over its last 20 frames
void ui_text_wrap(float x, float y, float size, u32 color, float width, const char *s);
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
const char *emu_game_title(void);
void ui_help_panel(float y0, const char *title, const char *impact, const char *text);

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
bool library_guess_dir(void);
bool library_is_ps1_image(const char *path);   // cheap check: not another system's disc
bool library_missing_file(const char *path, char *out, size_t len);   // a disc or track that isn't there
// emu.c: why the last game didn't start, and what to do (a title line, then the advice)
const char *emu_load_error(void);   // browser.c: first run, find a folder with PS1 games
int library_count(void);
const game_t *library_get(int i);
int library_series_count(void);
int library_game_series(int game);          // -1 if not in a series
const char *library_series_name(int s);
int library_series_size(int s);
int library_series_game(int s, int k);      // k-th game of series s, in order
int library_omitted(void);                  // files and folders the last scan left out
// NULL if the disc list of a multi-disc game could not be written
const char *library_launch_path(const game_t *g, char *buf, size_t len);

// covers.c - box art
void covers_init(void);
void covers_exit(void);
// The cover's texture, or NULL while it loads. *sub is the part of it that
// holds the picture (its own aspect), in citro2d's flipped convention.
C3D_Tex *covers_get(int game, Tex3DS_SubTexture *sub);
u32 covers_frame(void);
// a cover's stand-in: a disc drawn in code with the title on it (placeholder.c)
void cover_placeholder(float cx, float cy, float size, float dim, float z, const char *title, u32 accent);
void covers_reset(void);
void covers_focus(int game);
void covers_pump(void);
void covers_stats_reset(void);   // developer diagnostics
int covers_test_tick(void);
extern bool covers_slow;
extern unsigned covers_sd_ms;
void covers_note_select(void);
void covers_note_draw(bool selected, bool near);
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
// Settings > About: version and a QR code to share PSX3D (share.c)
void share_about_run(bool game);
// mp_ui.c - Play together and Share PSX3D screens, opened from the main menu
void mp_hub_run(void);
void mp_host_run(void);   // main menu > Play together > Host (devtools/ui-impl/INTERFACES.md)
void mp_join_run(void);   // main menu > Play together > Join
// after mp_hub_run: a game to start (Host a game), path filled
bool mp_launch_request(char *path, size_t len);
void share_run(void);
bool share_about_visible(void);
// Returns 1 to play (path filled), 2 folder browser, 3 remote, 4 back to the
// main menu, 0 quit.
int libui_run(char *out_path, size_t len);
// the library's top-screen backdrop: sky picture and rolling wire grid
void libui_draw_backdrop(void);
// playlog.c - play history and favourites, keyed by save name
u32 playlog_last(const char *name);      // unix time, 0 = never played
u32 playlog_seconds(const char *name);
bool playlog_favourite(const char *name);
bool playlog_toggle_favourite(const char *name);   // returns the new state
void playlog_begin(const char *name);    // a local game starts
void playlog_pause(bool paused);         // pause menu, HOME Menu, sleep
void playlog_end(void);                  // the game ends: time added, file written
void libui_select_title(const char *title);
const game_t *libui_picked(void);

// home.c - the main menu PSX3D opens to
typedef struct {
	char save[256];    // save name for emu_run_game ("" = NULL)
	char title[256];   // title for emu_run_game ("" = NULL)
	bool resume;       // load the Continue auto-save without asking
} home_launch_t;
enum { HOME_QUIT, HOME_AGAIN, HOME_LIBRARY, HOME_PLAY, HOME_FOLDERS };
int home_run(bool have_games, char *path, size_t len, home_launch_t *out);
// Library with no games: 0 quit, 1 main menu, 2 folder browser, 3 look again
int home_empty_library_run(void);
void home_note_game(const char *path, const char *save_name, const char *title, const char *state_name);

// browser.c - returns true and fills path when a game is chosen
bool browser_run(char *out_path, size_t out_len);

// emu.c
bool emu_init(void);
void emu_deinit(void);
bool emu_run_game(const char *path, const char *save_name, const char *title);
extern bool emu_continue_now;   // next game: load its Continue auto-save, no prompt

extern int __ctr_svchax;
// Why fast mode (the dynarec) is off; SLOW_NONE when it is on.
enum { SLOW_NONE, SLOW_EMULATOR, SLOW_NO_LUMA };
extern int g_slow_reason;
// Plain-language notice for the library/browser/game start, or NULL when fast mode is on.
const char *slow_mode_notice(bool shortform);
extern float sw_slider;
extern int sw_stereo;
float slider_get(void);
extern bool sw_csnd, sw_nonet, sw_nosound, sw_noirrst, sw_noaa, sw_aa, sw_nolcd, sw_nocovers;
extern bool g_new3ds;
extern int g_syscore_pct;

#endif
