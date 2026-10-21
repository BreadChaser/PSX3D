#include <stdio.h>
// Core options: the core registers its options with SET_VARIABLES
// ("Description; default|value2|value3"). Values set by the user are stored
// in config.c as overrides; everything else falls back to the core default.
#include <stdlib.h>
#include <string.h>
#include "psx3d.h"
#include "clip.h"

#define MAX_OPTIONS 128
#define MAX_VALUES  80   // psxclock has 72 (Auto, 30..100): 64 cut off 93..100
#define RENDERER_KEY "psx3d_renderer"

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
	"pcsx_rearmed_gpu_unai_",
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

// Options changed in a game's menu belong to that game: games/<game>.ini
// "o.<name>" (pcsx_rearmed_ prefix dropped to fit the key length).
const char *options_game_key(const char *key)
{
	static char k[32];
	snprintf(k, sizeof(k), "o.%s", !strncmp(key, "pcsx_rearmed_", 13) ? key + 13 : key);
	return k;
}
static const char *saved_value(const char *key)
{
	const char *v = config_in_game() ? gamecfg_get(options_game_key(key)) : NULL;
	return v ? v : config_core_override(key);
}

static bool option_is(const char *key, const char *value)
{
	const char *v = options_get(key);
	return v && !strcmp(v, value);
}

