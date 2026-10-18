// Exercise the actual control state machine and run-switch validation without a
// console: lifecycle, renderer changes, warmup, averages, stop-on-failure, paths.
#define PSX3D_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
typedef unsigned long long u64;
#define PATH_LEN 512
#define PSX3D_DIR "sdmc:/3ds/PSX3D"
typedef struct {
	char title[256], cover_key[256];
} game_t;
static bool g_new3ds;
bool sw_prof, sw_stats, sw_fs1, sw_fs3, sw_noskip, sw_nosputhread, sw_acap;
bool sw_nogputhread, sw_aa, sw_showright, sw_sbs, sw_gpu_async, sw_gpu_flush;
float sw_slider;
int sw_state = -1, sw_renderer = -1, sw_pmu0 = -1, sw_pmu1 = -1, sw_gldbg, sw_vramdump;
char sw_opt[8][48], sw_cdra[8], sw_clock[8];
static u64 mock_ms;
static char wire[16384], response[512], status[4096];
static int state_writes, init_calls, shot_calls;
int pica_debug;
static bool state_ok = true;
static game_t games[] = {{"Crash Bandicoot", "Crash Bandicoot (USA)"}, {"Crash Team Racing", "CTR"}};
u64 osGetTime(void) { return mock_ms; }
u64 svcGetSystemTick(void) { return 12345; }
bool envIsHomebrew(void) { return false; }
void aptSetChainloaderToSelf(void) {}
void log_printf(const char *fmt, ...) { (void)fmt; }
void options_mark_updated(void) {}
const char *options_get(const char *key)
{
	(void)key;
	return "disabled";
}
bool renderer_is_gpu(void) { return sw_renderer != 1; }
unsigned statsview_emulated(void) { return 60; }
int library_count(void) { return 2; }
const game_t *library_get(int i) { return &games[i]; }
bool library_launch_path(const game_t *g, char *out, size_t n)
{
	snprintf(out, n, "sdmc:/roms/%s.cue", g->title);
	return true;
}
void debug_parse_switch(const char *word)
{
	if (!strncmp(word, "state=", 6))
		sw_state = atoi(word + 6);
	if (!strcmp(word, "renderer=sw"))
		sw_renderer = 1;
	if (!strcmp(word, "renderer=gpu"))
		sw_renderer = 0;
}
#include "../../source/devcontrol.c"
#include "../../source/devswitch.c"
const char *const dev_columns[DEV_METRICS] = {
    "fps",       "speed_pct",     "emu_ms",  "present_ms", "wait_ms",   "gpu_ms",  "mcycles",
    "ev0_k",     "ev1_k",         "skipped", "late",       "batches",   "prims",   "decodes",
    "evictions", "invalidations", "uploads", "syncs",      "readbacks", "pic_gpu", "pic_none",
    "lum",       "top_lum",       "xfer_ms", "heap_ms",    "queue_ms",  "gap_ms"};
