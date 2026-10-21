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
static unsigned scanned[2], accepted[2];   // .ini files read / published per layer
typedef struct file { struct file *next; game_preset_t preset; int layer, version; char *evidence[2][32]; char name[]; } file_t;
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
#define PRESET_KEY(key, words, lo, hi) { #key, words, lo, hi },
static const struct { const char *name, *words; int lo, hi; } preset_keys[] = {
#include "preset_schema.def"
};
#undef PRESET_KEY
#define NKEYS (sizeof(preset_keys) / sizeof(preset_keys[0]))
_Static_assert(NKEYS * sizeof(int) == sizeof(game_preset_options_t), "keys follow the struct");
_Static_assert(NKEYS <= 32, "file_t evidence has room for every key");
// Evidence text only for the keys a file sets (most set one or two).
static void file_free(file_t *file)
{
	for (int c = 0; c < 2; c++)
		for (unsigned k = 0; k < NKEYS; k++)
			free(file->evidence[c][k]);
	free(file);
}
enum { MAX_FILES = 256, MAX_SERIALS = 2048, MAX_LINE = 256, MAX_LINES = 512 };

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
		// scan_pack sorts filenames: lexical first wins, independently of readdir.
		log_printf("presets: conflict %s: kept %s, ignored %s\n", serial, e->file[file->layer]->name, file->name);
		return;
	}
	if (!e) {
		e = &index_[nindex++];
		memset(e, 0, sizeof(*e));
		strcpy(e->serial, serial);
	}
	e->file[file->layer] = file;
}

