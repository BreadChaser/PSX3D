#include <stdio.h>
// Core options: the core registers its options with SET_VARIABLES
// ("Description; default|value2|value3"). Values set by the user are stored
// in config.c as overrides; everything else falls back to the core default.
#include <stdlib.h>
#include <string.h>
#include "psx3d.h"

#define MAX_OPTIONS 128
#define MAX_VALUES  64

typedef struct {
	char *key;
	char *desc;
	char *info;      // the core's explanation (v1 options)
	char *def;       // default value (v1 options; v0 uses values[0])
	char *values[MAX_VALUES];
	int   nvalues;
} option_t;

static option_t opts[MAX_OPTIONS];
static int nopts;
static bool updated;

// PSX3D only runs on the 3DS, so some options have exactly one right answer
// here. They're fixed and not shown in the menu.
static const char *const locked[][2] = {
	{ "pcsx_rearmed_scale_hires", "enabled" },        // 240-line screen: never draw 480 lines
	{ "pcsx_rearmed_gpu_unai_skipline", "disabled" }, // superseded by scale_hires
	{ "pcsx_rearmed_spu_interpolation", "gaussian" }, // what the real SPU does
	{ "pcsx_rearmed_spu_reverb", "enabled" },
	{ "pcsx_rearmed_spu_thread", "disabled" },        // would share core 2 with the GPU
	{ "pcsx_rearmed_gpu_thread_rendering", "enabled" },
	{ "pcsx_rearmed_drc_thread", "disabled" },
	{ "pcsx_rearmed_rgb32_output", "disabled" },      // frontend draws RGB565
	{ "pcsx_rearmed_fractional_framerate", "auto" },  // frontend paces frames itself
	{ "pcsx_rearmed_display_fps_v2", "disabled" },    // PSX3D has its own FPS counter
	{ "pcsx_rearmed_3ds_appcputime", "35" },          // no effect from the Homebrew Launcher
};

// options for hardware or renderers this build doesn't have
static const char *const hidden_prefix[] = {
	"pcsx_rearmed_neon_",        // NEON renderer: no NEON on the 3DS CPU
	"pcsx_rearmed_gpu_peops_",   // P.E.Op.S. renderer isn't built in
	"pcsx_rearmed_show_",        // RetroArch menu folding
	"pcsx_rearmed_crosshair",    // lightguns
	"pcsx_rearmed_konamigun",
	"pcsx_rearmed_guncon",
	"pcsx_rearmed_negcon_",
	"pcsx_rearmed_input_sensitivity",  // mouse
	"pcsx_rearmed_vibration",    // no rumble motor
	"pcsx_rearmed_multitap",     // one player per 3DS
	"pcsx_rearmed_drc",          // chosen automatically (Luma SVC access)
};

static const char *locked_value(const char *key)
{
	// Old 3DS: no core 2, and the syscore gives Homebrew Launcher apps no
	// CPU time, so the core's helper threads would starve there. The CIA
	// gets 30% of it: enough for sound and CD reads, not for drawing.
	if (!g_new3ds) {
		// The syscore only gives us 30% in time slices: drawing there made
		// the game wait (25 fps vs 36). Sound is buffered and copes.
		if (strcmp(key, "pcsx_rearmed_gpu_thread_rendering") == 0)
			return "disabled";
		// Sound mixing there pays off a little (Crash, GT2 +3%): it runs
		// ahead in the slices, and we mix it here when it hasn't started.
		// Not from the Homebrew Launcher: no syscore time at all there.
		if (strcmp(key, "pcsx_rearmed_spu_thread") == 0)
			return g_syscore_pct && !sw_nosputhread ? "enabled" : "disabled";
		if (strcmp(key, "pcsx_rearmed_3ds_appcputime") == 0) {
			static char b[8];
			snprintf(b, sizeof(b), "%d", g_syscore_pct ? g_syscore_pct : 35);
			return b;
		}
		// CD reads there: CTR streams music, 1.3 -> 0.35ms a frame here
		if (strcmp(key, "pcsx_rearmed_cd_readahead") == 0)
			return sw_cdra[0] ? sw_cdra : g_syscore_pct ? "256" : "0";
		// gaussian interpolation costs real time on a 268MHz core
		if (strcmp(key, "pcsx_rearmed_spu_interpolation") == 0)
			return "simple";
		if (strcmp(key, "pcsx_rearmed_spu_reverb") == 0)
			return NULL;   // a choice on Old 3DS (default off, below)
	}
	for (size_t i = 0; i < sizeof(locked) / sizeof(locked[0]); i++)
		if (strcmp(locked[i][0], key) == 0)
			return locked[i][1];
	return NULL;
}

