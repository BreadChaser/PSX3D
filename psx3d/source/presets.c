// Curated per-game data: one INI file per game in romfs:/presets (built in),
// optionally newer files in sdmc:/3ds/PSX3D/presets (same format, own VERSION).
// All files are parsed once at startup into a serial index; disc identification
// is a lookup, never in the frame loop. Format: docs/presets.md.
#include "psx3d.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BUILTIN, SD };
static const char *preset_dirs[2];
static int pack[2];   // VERSION of each pack; 0 = not used
typedef struct file { struct file *next; game_preset_t preset; int layer, version; char name[]; } file_t;
static file_t *preset_files;
typedef struct { char serial[10]; file_t *file[2]; } entry_t;
static entry_t *index_;
static int nindex;
static const game_preset_t *current_preset;
static const file_t *current_file;
// game_capabilities() result, recomputed when the preset or console changes.
static game_preset_options_t cached;
static const game_preset_t *cached_preset;
static int cached_model = -1;
#ifdef PSX3D_PS1_OVERCLOCK_PRESETS
static bool core_clock; // the core set this game's clock itself
#endif

// Named keys in game_preset_options_t field order. Value words count from 0
// (touch slots from -1 = hide); psxclock takes a number.
#define BINDS "hide|b|y|select|start|up|down|left|right|a|x|l1|r1|l2|r2|l3|r3|ff|ff_hold|rewind"
static const char *const preset_keys[][2] = {
	{ "fast_forward", "no|yes" }, { "rewind", "no|yes" }, { "touch_controls", "auto|on|off" },
	{ "l2r2", "no|yes" }, { "l3r3", "no|yes|analog" }, { "stick", "no|yes|analog" },
	{ "touch_l2", BINDS }, { "touch_r2", BINDS }, { "touch_l3", BINDS },
	{ "touch_r3", BINDS }, { "touch_x1", BINDS }, { "touch_x2", BINDS },
	{ "frameskip", "off|auto" }, { "widescreen", "hidden|on|warn|off" }, { "psxclock", NULL },
};
#define NKEYS (sizeof(preset_keys) / sizeof(preset_keys[0]))
_Static_assert(NKEYS * sizeof(int) == sizeof(game_preset_options_t), "keys follow the struct");

static int preset_word(const char *list, const char *v)
{
	for (int i = 0; *list; i++) {
		size_t n = strcspn(list, "|");
		if (n == strlen(v) && !strncmp(list, v, n))
			return i;
		list += n + (list[n] == '|');
	}
	return -1;
}
static int preset_number(const char *v)
{
	if (!*v || strspn(v, "0123456789") != strlen(v) || strlen(v) > 6)
		return -1;
	return atoi(v);
}
static char *preset_trim(char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	for (char *e = s + strlen(s); e > s && isspace((unsigned char)e[-1]);)
		*--e = 0;
	return s;
}

static void index_add(const char *serial, file_t *file)
{
	entry_t *e = NULL;
	for (int i = 0; i < nindex; i++)
		if (!strcmp(index_[i].serial, serial))
			e = &index_[i];
	if (e && e->file[file->layer]) {
		log_printf("presets: %s listed twice, kept %s\n", serial, e->file[file->layer]->name);
		return;
	}
	if (!e) {
		entry_t *grown = realloc(index_, (nindex + 1) * sizeof(*index_));
		if (!grown)
			return;
		index_ = grown;
		e = &index_[nindex++];
		memset(e, 0, sizeof(*e));
		strcpy(e->serial, serial);
	}
	e->file[file->layer] = file;
}