// Validate a complete bounded file before adding any serial to the index.
static bool valid_serial(const char *s)
{
	if (strlen(s) != 9) return false;
	for (int i = 0; i < 9; i++)
		if (i < 4 ? s[i] < 'A' || s[i] > 'Z' : s[i] < '0' || s[i] > '9') return false;
	return true;
}
// Return one complete ASCII line, including newline in the 255-byte bound.
static int preset_line(FILE *f, char line[MAX_LINE])
{
	unsigned n = 0; int c;
	while ((c = fgetc(f)) != EOF) {
		if (!c || c > 127 || n == MAX_LINE-1) return -1;
		line[n++] = c;
		if (c == '\n') break;
	}
	line[n] = 0;
	return ferror(f) ? -1 : n ? 1 : 0;
}
static void read_preset(int layer, const char *name)
{
	char path[PATH_LEN], line[MAX_LINE], serials[MAX_LINE] = "";
	snprintf(path, sizeof(path), "%s/%s", preset_dirs[layer], name);
	FILE *f = fopen(path, "r");
	file_t *file = f ? calloc(1, sizeof(*file) + strlen(name) + 1) : NULL;
	if (!file) { if (f) fclose(f); return; }
	int *opt[2] = { (int *)&file->preset.old3ds, (int *)&file->preset.new3ds };
	for (unsigned k = 0; k < NKEYS; k++) opt[0][k] = opt[1][k] = k >= 6 && k < 12 ? -2 : -1;
	unsigned seen[3][NKEYS] = {{0}}, meta = 0, lines = 0;
	char sources[3][NKEYS][128] = {{{0}}}, confidence[3][NKEYS][16] = {{{0}}};
	int section = 0, version = 0, read;
	int values[3][NKEYS] = {{0}};
	bool ok = true;
	while (ok && (read = preset_line(f, line)) != 0) {
		if (read < 0 || ++lines > MAX_LINES) { ok = false; break; }
		line[strcspn(line, ";#\r\n")] = 0;
		char *key = preset_trim(line), *v = strchr(key, '=');
		if (!*key) continue;
		if (*key == '[') {
			section = preset_word("[common]|[old]|[new]", key);
			if (section < 0) ok = false;
			continue;
		}
		if (!v) { ok = false; break; }
		*v = 0; key = preset_trim(key); v = preset_trim(v + 1);
		char *suffix = strchr(key, '.');
		if (suffix) *suffix++ = 0;
		unsigned k;
		for (k = 0; k < NKEYS && strcmp(key, preset_keys[k].name); k++);
		if (k < NKEYS) {
			unsigned bit = !suffix ? 1 : !strcmp(suffix, "source") ? 2 : !strcmp(suffix, "confidence") ? 4 : 0;
			if (!bit || (seen[section][k] & bit) || !*v) { ok = false; break; }
			seen[section][k] |= bit;
			if (bit == 2) {
				if (strlen(v) >= sizeof(sources[0][0])) { ok = false; break; }
				strcpy(sources[section][k], v);
			} else if (bit == 4) {
				if (preset_word("high|medium|low|unassessed", v) < 0) { ok = false; break; }
				strcpy(confidence[section][k], v);
			} else {
				int value = *preset_keys[k].words ? preset_word(preset_keys[k].words, v) - (k >= 6 && k < 12) : preset_number(v);
				if (value < preset_keys[k].lo || value > preset_keys[k].hi) { ok = false; break; }
				if (k == 1 && value == 1 && section != 2) { ok = false; break; }
				values[section][k] = value;
			}
		} else {
			int m = preset_word("title|version|serials|curated", key);
			if (suffix || section || m < 0 || (meta & (1u << m)) || !*v) { ok = false; break; }
			meta |= 1u << m;
			if (m == 1) version = *v == '0' ? -1 : preset_number(v);
			if (m == 2) strcpy(serials, v);
			if (m == 3) {
				int value = preset_word("no|yes", v);
				if (value < 0) { ok = false; break; }
				file->preset.curated = value;
			}
		}
	}
	ok = ok && !ferror(f) && (meta & 7) == 7 && version > 0 && version <= pack[layer];
	fclose(f);
	for (unsigned k = 0; ok && k < NKEYS; k++) {
		for (int sec = 0; sec < 3; sec++)
			if (seen[sec][k] && seen[sec][k] != 7) ok = false;
		for (int c = 0; c < 2; c++) {
			int sec = seen[c+1][k] ? c+1 : 0;
			// Fast forward (key 0) is always allowed (docs/settings.md): an
			// older SD pack may still say no; the value is checked and dropped.
			if (seen[sec][k] && k) opt[c][k] = values[sec][k];
			if (seen[sec][k]) {
				char text[160];
				snprintf(text, sizeof(text), "%s: %s", confidence[sec][k], sources[sec][k]);
				if (!(file->evidence[c][k] = strdup(text))) ok = false;
			}
		}
	}
	char ids[26][10]; unsigned n = 0;
	char *s = serials;
	while (ok) {
		char *comma = strchr(s, ','); if (comma) *comma = 0;
		s = preset_trim(s);
		if (n == 26 || !valid_serial(s)) { ok = false; break; }
		for (unsigned i = 0; i < n; i++) if (!strcmp(ids[i], s)) ok = false;
		strcpy(ids[n++], s);
		if (!comma) break;
		s = comma + 1;
	}
	// A cross-file duplicate rejects the entire later file, including its other IDs.
	// Count only new serials: replacing a built-in entry consumes no index slot.
	unsigned additional = 0;
	for (unsigned j = 0; ok && j < n; j++) {
		bool existing = false;
		for (int i = 0; i < nindex; i++) if (!strcmp(ids[j], index_[i].serial)) {
			existing = true;
			if (index_[i].file[layer]) ok = false;
			break;
		}
		additional += !existing;
	}
	if (!ok || nindex + additional > MAX_SERIALS) {
		log_printf("presets: rejected %s (invalid metadata, option, evidence, serial or bounds)\n", path);
		file_free(file); return;
	}
	// Reserve all new index slots before publishing any serial from this file.
	if (additional) {
		entry_t *grown = realloc(index_, (nindex+additional) * sizeof(*index_));
		if (!grown) { file_free(file); return; }
		index_ = grown;
	}
	accepted[layer]++;
	file->layer = layer; file->version = version; strcpy(file->name, name);
	file->next = preset_files; preset_files = file;
	for (unsigned i = 0; i < n; i++) index_add(ids[i], file);
}
static int name_compare(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void scan_pack(int layer)
{
	char path[PATH_LEN], line[MAX_LINE] = "";
	snprintf(path, sizeof(path), "%s/VERSION", preset_dirs[layer]);
	FILE *f = fopen(path, "r");
	bool ok = f && preset_line(f, line) > 0;
	if (ok) { int c = fgetc(f); ok = c == EOF && !ferror(f); }
	if (f) fclose(f);
	pack[layer] = ok && *preset_trim(line) != '0' ? preset_number(preset_trim(line)) : 0;
	if (pack[layer] <= 0 || (layer == SD && pack[SD] < pack[BUILTIN])) { pack[layer] = 0; return; }
	char *names[MAX_FILES]; unsigned n = 0;
	DIR *d = opendir(preset_dirs[layer]); struct dirent *de;
	while (d && (de = readdir(d))) {
		size_t len = strlen(de->d_name);
		if (len <= 4 || strcmp(de->d_name + len - 4, ".ini") || !strcmp(de->d_name, "defaults.ini")) continue;
		if (n == MAX_FILES) { ok = false; break; }
		names[n] = strdup(de->d_name);
		if (!names[n]) { ok = false; break; }
		n++;
	}
	if (d) closedir(d);
	if (!ok) { pack[layer] = 0; log_printf("presets: pack exceeds file/memory limit\n"); }
	qsort(names, n, sizeof(*names), name_compare);
	for (unsigned i = 0; i < n; i++) { if (ok) { scanned[layer]++; read_preset(layer, names[i]); } free(names[i]); }
}

// Once at startup: every file is parsed into the serial index here, so disc
// identification is a lookup.
void game_presets_init(const char *builtin, const char *sd)
{
	while (preset_files) {
		file_t *next = preset_files->next;
		file_free(preset_files);
		preset_files = next;
	}
	free(index_);
	index_ = NULL;
	nindex = 0;
	scanned[0] = scanned[1] = accepted[0] = accepted[1] = 0;
	preset_dirs[BUILTIN] = builtin;
	preset_dirs[SD] = sd;
	char path[PATH_LEN];
	snprintf(path, sizeof(path), "%s/defaults.ini", builtin);   // built in only, never from SD
	defaults_init(path);
	scan_pack(BUILTIN);
	scan_pack(SD);
	game_presets_reset();
	log_printf("presets: built-in pack %d, SD pack %d, %d serials\n", pack[BUILTIN], pack[SD], nindex);
}
// A downloaded pack (presetupdate.c): load it as the SD layer exactly as the
// next start would and require every one of its files to be accepted. The
// caller re-runs game_presets_init with the real SD folder afterwards.
bool game_presets_validate(const char *builtin, const char *dir, int version, unsigned files)
{
	game_presets_init(builtin, dir);
	bool ok = pack[SD] == version && scanned[SD] == files && accepted[SD] == files;
	log_printf("presets: check %s: pack %d (want %d), %u/%u files accepted (want %u)\n",
		dir, pack[SD], version, accepted[SD], scanned[SD], files);
	return ok;
}
// After the validated folder was renamed into place: same files, new path.
void game_presets_moved(const char *sd) { preset_dirs[SD] = sd; }
int game_presets_pack(void) { return pack[SD] > pack[BUILTIN] ? pack[SD] : pack[BUILTIN]; }
int game_preset_version(void) { return current_file ? current_file->version : 0; }
const char *game_preset_source(void) { return !current_file ? "none" : current_file->layer ? "sd" : "builtin"; }

static int key_index(const char *key)
{
	for (unsigned k = 0; k < NKEYS; k++) if (!strcmp(key, preset_keys[k].name)) return k;
	return -1;
}
bool game_preset_sets(const char *key)
{
	int k = key_index(key);
	if (!current_preset || k < 0) return false;
	const int *p = (const int *)(g_new3ds ? &current_preset->new3ds : &current_preset->old3ds);
	return p[k] >= (k >= 6 && k < 12 ? -1 : 0);
}
const char *game_preset_evidence(const char *key)
{
	int k = key_index(key);
	const char *e = current_file && k >= 0 ? current_file->evidence[g_new3ds][k] : NULL;
	return e ? e : "";
}
int game_auto_floor(void)
{
	for (int i = 0; i < 8 && sw_opt[i][0]; i++)
		if (!strncmp(sw_opt[i], "auto_floor=", 11)) {
			int n = preset_number(sw_opt[i]+11); if (n >= 1 && n <= 60) return n;
		}
	// No menu row: a tested preset value or the forced default (defaults.ini).
	int n = game_capabilities().auto_floor;
	return n >= 1 && n <= 60 ? n : 15;
}
bool game_preset_tested(void)
{
	int n = game_capabilities().tested_with;
	return current_preset && n > 0 && n <= defaults_version();
}
// Technical core options a preset can set, by schema key (core key minus prefix).
static int tech_key(const char *core_key)
{
	if (strncmp(core_key, "pcsx_rearmed_", 13)) return -1;
	int k = key_index(core_key + 13);
	return k >= key_index("alt_flip") && k <= key_index("nocompathacks") ? k : -1;
}
const char *game_preset_core_value(const char *core_key)
{
	int k = tech_key(core_key);
	if (k < 0 || !current_preset) return NULL;
	int v = ((const int *)(g_new3ds ? &current_preset->new3ds : &current_preset->old3ds))[k];
	if (v < 0) return NULL;
	const char *w = preset_keys[k].words;
	for (int i = 0; i < v; i++) w += strcspn(w, "|") + 1;
	size_t n = strcspn(w, "|");
	if (n == 2 && !strncmp(w, "on", 2)) return "enabled";
	if (n == 3 && !strncmp(w, "off", 3)) return "disabled";
	static char word[16];
	snprintf(word, sizeof(word), "%.*s", (int)n, w);
	return word;
}
bool game_preset_curated(void) { return current_preset && current_preset->curated; }
void game_presets_invalidate(void) { cached_model = -1; }
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
		.auto_floor = 15,
		.fast_forward = 1,
		.rewind = g_new3ds,
		.touch_controls = TOUCH_CONTROLS_AUTO,
		.needs_l2r2 = !g_new3ds,
		.needs_l3r3 = NEEDS_ANALOG,
		.needs_right_stick = g_new3ds ? 0 : NEEDS_ANALOG,
		.touch_bindings = {-2, -2, -2, -2, -2, -2},
		.widescreen = 1, // offered when the game has a widescreen fix
	};
	for (int *f = &result.controller; f <= &result.tested_with; f++) *f = -1;
	result.auto_floor = defaults_word("auto_floor", "", 15);
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
	if (g_new3ds && config_in_game() && config_rewind_on()) result.rewind = 1;
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

