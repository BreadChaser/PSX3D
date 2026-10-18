#ifndef PSX3D_DEVTOOLS_H
#define PSX3D_DEVTOOLS_H
#include "statsview.h"
/* Explicit switches.txt opt-in only. No sockets/threads/probes when disabled. */
extern bool dev_remote, dev_bench, dev_bench_home;
extern char dev_token[96], dev_update_url[256];
void dev_authorize(void);
bool dev_enabled(void);
bool dev_batch_active(void);
void dev_interrupt(void);
void dev_init(void);
void dev_shutdown(void);
bool dev_tick(bool playing); /* main thread, after pica_thread_sync */
bool dev_transition(void);
bool dev_exiting(void);
bool dev_next_game(char *path, char *save, char *title);
void dev_game_started(const char *title, bool state_ok);
void dev_game_ended(bool ok);
void dev_sample(const statsview_sample *s, const char *probe, int top_lum, double cycles, double ev0,
                double ev1);
void dev_reply(const char *json);
/* Main thread switch parser, shared by bench and remote load. */
bool switches_run(char *line, char *game, size_t len, char *error, size_t error_len);
bool switches_set(const char *word);
bool emu_debug_state(int slot, bool save);
bool pmu_sample(double *cycles, double *ev0, double *ev1);
/* Network worker only; no emulation/GPU calls. */
bool dev_update(const char *args, char *result, size_t capacity);
#endif