// Parses one file into the index. A bad built-in line is logged and ignored;
// any bad line rejects an SD file, so the game's built-in file is used instead.
static void read_preset(int layer, const char *name)
{
	char path[PATH_LEN], line[256], serials[256] = "";
	snprintf(path, sizeof(path), "%s/%s", preset_dirs[layer], name);
	FILE *f = fopen(path, "r");
	file_t *file = f ? calloc(1, sizeof(*file) + strlen(name) + 1) : NULL;
	if (!file) {
		if (f)
			fclose(f);
		return;
	}
	game_preset_t *p = &file->preset;
	int *opt[2] = { (int *)&p->old3ds, (int *)&p->new3ds };
	for (unsigned k = 0; k < NKEYS; k++)
		opt[0][k] = opt[1][k] = k >= 6 && k < 12 ? INPUT_BIND_PRESET : -1;
	int section = 0, version = 0; // section 0 common, 1 [old], 2 [new]
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, ";#\r\n")] = 0;
		char *s = preset_trim(line), *v = strchr(s, '=');
		int value = -1;
		unsigned k = NKEYS;
		if (!*s)
			continue;
		if (*s == '[')
			value = section = preset_word("[common]|[old]|[new]", s);
		else if (v) {
			*v = 0;
			s = preset_trim(s);
			v = preset_trim(v + 1);
			for (k = 0; k < NKEYS && strcmp(s, preset_keys[k][0]); k++)
				;
			if (k < NKEYS)
				value = preset_keys[k][1] ? preset_word(preset_keys[k][1], v) - (k >= 6 && k < 12) : preset_number(v);
			else if (section)
				;
			else if (!strcmp(s, "title"))
				value = 0;
			else if (!strcmp(s, "version"))
				value = version = preset_number(v);
			else if (!strcmp(s, "serials"))
				value = snprintf(serials, sizeof(serials), "%s", v) < (int)sizeof(serials) ? 0 : -1;
			else if (!strcmp(s, "curated"))
				p->curated = (value = preset_word("no|yes", v)) == 1;
		}
		if (k == 14 && (value < 30 || value > 100))
			value = -1;
		if (value < (k >= 6 && k < 12 ? -1 : 0) || section < 0) {
			log_printf("presets: %s: bad line '%s'\n", path, s);
			if (layer == SD)
				break;
			continue;
		}
		for (int c = 0; k < NKEYS && c < 2; c++)
			if (section != 2 - c)
				opt[c][k] = value;
	}
	bool ok = !ferror(f) && (layer == BUILTIN || feof(f)) && version > 0 && version <= pack[layer] && serials[0];
	fclose(f);
	if (!ok) {
		log_printf("presets: %s unusable\n", path);
		free(file);
		return;
	}
	file->layer = layer;
	file->version = version;
	strcpy(file->name, name);
	file->next = preset_files;
	preset_files = file;
	for (char *ctx, *s = strtok_r(serials, ",", &ctx); s; s = strtok_r(NULL, ",", &ctx))
		if (strlen(s = preset_trim(s)) == 9)
			index_add(s, file);
		else
			log_printf("presets: %s: bad serial '%s'\n", path, s);
}

static void scan_pack(int layer)
{
	char path[PATH_LEN], line[16] = "";
	snprintf(path, sizeof(path), "%s/VERSION", preset_dirs[layer]);
	FILE *f = fopen(path, "r");
	if (f) {
		if (!fgets(line, sizeof(line), f))
			line[0] = 0;
		fclose(f);
	}
	pack[layer] = preset_number(preset_trim(line));
	// An SD pack older than the built-in one predates this app: ignore it all.
	if (pack[layer] <= 0 || (layer == SD && pack[SD] < pack[BUILTIN])) {
		pack[layer] = 0;
		return;
	}
	DIR *d = opendir(preset_dirs[layer]);
	struct dirent *de;
	while (d && (de = readdir(d))) {
		size_t n = strlen(de->d_name);
		if (n > 4 && !strcmp(de->d_name + n - 4, ".ini"))
			read_preset(layer, de->d_name);
	}
	if (d)
		closedir(d);
}

// Once at startup: every file is parsed into the serial index here, so disc
// identification is a lookup.
void game_presets_init(const char *builtin, const char *sd)
{
	while (preset_files) {
		file_t *next = preset_files->next;
		free(preset_files);
		preset_files = next;
	}
	free(index_);
	index_ = NULL;
	nindex = 0;
	preset_dirs[BUILTIN] = builtin;
	preset_dirs[SD] = sd;
	scan_pack(BUILTIN);
	scan_pack(SD);
	game_presets_reset();
	log_printf("presets: built-in pack %d, SD pack %d, %d serials\n", pack[BUILTIN], pack[SD], nindex);
}
int game_presets_pack(void) { return pack[SD] > pack[BUILTIN] ? pack[SD] : pack[BUILTIN]; }
int game_preset_version(void) { return current_file ? current_file->version : 0; }
const char *game_preset_source(void) { return !current_file ? "none" : current_file->layer ? "sd" : "builtin"; }