void devnet_init(void) { init_calls++; }
void devnet_exit(void) {}
bool devnet_take(char *out)
{
	if (!wire[0])
		return false;
	strcpy(out, wire);
	wire[0] = 0;
	return true;
}
void devnet_reply(const char *json) { snprintf(response, sizeof(response), "%s", json); }
void devnet_status(const char *json) { snprintf(status, sizeof(status), "%s", json); }
void devnet_sample(const dev_measurement *s, bool reset)
{
	(void)s;
	(void)reset;
}
bool devnet_shot(const char *path, bool bottom)
{
	(void)path;
	(void)bottom;
	shot_calls++;
	return true;
}
void devnet_start_update(const char *args) { (void)args; }
void devnet_update_reply(const char *args) { (void)args; }
void devnet_notification_done(void) {}
bool emu_debug_state(int slot, bool save)
{
	assert(slot >= 0 && slot < 10);
	state_writes += save;
	return state_ok;
}
static void command(const char *s, bool playing)
{
	strcpy(wire, s);
	response[0] = 0;
	dev_tick(playing);
}
static void sample(unsigned ms, float fps)
{
	mock_ms = ms;
	statsview_sample s = {
	    .fps = fps, .target = 60, .emu = 10, .elapsed = 1000, .renderer = "", .audio = "audio off"};
	dev_sample(&s, "pic gpu 1 none 0 cpu 0; lum 10", 10, 5, 1, 2);
}
int main(void)
{
	mkdir("sdmc:", 0777);
	mkdir("sdmc:/3ds", 0777);
	mkdir(PSX3D_DIR, 0777);
	assert(!dev_tick(false));
	dev_init();
	assert(!init_calls); // disabled is inert
	dev_remote = true;
	dev_init();
	assert(init_calls == 1);
	char path[PATH_LEN], save[PATH_LEN], title[PATH_LEN], err[160], game[PATH_LEN];
	char bad[] = "game=Crash state=oops";
	assert(!switches_run(bad, game, sizeof(game), err, sizeof(err)));
	command("batch-id test", false);
	command("bench game=Crash_Bandicoot state=2 renderer=gpu warmup=1 seconds=2 shot=0;game=Crash_Bandicoot "
	        "state=2 renderer=sw warmup=0 seconds=1 shot=0",
	        false);
	assert(batch_active && strstr(response, "started"));
	assert(dev_next_game(path, save, title) && sw_renderer == 0 && sw_state == 2);
	assert(!strcmp(save, "Crash Bandicoot (USA)"));
	dev_game_started(title, true);
	sample(1000, 1);
	assert(!samples); // warm-up boundary discarded
	sample(2000, 50);
	sample(3000, 60);
	assert(run_index == 1 && load_pending && batch_active);
	dev_game_ended(true);
	assert(dev_next_game(path, save, title) && sw_renderer == 1);
	dev_game_started(title, true);
	sample(4000, 1);
	sample(5000, 30);
	assert(!batch_active && run_index == 2 && !shot_calls);
	dev_game_ended(true);
	FILE *f = fopen(PSX3D_DIR "/bench/results.tsv", "r");
	assert(f);
	char buf[8192];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = 0;
	fclose(f);
	assert(strstr(buf, "test\t1\tok\t2\t0\t55.000000"));
	assert(strstr(buf, "test\t2\tok\t1\t0\t30.000000"));
	command("load Missing_Game state=2", false);
	assert(!dev_next_game(path, save, title));
	assert(strstr(response, "false"));
	command("load sdmc:/roms/My Game/my_file.cue state=2", false);
	assert(dev_next_game(path, save, title));
	assert(!strcmp(path, "sdmc:/roms/My Game/my_file.cue"));
	dev_game_started(title, true);
	assert(strstr(response, "true"));
	command("set renderer=gpu", true);
	assert(strstr(response, "false"));
	command("savestate 9oops", true);
	assert(!state_writes);
	command("savestate 9", true);
	assert(state_writes == 1);
	command("bench game=Crash_Bandicoot state=2 renderer=gpu warmup=0 seconds=1", true);
	assert(dev_next_game(path, save, title));
	dev_game_started(title, false);
	dev_game_ended(true);
	assert(!batch_active && strstr(last_error, "state"));
	command("quit", false);
	dev_next_game(path, save, title);
	assert(!dev_transition());
	command("bench game=Crash_Team_Racing warmup=0 seconds=1 shot=0", false);
	assert(dev_next_game(path, save, title));
	dev_game_started(title, true);
	command("quit", true);
	dev_game_ended(true);
	assert(!batch_active);
	command("bench game=Crash_Team_Racing warmup=0 seconds=1 shot=0", false);
	assert(dev_next_game(path, save, title));
	dev_game_started(title, true);
	dev_interrupt();
	assert(dev_tick(true));
	assert(!batch_active);
	dev_game_ended(true);
	dev_shutdown();
	puts("developer lifecycle, batch averaging, state validation and safe failures: PASS");
}