static bool is_hidden(const char *key)
{
	if (locked_value(key))
		return true;
	for (size_t i = 0; i < sizeof(hidden_prefix) / sizeof(hidden_prefix[0]); i++)
		if (strncmp(key, hidden_prefix[i], strlen(hidden_prefix[i])) == 0)
			return true;
	return false;
}

// menu index -> option index, visible options only
static int visible[MAX_OPTIONS];
static int nvisible;

static void options_clear(void)
{
	for (int i = 0; i < nopts; i++) {
		free(opts[i].key);
		free(opts[i].desc);
		for (int j = 0; j < opts[i].nvalues; j++)
			free(opts[i].values[j]);
		free(opts[i].info);
		free(opts[i].def);
	}
	memset(opts, 0, sizeof(opts));
	nopts = 0;
}

static void finish_register(void);

void options_register(const struct retro_variable *vars)
{
	options_clear();
	for (; vars && vars->key && nopts < MAX_OPTIONS; vars++) {
		option_t *o = &opts[nopts];
		const char *semi = strstr(vars->value, "; ");
		if (!semi)
			continue;
		o->key = strdup(vars->key);
		o->desc = strndup(vars->value, semi - vars->value);

		const char *p = semi + 2;
		while (*p && o->nvalues < MAX_VALUES) {
			const char *bar = strchr(p, '|');
			size_t len = bar ? (size_t)(bar - p) : strlen(p);
			o->values[o->nvalues++] = strndup(p, len);
			if (!bar)
				break;
			p = bar + 1;
		}
		nopts++;
	}
	finish_register();
}

static void finish_register(void)
{
	nvisible = 0;
	for (int i = 0; i < nopts; i++)
		if (!is_hidden(opts[i].key))
			visible[nvisible++] = i;
	log_printf("registered %d core options (%d shown)\n", nopts, nvisible);
}

// RETRO_ENVIRONMENT_SET_CORE_OPTIONS (v1): same options with descriptions
void options_register_v1(const struct retro_core_option_definition *defs)
{
	options_clear();
	for (; defs && defs->key && nopts < MAX_OPTIONS; defs++) {
		option_t *o = &opts[nopts];
		o->key = strdup(defs->key);
		o->desc = strdup(defs->desc ? defs->desc : defs->key);
		o->info = defs->info ? strdup(defs->info) : NULL;
		o->def = defs->default_value ? strdup(defs->default_value) : NULL;
		for (int j = 0; j < RETRO_NUM_CORE_OPTION_VALUES_MAX && defs->values[j].value && o->nvalues < MAX_VALUES; j++)
			o->values[o->nvalues++] = strdup(defs->values[j].value);
		if (o->nvalues)
			nopts++;
	}
	finish_register();
}

static option_t *find(const char *key)
{
	for (int i = 0; i < nopts; i++)
		if (strcmp(opts[i].key, key) == 0)
			return &opts[i];
	return NULL;
}

// Fast-forward: the core skips drawing 3 of every 4 frames while it's held
// (only the last one is shown anyway). Never saved.
static bool ff_skip;