bool game_preset_curated(void) { return current_preset && current_preset->curated; }
void game_presets_reset(void)
{
	current_preset = NULL;
	current_file = NULL;
	cached_model = -1;
#ifdef PSX3D_PS1_OVERCLOCK_PRESETS
	core_clock = false;
#endif
}
game_preset_options_t game_capabilities(void)
{
	if (cached_model == g_new3ds && cached_preset == current_preset)
		return cached;
	// Uncurated games expose every available feature; stick clicks and the
	// touch stick appear when the game switches the pad to analog.
	game_preset_options_t result = {
		.frameskip = 1,
		.fast_forward = 1,
		.rewind = g_new3ds,
		.touch_controls = TOUCH_CONTROLS_AUTO,
		.needs_l2r2 = !g_new3ds,
		.needs_l3r3 = NEEDS_ANALOG,
		.needs_right_stick = g_new3ds ? 0 : NEEDS_ANALOG,
		.touch_bindings = {-2, -2, -2, -2, -2, -2},
		.widescreen = 1, // offered when the game has a widescreen fix
	};
	if (current_preset) {
		// Set values replace defaults; touch slots inherit at -2, the rest at -1.
		const int *p = (const int *)(g_new3ds ? &current_preset->new3ds : &current_preset->old3ds);
		for (unsigned k = 0; k < NKEYS; k++)
			if (p[k] >= (k >= 6 && k < 12 ? INPUT_BIND_HIDE : 0))
				((int *)&result)[k] = p[k];
	}
	// The current rewind engine requires memory and a New 3DS worker core.
	// Presets cannot enable an engine that this console cannot run.
	if (!g_new3ds)
		result.rewind = 0;
#ifdef PSX3D_PS1_OVERCLOCK_PRESETS
	if (core_clock)
		result.psxclock = 0;
#endif
	cached = result;
	cached_preset = current_preset;
	cached_model = g_new3ds;
	return result;
}

void game_presets_disc_log(const char *message)
{
	static const char prefix[] = "CD-ROM ID: ";
	if (strncmp(message, prefix, sizeof(prefix) - 1))
		return;
	const char *p = message + sizeof(prefix) - 1;
	char serial[10];
	int n = 0;
	bool valid = true;
	for (; *p && *p != '\n' && *p != '\r'; p++) {
		unsigned char c = (unsigned char)*p;
		if (c == '-' || c == '_' || c == '.')
			continue;
		if (n == 9 || (n < 4 ? !isalpha(c) : !isdigit(c))) {
			valid = false;
			break;
		}
		serial[n++] = toupper(c);
	}
	serial[n] = 0;
	game_presets_reset();
	for (int i = 0; valid && n == 9 && i < nindex; i++) {
		file_t *sd = index_[i].file[SD], *use = index_[i].file[BUILTIN];
		if (strcmp(serial, index_[i].serial))
			continue;
		// The higher file version wins; equal versions keep the built-in file.
		if (sd && (!use || sd->version > use->version))
			use = sd;
		use->preset.serial = index_[i].serial;
		current_preset = &use->preset;
		current_file = use;
		log_printf("preset: %s %s/%s version %d\n", serial, preset_dirs[use->layer], use->name, use->version);
	}
	if (!current_preset)
		log_printf("preset: %s none\n", serial);
	cheats_disc_changed();
}

#ifdef PSX3D_PS1_OVERCLOCK_PRESETS
// The core logs its own per-game clock fix right after the disc ID (Musashi,
// Digimon, Legaia...). An explicit psxclock would replace it, so a preset clock
// never applies to those games (tools/presets/check.py also rejects them).
void game_presets_core_log(const char *message)
{
	if (strncmp(message, "using cycle_multiplier_override", 31))
		return;
	core_clock = true;
	cached_model = -1;
	options_mark_updated();
	log_printf("preset: psxclock ignored, the core sets this game's clock\n");
}
#endif
