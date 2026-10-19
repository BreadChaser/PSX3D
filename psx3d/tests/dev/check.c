// Exercise the actual control state machine and run-switch validation without a
// console: lifecycle, renderer changes, warmup, averages, stop-on-failure, paths.
#define PSX3D_H
#define PSX3D_VERSION "test"
#define PICA_LAYOUT_NAME "split"
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
static bool ui_active;
bool settings_ui_key(const char *name) { return ui_active && !strcmp(name,"down"); }
int sw_rtfscap, sw_rtfslevel;
bool sw_rtfsmodel, sw_rtfswindow, sw_rtfs, sw_ff, sw_floataudio, sw_frontend;
typedef struct { unsigned emulated, elapsed_ms, presented, held, top, bottom; float run_ms, present_ms, audio_ms, misc_ms, pace_ms; float spu_ms, spu_wait_ms, pica_ms; float phase_ms[8], draw_fraction; unsigned skip_level, drawn, skipped; } emu_frontend_sample;
const emu_frontend_sample *emu_frontend_stats(void) { static emu_frontend_sample f; return &f; }
bool sw_prof, sw_stats, sw_fs1, sw_fs3, sw_noskip, sw_nosputhread, sw_acap;
bool sw_nogputhread, sw_aa, sw_showright, sw_sbs, sw_gpu_async, sw_gpu_flush;
float sw_slider;
int sw_state = -1, sw_pmu0 = -1, sw_pmu1 = -1, sw_gldbg, sw_vramdump;
char sw_opt[8][48], sw_cdra[8], sw_clock[8];
static u64 mock_ms;
static char wire[16384], response[512], status[4096];
static int state_writes, init_calls, shot_calls;
int pica_debug;
bool sw_hack_spunearest;
unsigned pica_submit;
unsigned pica_prims;
bool pica_texmask = true, pica_texmemo = true;
int psx3d_idle_skip=1, psx3d_idle_pointer, psx3d_idle_delay;
bool sw_pmuphase, sw_pmuclass, sw_rtfsdense;
void pmu_category_detail(double values[21]) { for (int k=0;k<21;k++) values[k]=-1; }
void pmu_renderer(double values[4]) { for (int k=0;k<4;k++) values[k]=-1; }
int psx3d_skipgate = 1;
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
void pmu_categories(double cycles[3], unsigned counts[3]) { for (int k=0;k<3;k++) { cycles[k]=-1; counts[k]=0; } }
bool renderer_is_gpu(void) { return true; }
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
	if (!strcmp(word, "rtt=old")) sw_gldbg = (sw_gldbg | 262144) & ~1048576;
	if (!strcmp(word, "rtt=new")) sw_gldbg = (sw_gldbg & ~262144) | 1048576;
	if (!strcmp(word, "rtt=auto")) sw_gldbg &= ~(262144 | 1048576);
	if (!strcmp(word, "rttcache=old")) sw_gldbg |= 524288;
	if (!strcmp(word, "rttcache=new")) sw_gldbg &= ~524288;
	if (!strcmp(word, "idle=off")) psx3d_idle_skip = 0;
	if (!strcmp(word, "idle=on")) psx3d_idle_skip = 1;
	if (!strcmp(word, "idleptr=off")) psx3d_idle_pointer = 0;
	if (!strcmp(word, "idleptr=on")) psx3d_idle_pointer = 1;
	if (!strcmp(word, "idledelay=off")) psx3d_idle_delay = 0;
	if (!strcmp(word, "idledelay=on")) psx3d_idle_delay = 1;
	if (!strcmp(word, "pica_prims_old")) pica_prims = 0;
	if (!strcmp(word, "pica_prims_fast")) pica_prims = 7;
	if (!strcmp(word, "pica_prims_copy")) pica_prims |= 1;
	if (!strcmp(word, "pica_prims_clip")) pica_prims |= 2;
	if (!strcmp(word, "pica_prims_round")) pica_prims |= 4;
}
void covers_stop_wait(void) {}
void covers_resume(void) {}
bool covers_refresh(int game) { return game >= 0; }
void covers_debug(char *out, size_t len) { snprintf(out, len, "{\"ok\":true}"); }
void libui_select_title(const char *title) { (void)title; }
#include "../../source/clip.h"
bool emu_record_start(void) { return false; }
#include "../../source/devcontrol.c"
#include "../../source/devswitch.c"
const char *const dev_columns[DEV_METRICS] = {
    "fps",       "speed_pct",     "emu_ms",  "present_ms", "wait_ms",   "gpu_ms",  "mcycles",
    "ev0_k",     "ev1_k",         "skipped", "late",       "batches",   "prims",   "decodes",
    "evictions", "invalidations", "uploads", "syncs",      "readbacks", "pic_gpu", "pic_none",
    "lum",       "top_lum",       "xfer_ms", "heap_ms",    "queue_ms",  "gap_ms", "front_run_ms", "front_present_ms", "audio_ms", "loop_ms", "pace_ms", "top_count", "bottom_count", "held_count", "ff", "spu_ms", "spu_wait_ms", "skip_level", "drawn_count", "skipped_count", "audio_underruns", "audio_drops", "audio_gap_ms", "pica_cpu_ms", "phase_list_ms", "phase_prim_ms", "phase_tex_ms", "phase_decode_ms", "phase_state_ms", "phase_submit_ms", "phase_read_ms", "phase_vertex_ms", "draw_mcycles", "skip_mcycles", "idle_mcycles", "draw_vblanks", "skip_vblanks", "idle_vblanks", "renderer_mcycles", "renderer_ev0_k", "renderer_ev1_k", "renderer_samples", "draw_ev0_k", "draw_ev1_k", "draw_spu_ms", "draw_spuwait_ms", "draw_pica_ms", "draw_audio_ms", "draw_dma_ms", "skip_ev0_k", "skip_ev1_k", "skip_spu_ms", "skip_spuwait_ms", "skip_pica_ms", "skip_audio_ms", "skip_dma_ms", "idle_ev0_k", "idle_ev1_k", "idle_spu_ms", "idle_spuwait_ms", "idle_pica_ms", "idle_audio_ms", "idle_dma_ms", "draw_fraction"};
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
static dev_measurement captured;
void devnet_sample(const dev_measurement *s, bool reset)
{
	if (s) captured = *s;
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
	strcpy(dev_update_url, "http://192.168.1.225/x");
	dev_remote = true;
	dev_init();
	assert(!init_calls && !dev_remote && !dev_update_url[0]);
	strcpy(dev_token, "test-token");
	strcpy(dev_update_url, "http://192.168.1.225/x");
	dev_init();
	assert(!init_calls && !dev_update_url[0]); // token alone grants nothing
	dev_remote = true;
	dev_init();
	assert(init_calls == 1);
	statsview_sample counters = {.elapsed = 1000,
	 .renderer = "pica: ends: atlas 0; evictions by size 0 0 0 0; sync reasons cpu 0 tex 1; 12 batches 34 prims 56 tile decodes (7 evictions 8 invalidations) 9 uploads 10 syncs 11 readbacks"};
	dev_sample(&counters, "", 100, 1, 2, 3);
	assert(captured.v[11] == 12 && captured.v[13] == 56 && captured.v[17] == 10 && captured.v[18] == 11);

	char path[PATH_LEN], save[PATH_LEN], title[PATH_LEN], err[160], game[PATH_LEN];
	char bad[] = "game=Crash state=oops";
	assert(!switches_run(bad, game, sizeof(game), err, sizeof(err)));
	command("batch-id test", false);
	command("bench game=Crash_Bandicoot state=2 renderer=gpu warmup=1 seconds=2 shot=0;game=Crash_Bandicoot "
	        "state=2 renderer=sw warmup=0 seconds=1 shot=0",
	        false);
	assert(batch_active && strstr(response, "started"));
	assert(dev_next_game(path, save, title) && sw_state == 2);
	assert(!strcmp(save, "Crash Bandicoot (USA)"));
	dev_game_started(title, true);
	sample(1000, 1);
	assert(!samples); // warm-up boundary discarded
	sample(2000, 50);
	sample(3000, 60);
	assert(run_index == 1 && load_pending && batch_active);
	dev_game_ended(true);
	assert(dev_next_game(path, save, title) && renderer_is_gpu());
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
	command("set rtt=old", true);
	assert(dev_transition());
	dev_game_ended(true);
	assert(!strstr(response, "false") && command_loading);
	assert(dev_next_game(path, save, title) && (sw_gldbg & 262144) && sw_state == 2);
	dev_game_started(title, true);
	assert(strstr(response, "true") && !command_loading);
	command("set rtt=new", true);
	dev_game_ended(true);
	assert(dev_next_game(path, save, title) && !(sw_gldbg & 262144));
	dev_game_started(title, true);
	assert(switches_set("rtt=auto") && !(sw_gldbg & (262144|1048576)));
	assert(!switches_set("rtt=invalid"));
	assert(switches_set("rttcache=old") && (sw_gldbg & 524288));
	assert(switches_set("rttcache=new") && !(sw_gldbg & 524288));
	assert(!switches_set("rttcache=invalid"));
	assert(switches_set("hack_spunearest=on") && sw_hack_spunearest);
	assert(!switches_set("hack_spunearest=invalid"));
	command("set hack_spunearest=off", true);
	assert(sw_hack_spunearest && strstr(response, "false"));
	assert(!switches_set("dyn_lookup=on") && !switches_set("dyn_nclip=on") && !switches_set("g1sync=off"));
	command("set dyn_lookup=off", true);
	assert(switches_set("idleptr=on") && psx3d_idle_pointer);
	assert(switches_set("idledelay=on") && psx3d_idle_delay);
	command("set idleptr=off", true);
	assert(strstr(response, "false") && psx3d_idle_pointer);
	command("set pica_prims_fast", true);
	assert(pica_prims == 7 && strstr(response, "true"));
	command("set pica_prims_old", true);
	assert(!pica_prims);
	assert(switches_set("pica_prims_copy") && pica_prims == 1);
	assert(switches_set("pica_prims_clip") && pica_prims == 3);
	assert(switches_set("pica_prims_round") && pica_prims == 7);
	assert(!switches_set("pica_prims_invalid"));
	char defaults[] = "game=Crash_Bandicoot";
	assert(switches_run(defaults, game, sizeof(game), err, sizeof(err)));
	assert(sw_frontend && !sw_floataudio && !sw_rtfs && !sw_rtfsdense && sw_rtfscap == 4);
	assert(pica_submit == 7 && pica_prims == 7 && pica_texmask && pica_texmemo && psx3d_skipgate);
	assert(!sw_hack_spunearest);
	assert(!psx3d_idle_pointer && !psx3d_idle_delay);
	command("set renderer=gpu", true);
	assert(strstr(response, "false"));
	command("ui-key down", false); assert(strstr(response,"false"));
 ui_active=true;
 command("ui-key down",false); assert(strstr(response,"true"));
 command("ui-key home",false); assert(strstr(response,"false"));
 ui_active=false;
 command("ui pause", false);
	assert(strstr(response, "false") && !dev_ui_pending());
	command("ui settings", true);
	assert(dev_ui_take() == DEV_UI_SETTINGS && !dev_ui_pending());
	command("ui states slot=9", true);
	assert(dev_ui_take() == DEV_UI_STATES && dev_ui_slot == 9);
	command("ui states slot=10", true);
	assert(strstr(response, "false") && !dev_ui_pending());
	command("ui states slot=9oops", true);
	assert(strstr(response, "false") && !dev_ui_pending());
	command("ui library select=Missing", true);
	assert(strstr(response, "false") && !dev_ui_pending());
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
	// Replay samples include the first interval and final partial interval,
	// ignore wall-time seconds/warmup, and stop a batch on divergence.
	command("quit", false); dev_next_game(path,save,title);
	assert(switches_set("record=on") && clip_enabled);
	assert(switches_set("record=off") && !clip_enabled);
	assert(!switches_set("replay=-1") && !switches_set("record_seconds=0"));
	char conflict[]="game=Crash_Bandicoot state=2 replay=1";
	assert(!switches_run(conflict,game,sizeof(game),err,sizeof(err)));
	command("bench game=Crash_Bandicoot replay=1 warmup=999 seconds=1 shot=0",false);
	assert(dev_next_game(path,save,title) && clip_requested==1 && sw_state==-1);
	clip_status=(clip_result){.replay=true,.frames=150}; clip_replay_session=true;
	mock_ms=10000; dev_game_started(title,true);
	sample(11000,50); assert(samples==1 && batch_active);
	sample(12000,50); assert(samples==2 && batch_active);
	clip_status.frame=150; clip_status.checks=3; clip_status.done=true;
	sample(12900,50); assert(samples==3 && !batch_active);
	dev_game_ended(true); clip_close();
	command("quit",false); dev_next_game(path,save,title);
	command("bench game=Crash_Bandicoot replay=1 shot=0;game=Crash_Bandicoot replay=1 shot=0",false);
	assert(dev_next_game(path,save,title));
	clip_status=(clip_result){.replay=true,.frames=120}; clip_replay_session=true;
	mock_ms=20000; dev_game_started(title,true);
	sample(21000,50); clip_status.done=clip_status.failed=true; clip_status.first_divergent=60;
	sample(22000,50); assert(!batch_active && run_index==0 && !load_pending);
	dev_game_ended(true); clip_close();
	dev_shutdown();
	puts("developer lifecycle, batch averaging, state validation and safe failures: PASS");
}