// ---------------------------------------------------------------- defaults.ini
// Startup only. Sections [old] [new] [forced.old] [forced.new]; values are words.
enum { DEF_KEYS = 96, DEF_WORD = 24 };
static struct { char key[DEF_WORD], value[4][DEF_WORD]; } defs[DEF_KEYS];
static int ndefs, defs_version;
static bool defs_loaded;
static char defs_path[PATH_LEN] = "romfs:/presets/defaults.ini";
void defaults_init(const char *path)
{
	snprintf(defs_path, sizeof(defs_path), "%s", path);
	ndefs = defs_version = 0;
	memset(defs, 0, sizeof(defs));
	defs_loaded = true;
	FILE *f = fopen(defs_path, "r");
	char line[MAX_LINE];
	int section = -1, read, lines = 0;
	bool ok = f != NULL;
	while (ok && (read = preset_line(f, line)) != 0) {
		if (read < 0 || ++lines > MAX_LINES) { ok = false; break; }
		line[strcspn(line, ";#\r\n")] = 0;
		char *key = preset_trim(line), *v = strchr(key, '=');
		if (!*key) continue;
		if (*key == '[') {
			section = preset_word("[old]|[new]|[forced.old]|[forced.new]", key);
			if (section < 0) ok = false;
			continue;
		}
		if (!v) { ok = false; break; }
		*v = 0; key = preset_trim(key); v = preset_trim(v + 1);
		if (strlen(key) >= DEF_WORD || strlen(v) >= DEF_WORD || !*v) { ok = false; break; }
		if (section < 0) {
			if (strcmp(key, "defaults_version") || defs_version) { ok = false; break; }
			defs_version = preset_number(v);
			continue;
		}
		int i = 0;
		while (i < ndefs && strcmp(defs[i].key, key)) i++;
		if (i == ndefs) {
			if (ndefs == DEF_KEYS) { ok = false; break; }
			strcpy(defs[ndefs++].key, key);
		}
		if (defs[i].value[section][0]) { ok = false; break; }
		strcpy(defs[i].value[section], v);
	}
	if (f) fclose(f);
	if (!ok || defs_version <= 0) {
		log_printf("defaults: %s unusable, core defaults apply\n", defs_path);
		ndefs = defs_version = 0;
	}
}
static const char *def_value(const char *key, int section)
{
	if (!defs_loaded) defaults_init(defs_path);
	for (int i = 0; i < ndefs; i++)
		if (!strcmp(defs[i].key, key))
			return defs[i].value[section + g_new3ds][0] ? defs[i].value[section + g_new3ds] : NULL;
	return NULL;
}
int defaults_version(void) { if (!defs_loaded) defaults_init(defs_path); return defs_version; }
const char *defaults_get(const char *key) { return def_value(key, 0); }
const char *defaults_forced(const char *key) { return def_value(key, 2); }
const char *defaults_core(const char *core_key, bool *forced)
{
	const char *k = !strncmp(core_key, "pcsx_rearmed_", 13) ? core_key + 13 : core_key;
	if (!strcmp(k, "frameskip_type")) k = "frameskip";
	const char *v = defaults_forced(k);
	*forced = v != NULL;
	if (!v) v = defaults_get(k);
	if (!v) return NULL;
	return !strcmp(v, "on") ? "enabled" : !strcmp(v, "off") ? "disabled" : v;
}
// Index of this console's default word in "a|b|c" (a number when words is "").
int defaults_word(const char *key, const char *words, int fallback)
{
	const char *v = defaults_forced(key);
	if (!v) v = defaults_get(key);
	if (!v) return fallback;
	int n = *words ? preset_word(words, v) : preset_number(v);
	return n < 0 ? fallback : n;
}