static bool is_hidden(const char *key)
{
	// Renderer selection has been removed. The 3D switches are
	// owned by the frontend, never exposed as duplicate raw core options.
	if (!strcmp(key, RENDERER_KEY) || !strcmp(key, "pcsx_rearmed_psx3d_stereo") ||
	    !strcmp(key, "pcsx_rearmed_psx3d_widescreen")) return true;
	if (!strncmp(key, "pcsx_rearmed_screen_centering", sizeof("pcsx_rearmed_screen_centering") - 1)) return true;
	if (!strcmp(key, "pcsx_rearmed_display_info") && !g_cfg.debug && !settings_ui_debug_capture()) return true;
	if (config_in_game() && !g_cfg.show_all_settings &&
	    ((!strcmp(key, "pcsx_rearmed_frameskip_type") && game_preset_sets("frameskip"))
#ifdef PSX3D_PS1_OVERCLOCK_PRESETS
	     || (!strcmp(key, "pcsx_rearmed_psxclock") && game_preset_sets("psxclock"))
#endif
	    )) return true;
	if ((!strcmp(key, "pcsx_rearmed_screen_centering_x") ||
	     !strcmp(key, "pcsx_rearmed_screen_centering_y") ||
	     !strcmp(key, "pcsx_rearmed_screen_centering_h_adj")) &&
	    !option_is("pcsx_rearmed_screen_centering", "manual")) return true;
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

static bool retired_frameskip(const char *key)
{
	return !strcmp(key, "pcsx_rearmed_frameskip_threshold") ||
	       !strcmp(key, "pcsx_rearmed_frameskip_interval");
}
static void finish_register(void);

void options_register(const struct retro_variable *vars)
{
	options_clear();
	for (; vars && vars->key && nopts < MAX_OPTIONS; vars++) {
		if (retired_frameskip(vars->key)) continue;
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
	for (int i = 0; i < nopts; i++) {
		option_t *o = &opts[i];
		if (!strcmp(o->key, "pcsx_rearmed_frameskip_type")) {
			for (int j = 0; j < o->nvalues; j++) free(o->values[j]);
			o->values[0] = strdup("disabled"); o->values[1] = strdup("auto");
			o->nvalues = 2;
			free(o->def); o->def = strdup("auto");
		}
		// Shaded polygons without the PS1's dither quantize to 15-bit colour
		// in tinted bands (Silent Hill's fogged ground). Follow the game.
		if (!strcmp(o->key, "pcsx_rearmed_dithering")) {
			free(o->def); o->def = strdup("enabled");
		}
	}
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
		if (retired_frameskip(defs->key)) continue;
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
	// Developer clips use synchronous sound/CD. Keep normal defaults intact.
	if ((clip_enabled || clip_requested >= 0) &&
	    (!strcmp(key, "pcsx_rearmed_spu_thread") || !strcmp(key, "pcsx_rearmed_cd_readahead")))
		return !strcmp(key, "pcsx_rearmed_spu_thread") ? "disabled" : "0";
	const char *recorded = clip_option(key);
	if (recorded) return recorded;
	if (!g_new3ds && !strcmp(key, "pcsx_rearmed_psx3d_stereo"))
		return "disabled";
	// Widescreen comes only from a game's own fix, never the GTE squeeze.
	if (!strcmp(key, "pcsx_rearmed_psx3d_widescreen"))
		return "0";
	if (!strcmp(key, "pcsx_rearmed_gpu_thread_rendering") && (!g_new3ds || sw_nogputhread))
		return "disabled";
	if (!strcmp(key, RENDERER_KEY) || !strncmp(key, "pcsx_rearmed_gpu_unai_", sizeof("pcsx_rearmed_gpu_unai_") - 1)) return NULL;
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

	// Test only the skip limit; keep the selected frameskip policy intact.
	if (sw_fs3 && !g_new3ds && !strcmp(key, "pcsx_rearmed_frameskip_interval"))
		return "3";

	if (!strcmp(key, "pcsx_rearmed_frameskip_type")) {
		const char *per_game = config_in_game() ? gamecfg_get(options_game_key(key)) : NULL;
		if (per_game) return !strcmp(per_game, "disabled") ? "disabled" : "auto";
		if (config_in_game() && game_preset_sets("frameskip"))
			return game_capabilities().frameskip ? "auto" : "disabled";
		const char *saved = config_core_override(key);
		return saved && !strcmp(saved, "disabled") ? "disabled" : "auto";
	}
	// Core-only legacy/fast-forward policy knobs; never read saved values.
	if (!strcmp(key, "pcsx_rearmed_frameskip_interval")) return "2";
	if (!strcmp(key, "pcsx_rearmed_frameskip_threshold")) return "33";
#ifdef PSX3D_PS1_OVERCLOCK_PRESETS
	// Technical setting: the game's clock preset beats the all-games value,
	// the player's own per-game value beats the preset (docs/presets.md).
	static char preset_clock[8];
	if (!ov && config_in_game() && !strcmp(key, "pcsx_rearmed_psxclock") &&
	    !gamecfg_get(options_game_key(key)) && game_capabilities().psxclock > 0) {
		if (atoi(preset_clock) != game_capabilities().psxclock) {
			snprintf(preset_clock, sizeof(preset_clock), "%d", game_capabilities().psxclock);
			log_printf("preset: PS1 clock %s\n", preset_clock);
		}
		ov = preset_clock;
	}
#endif
	if (!ov)
		ov = saved_value(key);
	// Old 3DS: one 268MHz core runs the CPU, GPU and sound. Defaults that
	// trade a little accuracy for speed (all still changeable in the menu).
	if (!ov && !g_new3ds) {
		static const char *const o3ds[][2] = {
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

void options_mark_updated(void) { updated = true; game_presets_invalidate(); }
// the menu only sees the visible options
int options_count(void)
{
	// Only menu code calls this; rebuild when renderer or parent choices change.
	nvisible = 0;
	for (int i = 0; i < nopts; i++)
		if (!is_hidden(opts[i].key)) visible[nvisible++] = i;
	return nvisible;
}
const char *options_key(int i) { return opts[visible[i]].key; }
static const char *const player_names[][2] = {
	{ "pcsx_rearmed_region", "Game region" }, { "pcsx_rearmed_bios", "BIOS" },
	{ "pcsx_rearmed_memcard1", "Memory card 1" }, { "pcsx_rearmed_memcard2", "Memory card 2" },
	{ "pcsx_rearmed_cd_readahead", "Read disc ahead" }, { "pcsx_rearmed_psxclock", "Game CPU speed" },
	{ "pcsx_rearmed_dithering", "PS1 colour shading" },
	{ "pcsx_rearmed_frameskip_type", "Skip frames when slow" },
	{ "pcsx_rearmed_display_info", "Debug: core messages" },
	{ "pcsx_rearmed_alt_flip", "Flicker fix" },
	{ "pcsx_rearmed_gpu_slow_llists", "Safe drawing" },
	{ "pcsx_rearmed_spu_reverb", "Sound echo" },
	{ "pcsx_rearmed_nocdaudio", "Disc music" }, { "pcsx_rearmed_noxadecoding", "Disc voices and effects" },
	{ "pcsx_rearmed_analog_axis_modifier", "Stick response" },
	{ "pcsx_rearmed_analog_combo", "Analog mode shortcut" },
	{ "pcsx_rearmed_icache_emulation", "Accurate CPU cache" },
	{ "pcsx_rearmed_exception_emulation", "Accurate CPU errors" },
	{ "pcsx_rearmed_cd_turbo", "Faster disc loading" },
	{ "pcsx_rearmed_nocompathacks", "Disable automatic game fixes" },
	{ "pcsx_rearmed_nolddelay", "Simplify CPU loads" },
	{ "pcsx_rearmed_nosmccheck", "Skip code safety checks" },
	{ "pcsx_rearmed_gteregsunneeded", "Faster 3D calculations" },
	{ "pcsx_rearmed_nogteflags", "Simpler 3D clipping" },
	{ "pcsx_rearmed_nostalls", "Simpler CPU timing" },
};
const char *options_desc(int i)
{
	const option_t *o = &opts[visible[i]];
	for (size_t j = 0; j < sizeof(player_names) / sizeof(player_names[0]); j++)
		if (!strcmp(o->key, player_names[j][0])) return player_names[j][1];
	return o->desc;
}

// Player-facing labels only; options_get still returns the original core value.
const char *options_value(int i)
{
	const char *v = options_get(options_key(i));
	static const char *const labels[][2] = {
		{ "enabled", "On" }, { "disabled", "Off" }, { "auto", "Auto" },
		{ "manual", "Manual" }, { "game", "Game-controlled" },
		{ "borderless", "No borders" }, { "force", "Always" },
		{ "early", "Early" }, { "late", "Late" },
		{ "hle", "Built-in" }, { "libretro", "Game save" }, { "serial", "Per disc" },
		{ "shared", "Shared" }, { "none", "Off" },
		{ "square", "Square" }, { "circle", "Round" },
		{ "l1+r1+select", "L1+R1+Select" }, { "l1+r1+start", "L1+R1+Start" },
		{ "l1+r1+l3", "L1+R1+L3" }, { "l1+r1+r3", "L1+R1+R3" }, { "l3+r3", "L3+R3" },
	};
	if (!v) return "";
	for (size_t j = 0; j < sizeof(labels) / sizeof(labels[0]); j++)
		if (!strcmp(v, labels[j][0])) return labels[j][1];
	return v;
}

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

	if (config_in_game())
		gamecfg_set(options_game_key(o->key), o->values[idx]);
	else
		config_set_user_core_override(o->key, o->values[idx]);
	updated = true;
	return o->values[idx];
}

// ---------------------------------------------------------------- help
// What each option does and what it costs, shown on the top screen while
// it's selected. PSX3D's own notes win over the core's text; the impact
// line is ours (measured on 3DS where it says so).
static const char *const notes[][3] = {
	{ "pcsx_rearmed_region", "Usually leave Auto", "Auto detects the disc's region. Choose a region manually only if a game starts with the wrong video timing." },
	{ "pcsx_rearmed_bios", "Reopen the game to apply", "Auto uses an available PlayStation BIOS. The built-in BIOS needs no file, but some games need a real BIOS in the bios folder to boot correctly." },
	{ "pcsx_rearmed_memcard1", "Reopen the game to apply", "Game save uses PSX3D's usual per-game save file. Per disc keeps a separate card for each disc; Shared uses one card across games. Off removes this card." },
	{ "pcsx_rearmed_memcard2", "Reopen the game to apply", "Choose a separate card per disc, a card shared across games, or Off for an empty second memory-card slot." },
	{ "pcsx_rearmed_psxclock", "Lower may slow the game itself", "How fast the game's PS1 CPU runs. Lower values may raise the frame count while slowing the game itself or causing glitches. Usually leave Auto." },
	{ "pcsx_rearmed_frameskip_type", "Auto balances speed and smooth pictures", "Auto skips drawing to help game speed, while targeting a minimum picture rate (15/s by default) when possible. Off draws every picture and can slow busy scenes. Some games supply a preset; Advanced can reveal its setting." },
	{ "pcsx_rearmed_dithering", "Smoother colour gradients", "Adds the PS1's fine colour pattern to shaded polygons. On follows the game; Always also enables it when the game turns it off." },
	{ "pcsx_rearmed_alt_flip", "Usually leave Auto", "Changes when the game's picture is taken for the screen. Try Early or Late only if a game flickers or shows half-drawn pictures." },
	{ "pcsx_rearmed_gpu_slow_llists", "May be slower", "Draws every part of the picture strictly in the game's order. Turn on only if parts of a game are missing or drawn wrongly." },
	{ "pcsx_rearmed_spu_reverb", "Off can improve speed", "Play the PS1's echo effect. Off sounds drier and can help Old 3DS keep up." },
	{ "pcsx_rearmed_nocdaudio", "Usually keep On", "Play music stored as audio tracks on the game disc." },
	{ "pcsx_rearmed_noxadecoding", "Usually keep On", "Play compressed voices, music and effects stored on the game disc." },
	{ "pcsx_rearmed_cd_readahead", "Can reduce loading pauses", "Read this many disc sectors ahead on a spare CPU core. Higher values use more memory. Old 3DS chooses this automatically." },
	{ "pcsx_rearmed_analog_axis_modifier", "Changes stick response", "Choose the shape of the analog stick's movement range. Most games work best with Square." },
	{ "pcsx_rearmed_analog_combo", "Controller shortcut", "Choose the PlayStation button combination that switches between digital and analog controls, or Off to disable the shortcut." },
	{ "pcsx_rearmed_icache_emulation", "More accurate, may be slower", "Emulate the PS1 CPU's instruction cache. Some games need this to run correctly." },
	{ "pcsx_rearmed_exception_emulation", "More accurate, may be slower", "Handle PS1 CPU errors more accurately. Some games need this to run correctly." },
	{ "pcsx_rearmed_cd_turbo", "May break games", "Load data faster than a real PlayStation disc drive. Some games may crash or stop working." },
	{ "pcsx_rearmed_nocompathacks", "Usually keep Off", "Turn off the emulator's automatic game fixes. Some games need those fixes to work, so normally leave this Off." },
	{ "pcsx_rearmed_nolddelay", "May break games", "Simplify the timing of PS1 memory loads. Only useful for games modified to expect an older emulator." },
	{ "pcsx_rearmed_nosmccheck", "May crash games or break saves", "Skip checks for games that change their own running code. May improve speed, but unsafe for games that need those checks." },
	{ "pcsx_rearmed_gteregsunneeded", "Faster, may cause glitches", "Skip some 3D calculation results that games rarely use. Turn off if a game draws incorrectly." },
	{ "pcsx_rearmed_nogteflags", "Faster, may cause glitches", "Simplify checks used when drawing 3D objects. Turn off if parts of a game disappear or draw incorrectly." },
	{ "pcsx_rearmed_nostalls", "Faster, may break games", "Simplify PS1 CPU timing. Some games need the original timing to work correctly." },
	{ "pcsx_rearmed_display_info", "Debug only", "Show extra messages from the emulator for troubleshooting." },
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

// Full effective option manifest, including hidden core options. Clip replay
// restores determinism options; presentation options remain benchmark choices.
bool options_clip_snapshot(char *out, size_t size)
{
    size_t used = 0;
    out[0] = 0;
    for (int i = 0; i < nopts; i++) {
        const char *value = options_get(opts[i].key);
        if (!value) continue;
        int n = snprintf(out + used, size - used, "%s=%s\n", opts[i].key, value);
        if (n < 0 || (size_t)n >= size - used) return false;
        used += n;
    }
    return true;
}
