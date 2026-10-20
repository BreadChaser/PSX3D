#include "devtools.h"
#include "clip.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern float sw_slider;
extern unsigned pica_submit;
extern int psx3d_idle_skip, psx3d_idle_pointer, psx3d_idle_delay;
extern bool sw_aa, sw_showright, sw_sbs;
void debug_parse_switch(const char *word);

// Per-run overrides never persist to the user's config. Subsystem startup switches
// remain startup-only: changing SOC/audio allocation mid-session is unsafe.
bool switches_set(const char *word)
{
	/* Development A/B only; never persisted as a user setting. */
	if (!strcmp(word, "fsmart=old") || !strcmp(word, "fsmart=new")) {
		sw_fsmart = !strcmp(word, "fsmart=new"); return true;
	}
	/* Recorder options apply to both startup and remote loads. */
	if (clip_switch(word)) return true;
	if (!strcmp(word, "wsbundle=on") || !strcmp(word, "wsbundle=off") ||
	    !strcmp(word, "wsscreen=4:3") || !strcmp(word, "wsscreen=stretch") || !strcmp(word, "wsscreen=wide")) {
		debug_parse_switch(word); return true;
	}
	if (!strncmp(word, "rtfscap=", 8)) {
        char *end; long n = strtol(word+8, &end, 10);
        if (!word[8] || *end || n < 4 || n > 5) return false;
        sw_rtfscap = n; sw_rtfs = true; return true;
    }
	if (!strncmp(word, "rtfslevel=", 10)) {
		char *end; long n = strtol(word+10, &end, 10);
		if (!word[10] || *end || n < 0 || n > 7) return false;
		sw_rtfslevel = n; sw_rtfs = true; return true;
	}
	if (!strncmp(word, "frameskip=", 10)) {
		const char *v = word + 10;
		if (strcmp(v, "off") && strcmp(v, "on") && strcmp(v, "auto"))
			return false;
		sw_noskip = !strcmp(v, "off");
		sw_fs1 = sw_fs3 = false;
		word = sw_noskip ? "o:frameskip_type=disabled" : "o:frameskip_type=auto";
	}
	const char *integer_keys[] = {"state=", "clock=", "cdra=", "gldbg=", "vramdump=", "autosavetest="};
	const long limits[] = {9, 100, 1024, 0x7fffffff, 1000000, 3600};
	for (unsigned i = 0; i < sizeof(limits) / sizeof(*limits); i++) {
		size_t n = strlen(integer_keys[i]);
		if (!strncmp(word, integer_keys[i], n)) {
			char *end;
			long value = strtol(word + n, &end, 10);
			if (!word[n] || *end || value < 0 || value > limits[i])
				return false;
			if (i == 1 && !value)
				return false;
		}
	}
	if (!strncmp(word, "slider=", 7)) {
		char *end;
		float value = strtof(word + 7, &end);
		if (!word[7] || *end || !isfinite(value) || value < 0 || value > 1)
			return false;
	}
	if (!strncmp(word, "pmu=", 4)) {
		unsigned a, b;
		int n = 0;
		if (sscanf(word + 4, "%x,%x%n", &a, &b, &n) != 2 || word[4 + n] || a > 255 || b > 255)
			return false;
	}
	if (!strncmp(word, "o:", 2)) {
		const char *eq = strchr(word + 2, '=');
		if (!eq || eq == word + 2 || !eq[1] || strlen(word + 2) >= sizeof(sw_opt[0]))
			return false;
		size_t n = eq - (word + 2);
		for (int k = 0; k < 8; k++)
			if (!sw_opt[k][0] || (strcspn(sw_opt[k], "=") == n && !strncmp(sw_opt[k], word + 2, n))) {
				strcpy(sw_opt[k], word + 2);
				options_mark_updated();
				return true;
			}
		return false;
	}
	static const char *flags[] = {"prof",         "stats",     "fs1",         "fs3",  "noskip",
	                              "nogputhread",  "gputhread", "nosputhread", "acap", "renderer=sw",
	                              "renderer=gpu", "state0",    "showright",   "sbs",  "gpuasync",
	                              "nogpuasync",   "gpuflush",  "nogpuflush",  "aa", "rtt=old", "rtt=new", "rtt=auto", "rttcache=old", "rttcache=new", "rtfs", "rtfs=old", "rtfs=new", "rtfs=model", "rtfs=dense", "frontend=old", "frontend=new", "audio=old", "audio=new", "ff=on", "ff=off", "submitstate=old", "submitstate=new", "submitclip=old", "submitclip=new", "submittex=old", "submittex=new", "pmuclass=on", "pmuclass=off", "pmurender=on", "pmurender=off", "pica_prims_old", "pica_prims_fast", "pica_prims_copy", "pica_prims_clip", "pica_prims_round", "skipgate=old", "skipgate=new", "texmask=old", "texmask=new", "texmemo=old", "texmemo=new", "idle=on", "idle=off", "idleptr=on", "idleptr=off", "idledelay=on", "idledelay=off"};
	static const char *prefix[] = {"slider=", "state=", "autosavetest=", "gldbg=", "vramdump=", "pmu=", "cdra=", "clock="};
	bool known = false;
	for (unsigned k = 0; k < sizeof(flags) / sizeof(*flags); k++)
		if (!strcmp(word, flags[k]))
			known = true;
	for (unsigned k = 0; k < sizeof(prefix) / sizeof(*prefix); k++)
		if (!strncmp(word, prefix[k], strlen(prefix[k])))
			known = true;
	if (!known)
		return false;
	debug_parse_switch(word);
	options_mark_updated();
	return true;
}