void options_fast_forward(bool on)
{
	if (on == ff_skip)
		return;
	ff_skip = on;
	options_mark_updated();
}

const char *options_get(const char *key)
{
	if (!strcmp(key, "pcsx_rearmed_gpu_thread_rendering") && (!g_new3ds || sw_nogputhread))
		return "disabled";
	const char *ov = locked_value(key);
	option_t *o = find(key);

	for (int k = 0; k < 8 && sw_opt[k][0]; k++) {
		size_t n = strcspn(sw_opt[k], "=");
		if (!strncmp(key, "pcsx_rearmed_", 13) && strlen(key + 13) == n && !strncmp(key + 13, sw_opt[k], n))
			return sw_opt[k] + n + (sw_opt[k][n] == '=');
	}
	if (sw_noskip && !strcmp(key, "pcsx_rearmed_frameskip_type"))
		return "disabled";
	if (sw_clock[0] && !strcmp(key, "pcsx_rearmed_psxclock"))
		return sw_clock;
	if (sw_fs1 && !strcmp(key, "pcsx_rearmed_frameskip_type"))
		return "fixed_interval";
	if (sw_fs1 && !strcmp(key, "pcsx_rearmed_frameskip_interval"))
		return "1";
	if (ff_skip && !strcmp(key, "pcsx_rearmed_frameskip_type"))
		return "fixed_interval";
	if (ff_skip && !strcmp(key, "pcsx_rearmed_frameskip_interval"))
		return "3";

	if (!ov)
		ov = config_core_override(key);
	// Old 3DS: one 268MHz core runs the CPU, GPU and sound. Defaults that
	// trade a little accuracy for speed (all still changeable in the menu).
	if (!ov && !g_new3ds) {
		static const char *const o3ds[][2] = {
			{ "pcsx_rearmed_frameskip_type", "auto" },        // skip drawing, keep game speed
			{ "pcsx_rearmed_frameskip_interval", "2" },       // at most 2 skipped in a row
			{ "pcsx_rearmed_gpu_unai_fast_lighting", "enabled" },
			{ "pcsx_rearmed_dithering", "disabled" },
			{ "pcsx_rearmed_spu_reverb", "disabled" },
			// GTE speed hacks: rare glitches, measurable gain on a 268MHz core
			{ "pcsx_rearmed_gteregsunneeded", "enabled" },
			{ "pcsx_rearmed_nogteflags", "enabled" },
		};
		for (size_t i = 0; i < sizeof(o3ds) / sizeof(o3ds[0]); i++)
			if (strcmp(key, o3ds[i][0]) == 0)
				ov = o3ds[i][1];
	}
	if (o && ov) {
		// only hand the core values it actually knows about
		for (int j = 0; j < o->nvalues; j++)
			if (strcmp(o->values[j], ov) == 0)
				return o->values[j];
	}
	if (o && o->def)
		return o->def;
	if (o && o->nvalues)
		return o->values[0];
	return ov;
}

bool options_take_updated(void)
{
	bool r = updated;
	updated = false;
	return r;
}

void options_mark_updated(void) { updated = true; }
// the menu only sees the visible options
int options_count(void) { return nvisible; }
const char *options_key(int i) { return opts[visible[i]].key; }
const char *options_desc(int i) { return opts[visible[i]].desc; }

// Step option i to the next/previous value and store it as an override.
const char *options_cycle(int i, int dir)
{
	option_t *o = &opts[visible[i]];
	const char *cur = options_get(o->key);
	int idx = 0;

	for (int j = 0; j < o->nvalues; j++)
		if (cur && strcmp(o->values[j], cur) == 0)
			idx = j;
	idx = (idx + dir + o->nvalues) % o->nvalues;
	config_set_core_override(o->key, o->values[idx]);
	updated = true;
	return o->values[idx];
}

