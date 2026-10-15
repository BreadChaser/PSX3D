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
	int  scale_mode;   // 0 = 4:3, 1 = stretch to 400x240, 2 = integer/pixel 1:1
	int  smooth;       // bilinear filtering
	int  show_fps;
} psx3d_config;

extern psx3d_config g_cfg;
void config_load(void);
void config_save(void);
const char *config_core_override(const char *key);
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
void video_set_fps(double fps);
void video_present(bool show_game, void (*draw_top)(void *),
	void (*draw_bottom)(void *), void *ud);
C3D_RenderTarget *video_bottom_target(void);

// audio.c
bool audio_init(double sample_rate);
void audio_exit(void);
size_t audio_write(const int16_t *data, size_t frames);
void audio_pause(bool paused);

// input.c
void input_poll(void);
int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id);
u32 input_keys_down(void);
u32 input_keys_held(void);

// ui.c - text drawing on the bottom screen
void ui_init(void);
void ui_exit(void);
void ui_text(float x, float y, float size, u32 color, const char *fmt, ...);
void ui_rect(float x, float y, float w, float h, u32 color);
void ui_begin(void);

// browser.c - returns true and fills path when a game is chosen
bool browser_run(char *out_path, size_t out_len);

// emu.c
bool emu_init(void);
void emu_deinit(void);
bool emu_run_game(const char *path);

extern int __ctr_svchax;

#endif
