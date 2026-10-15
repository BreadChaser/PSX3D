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
	}
	memset(opts, 0, sizeof(opts));
	nopts = 0;
}

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
	nvisible = 0;
	for (int i = 0; i < nopts; i++)
		if (!is_hidden(opts[i].key))
			visible[nvisible++] = i;
	log_printf("registered %d core options (%d shown)\n", nopts, nvisible);
}

static option_t *find(const char *key)
{
	for (int i = 0; i < nopts; i++)
		if (strcmp(opts[i].key, key) == 0)
			return &opts[i];
	return NULL;
}

const char *options_get(const char *key)
{
	const char *ov = locked_value(key);
	option_t *o = find(key);

	if (!ov)
		ov = config_core_override(key);
	if (o && ov) {
		// only hand the core values it actually knows about
		for (int j = 0; j < o->nvalues; j++)
			if (strcmp(o->values[j], ov) == 0)
				return o->values[j];
	}
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