/* A remote load/batch applies its switches to the same globals as switches.txt.
 * Snapshot the startup values before the first remote run and put them back
 * before a local launch, so remote runs never change local play (a leftover
 * replay= ignored local input, state= loaded a benchmark slot, o: options stuck). */
static struct {
	bool saved;
	int idle_skip, idle_pointer, idle_delay, rtfslevel, rtfscap, state, autosave_test, pmu0, pmu1, gldbg, vramdump, skipgate;
	int clip_requested, clip_seconds;
	unsigned submit, prims;
	bool rtfsdense, rtfsmodel, rtfswindow, rtfs, ff, floataudio, frontend, fsmart, prof, stats, fs1, fs3, noskip;
	bool nosputhread, acap, nogputhread, aa, showright, sbs, gpu_async, gpu_flush, pmuphase, pmuclass;
	bool texmask, texmemo, clip_enabled;
	float slider;
	char opt[8][48], cdra[8], clock[8];
} local;
#define SWITCH_COPY(TO, FROM)                                                                                  \
	do {                                                                                                         \
		TO(idle_skip, psx3d_idle_skip); TO(idle_pointer, psx3d_idle_pointer); TO(idle_delay, psx3d_idle_delay); \
		TO(rtfslevel, sw_rtfslevel); TO(rtfscap, sw_rtfscap); TO(state, sw_state); TO(pmu0, sw_pmu0);           \
		TO(autosave_test, sw_autosave_test);                                                                   \
		TO(pmu1, sw_pmu1); TO(gldbg, sw_gldbg); TO(vramdump, sw_vramdump); TO(skipgate, psx3d_skipgate);       \
		TO(clip_requested, clip_requested); TO(clip_seconds, clip_seconds); TO(submit, pica_submit);           \
		TO(prims, pica_prims); TO(rtfsdense, sw_rtfsdense); TO(rtfsmodel, sw_rtfsmodel);                       \
		TO(rtfswindow, sw_rtfswindow); TO(rtfs, sw_rtfs); TO(ff, sw_ff); TO(floataudio, sw_floataudio);        \
		TO(frontend, sw_frontend); TO(fsmart, sw_fsmart); TO(prof, sw_prof); TO(stats, sw_stats);              \
		TO(fs1, sw_fs1); TO(fs3, sw_fs3); TO(noskip, sw_noskip); TO(nosputhread, sw_nosputhread);              \
		TO(acap, sw_acap); TO(nogputhread, sw_nogputhread); TO(aa, sw_aa); TO(showright, sw_showright);        \
		TO(sbs, sw_sbs); TO(gpu_async, sw_gpu_async); TO(gpu_flush, sw_gpu_flush); TO(pmuphase, sw_pmuphase);  \
		TO(pmuclass, sw_pmuclass); TO(texmask, pica_texmask); TO(texmemo, pica_texmemo);                       \
		TO(clip_enabled, clip_enabled); TO(slider, sw_slider);                                                 \
		FROM(opt, sw_opt); FROM(cdra, sw_cdra); FROM(clock, sw_clock);                                         \
	} while (0)