// ---------------------------------------------------------------- help
// What each option does and what it costs, shown on the top screen while
// it's selected. PSX3D's own notes win over the core's text; the impact
// line is ours (measured on 3DS where it says so).
static const char *const notes[][3] = {
	// key, impact, note (NULL = use the core's text)
	{ "pcsx_rearmed_psxclock", "Lower = faster, but games run their logic slower", "How fast the emulated PS1 CPU runs. Lowering it saves a lot of 3DS CPU time (Crash on Old 3DS: 36 -> 44 FPS at 30%) but games that need the full CPU will slow down or stutter. auto picks per game." },
	{ "pcsx_rearmed_frameskip_type", "Faster when the 3DS can't keep up", "Skips drawing some frames when the 3DS falls behind, so the game itself keeps full speed. auto skips only when needed." },
	{ "pcsx_rearmed_frameskip_threshold", "Higher = skips sooner", NULL },
	{ "pcsx_rearmed_frameskip_interval", "Higher = smoother speed, choppier picture", "The most frames that can be skipped in a row." },
#ifdef PSX3D_PICA
	{ "pcsx_rearmed_dithering", "PS1 colour shading", "Adds the PS1's fine colour pattern to shaded polygons. Off gives smoother-looking bands. enabled follows the game; force also enables it when the game turns it off." },
#else
	{ "pcsx_rearmed_dithering", "Slightly slower when on", "The PS1's colour dithering pattern. Off gives a cleaner picture and saves a little time." },
#endif
	{ "pcsx_rearmed_gpu_unai_fast_lighting", "Faster (small)", "A quicker way to shade textured polygons. Colours can be slightly off in some games." },
	{ "pcsx_rearmed_gpu_unai_lighting", "Off is faster, but most 3D games look wrong", NULL },
	{ "pcsx_rearmed_gpu_unai_blending", "Off is faster, but transparency breaks", NULL },
	{ "pcsx_rearmed_spu_reverb", "Off saves CPU time", "The PS1 sound chip's echo effect. Off sounds drier but frees CPU time, mostly useful on Old 3DS." },
	{ "pcsx_rearmed_nosmccheck", "Faster, but can crash games and break saves", NULL },
	{ "pcsx_rearmed_gteregsunneeded", "Faster (small), may cause glitches", "Skips saving some 3D math results games rarely read back. Usually safe; rare graphics glitches." },
	{ "pcsx_rearmed_nogteflags", "Faster (small), can cause graphics errors", "Skips the 3D math unit's overflow flags. A few games use them for clipping and draw wrong without them." },
	{ "pcsx_rearmed_cd_turbo", "Faster loading, can crash games", NULL },
	{ "pcsx_rearmed_cd_readahead", "Fewer loading stutters on New 3DS", "Reads ahead on the disc image on a spare core, so the game doesn't wait for the SD card." },
	{ "pcsx_rearmed_icache_emulation", "Slightly slower when on", NULL },
	{ "pcsx_rearmed_region", "No speed effect", NULL },
	{ "pcsx_rearmed_bios", "No speed effect", "Which BIOS to use. A real BIOS in sdmc:/3ds/PSX3D/bios fixes some games that don't boot with the built-in one." },
};

const char *options_info(int i)
{
	const option_t *o = (i >= 0 && i < nvisible) ? &opts[visible[i]] : NULL;
	if (!o)
		return "";
	for (size_t n = 0; n < sizeof(notes) / sizeof(notes[0]); n++)
		if (!strcmp(notes[n][0], o->key) && notes[n][2])
			return notes[n][2];
	return o->info ? o->info : "";
}

const char *options_impact(int i)
{
	const option_t *o = (i >= 0 && i < nvisible) ? &opts[visible[i]] : NULL;
	if (!o)
		return NULL;
	for (size_t n = 0; n < sizeof(notes) / sizeof(notes[0]); n++)
		if (!strcmp(notes[n][0], o->key))
			return notes[n][1];
	return NULL;
}
