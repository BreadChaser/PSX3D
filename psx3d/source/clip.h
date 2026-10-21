/* Developer-only portable gameplay clips. Files are little endian, versioned,
 * bounded, and checked before the core sees a save state. */
#ifndef PSX3D_CLIP_H
#define PSX3D_CLIP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "libretro.h"
#define CLIP_META 12288
#define CLIP_MAX_FRAMES 3600
#define CLIP_CHECK_INTERVAL 60
#define CLIP_BUILD "release-v050-pre2"
extern int clip_active; /* one predictable branch per retro_run; zero normally */
extern bool clip_enabled;
extern int clip_seconds, clip_requested;
extern bool clip_replay_session;
typedef struct {
    unsigned frame, frames, first_divergent, checks;
    bool replay, done, failed;
    char message[160];
} clip_result;
extern clip_result clip_status;
bool clip_switch(const char *word);
void clip_reset_request(void);
bool clip_record(const char *base, unsigned frames, unsigned fps_milli,
                 const char *serial, const char *settings);
bool clip_open(const char *base, const char *serial);
bool clip_restore(void);
void clip_run(void);
void clip_close(void);
void clip_set_live_input(retro_input_poll_t poll, retro_input_state_t input);
const char *clip_option(const char *key);
const char *clip_metadata(void);
bool clip_presentation_option(const char *key);
bool clip_archive_name(const char *name); /* recordings/<safe-title>/<n>.state|inp */
#endif
