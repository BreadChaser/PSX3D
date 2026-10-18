#include "devtools.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern float sw_slider;
extern bool sw_aa, sw_showright, sw_sbs;
void debug_parse_switch(const char *word);

// Per-run overrides never persist to the user's config. Subsystem startup switches
// remain startup-only: changing SOC/audio allocation mid-session is unsafe.
bool switches_set(const char *word)
{
	if (!strncmp(word, "frameskip=", 10)) {
		const char *v = word + 10;
		if (strcmp(v, "off") && strcmp(v, "on") && strcmp(v, "auto"))
			return false;
		sw_noskip = !strcmp(v, "off");
		sw_fs1 = sw_fs3 = false;
		word = sw_noskip ? "o:frameskip_type=disabled" : "o:frameskip_type=auto";
	}
	const char *integer_keys[] = {"state=", "clock=", "cdra=", "gldbg=", "vramdump="};
	const long limits[] = {9, 100, 1024, 0x7fffffff, 1000000};
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
	                              "nogpuasync",   "gpuflush",  "nogpuflush",  "aa", "rtt=old", "rtt=new"};
	static const char *prefix[] = {"slider=", "state=", "gldbg=", "vramdump=", "pmu=", "cdra=", "clock="};
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

bool switches_run(char *line, char *game, size_t len, char *error, size_t error_len)
{
	sw_prof = sw_stats = sw_fs1 = sw_fs3 = sw_noskip = sw_nosputhread = sw_acap = false;
	sw_nogputhread = sw_aa = sw_showright = sw_sbs = false;
	sw_gpu_async = sw_gpu_flush = true;
	sw_slider = -1;
	sw_state = sw_renderer = sw_pmu0 = sw_pmu1 = -1;
	sw_gldbg = sw_vramdump = 0;
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
	if (!game[0]) {
		snprintf(error, error_len, "game= is required");
		return false;
	}
	return true;
}