#define SAVE_VALUE(field, var) local.field = var
#define SAVE_ARRAY(field, var) memcpy(local.field, var, sizeof(local.field))
#define LOAD_VALUE(field, var) var = local.field
#define LOAD_ARRAY(field, var) memcpy(var, local.field, sizeof(local.field))
void switches_local(void)
{
	if (!local.saved)
		return;
	SWITCH_COPY(LOAD_VALUE, LOAD_ARRAY);
	options_mark_updated();
}
bool switches_run(char *line, char *game, size_t len, char *error, size_t error_len)
{
	if (!local.saved) {
		SWITCH_COPY(SAVE_VALUE, SAVE_ARRAY);
		local.saved = true;
	}
	clip_reset_request();
	sw_wsbundle = true; sw_wsscreen = -1;
	psx3d_idle_skip = 1; psx3d_idle_pointer = psx3d_idle_delay = 0;
	sw_rtfslevel = -1; sw_rtfscap = 4; pica_submit = 7;
	sw_rtfsdense = sw_rtfsmodel = sw_rtfswindow = sw_rtfs = sw_ff = sw_floataudio = false;
	sw_frontend = sw_fsmart = true;
	sw_prof = sw_stats = sw_fs1 = sw_fs3 = sw_noskip = sw_nosputhread = sw_acap = false;
	sw_nogputhread = sw_aa = sw_showright = sw_sbs = false;
	sw_gpu_async = sw_gpu_flush = true;
	sw_slider = -1;
	sw_state = sw_pmu0 = sw_pmu1 = -1;
	sw_autosave_test = 0;
	sw_gldbg = sw_vramdump = 0;
	pica_prims = 7; sw_pmuphase = sw_pmuclass = false;
	pica_texmask = pica_texmemo = true; psx3d_skipgate = 1;
	memset(sw_opt, 0, sizeof(sw_opt));
	sw_cdra[0] = sw_clock[0] = 0;
	game[0] = 0;
	char *ctx, *w = strtok_r(line, " \t\r\n", &ctx);
	for (; w; w = strtok_r(NULL, " \t\r\n", &ctx)) {
		if (!strncmp(w, "game=", 5)) {
			if (strlen(w + 5) >= len) {
				snprintf(error, error_len, "game path too long");
				return false;
			}
			strcpy(game, w + 5);
		} else if (!strncmp(w, "seconds=", 8) || !strncmp(w, "warmup=", 7) || !strncmp(w, "shot=", 5))
			continue;
		else if (!switches_set(w)) {
			snprintf(error, error_len, "unsupported or startup-only switch: %.80s", w);
			return false;
		}
	}
	if (sw_state < -1 || sw_state > 9) {
		snprintf(error, error_len, "state must be 0..9");
		return false;
	}
	if (clip_requested >= 0 && sw_state >= 0) {
		snprintf(error, error_len, "replay= and state= are mutually exclusive");
		return false;
	}
	if (!game[0]) {
		snprintf(error, error_len, "game= is required");
		return false;
	}
	return true;
}
