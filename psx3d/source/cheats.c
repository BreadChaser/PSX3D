// Cheats. Codes come from the libretro cheat database bundled in romfs
// (romfs:/cheats, CC BY-SA 4.0, see tools/art/CREDITS.md) or from the user's
// own sdmc:/3ds/PSX3D/cheats/<game>.cht, which wins. Files use the RetroArch
// .cht format. The cheats switched on are remembered per game in
// sdmc:/3ds/PSX3D/cheats/<game>.on (one description per line).
#define _GNU_SOURCE   // strcasestr
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "psx3d.h"
#include "safefile.h"
#include "widescreen.h"
extern char CdromId[10];
extern uint32_t CdromExeHash;
extern void (*psx3d_widescreen_hook)(void);
extern void (*psx3d_widescreen_state_hook)(void);
extern void psx3d_patch_clear(uint32_t address, unsigned bytes);
void *ws_memory(void) { return retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM); }
void ws_invalidate(uint32_t a, unsigned n) { psx3d_patch_clear(a, n); }

#define USER_DIR PSX3D_DIR "/cheats"
#define DB_DIR   "romfs:/cheats"
#define MAX_CHEATS 256

typedef struct { char *desc, *code; bool on, forced; } cheat_t;

static cheat_t cheats[MAX_CHEATS];
static int ncheats;
static char game[PATH_LEN];

int cheats_count(void) { return ncheats; }
const char *cheats_desc(int i) { return i >= 0 && i < ncheats ? cheats[i].desc : ""; }
bool cheats_forced(int i) { return i >= 0 && i < ncheats && cheats[i].forced; }
bool cheats_on(int i) { return i >= 0 && i < ncheats && cheats[i].on; }

// "Tekken 3 (World) (GameShark).cht" -> "Tekken 3"
static void base_title(const char *name, char *out, size_t len)
{
/* Bounded truncation is intentional; preserve the validated release code layout. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
	snprintf(out, len, "%s", name);
#pragma GCC diagnostic pop
	char *p = strstr(out, " (");
	if (p) *p = 0;
	if ((p = strstr(out, ".cht"))) *p = 0;
}

// the first "(...)" of a name: the region, e.g. "USA"
static void region_of(const char *name, char *out, size_t len)
{
	out[0] = 0;
	const char *a = strchr(name, '('), *b = a ? strchr(a, ')') : NULL;
	if (a && b)
/* Bounded truncation is intentional; preserve the validated release code layout. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
		snprintf(out, len, "%.*s", (int)(b - a - 1), a + 1);
#pragma GCC diagnostic pop
}

// how well a database file fits the game, 0 = not at all
static int score(const char *file, const char *key)
{
	char ft[PATH_LEN], kt[PATH_LEN], fr[64], kr[64], exact[PATH_LEN];
	snprintf(exact, sizeof(exact), "%s.cht", key);
	if (!strcasecmp(file, exact))
		return 100;
	base_title(file, ft, sizeof(ft));
	base_title(key, kt, sizeof(kt));
	if (strcasecmp(ft, kt))
		return 0;
	region_of(file, fr, sizeof(fr));
	region_of(key, kr, sizeof(kr));
	int s = 10;
	if (kr[0] && strstr(fr, kr))
		s = 60;   // same region ("USA" in "USA, Europe")
	else if (strstr(fr, "World"))
		s = 40;
	if (strstr(file, "(diff)"))
		s -= 5;   // a variant list: prefer the main one
	// Game Buster was a German device: its "World" lists carry PAL codes,
	// which write to the wrong places on a USA or Japanese disc
	if (strstr(file, "(Game Buster)") && !strstr(kr, "Europe") && !strstr(kr, "Germany") && !strstr(kr, "France"))
		s = s >= 40 ? 15 : s;
	return s;
}

static char *unquote(char *v)
{
	while (*v == ' ' || *v == '=') v++;
	if (*v == '"') v++;
	char *e = v + strlen(v);
	while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '"'))
		*--e = 0;
	return v;
}

// RetroArch .cht: cheatN_desc / cheatN_code / cheatN_enable lines
static void parse(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	char line[1024];
	int base = ncheats;
	while (fgets(line, sizeof(line), f)) {
		int idx;
		char field[16];
		if (sscanf(line, "cheat%d_%15[a-z]", &idx, field) != 2 || idx < 0 || base + idx >= MAX_CHEATS)
			continue;
		char *eq = strchr(line, '=');
		if (!eq)
			continue;
		cheat_t *c = &cheats[base + idx];
		if (!strcmp(field, "desc")) {
			free(c->desc);
			c->desc = strdup(unquote(eq));
		} else if (!strcmp(field, "code")) {
			free(c->code);
			c->code = strdup(unquote(eq));
		}
		if (base + idx + 1 > ncheats)
			ncheats = base + idx + 1;
	}
	fclose(f);
	// drop incomplete entries and templates that need a value typed in ("?")
	int n = base;
	for (int i = base; i < ncheats; i++) {
		cheat_t c = cheats[i];
		if (!c.desc || !c.code || !c.code[0] || strchr(c.code, '?')) {
			free(c.desc);
			free(c.code);
			continue;
		}
		cheats[n++] = c;
	}
	for (int i = n; i < ncheats; i++)
		memset(&cheats[i], 0, sizeof(cheats[i]));
	ncheats = n;
}

// Pull GameShark codes out of free text: pairs of 8 and 4 hex digits
// ("8001EDDE 241B", "8001EDDE:241B", "8001EDDE241B"), joined with '+'.
// Returns false when there are none.
static bool normalize_codes(const char *in, char *out, size_t len)
{
	char hex[1024];
	size_t n = 0, o = 0;
	for (const char *p = in; *p && n < sizeof(hex) - 1; p++)
		if (isxdigit((unsigned char)*p))
			hex[n++] = (char)toupper((unsigned char)*p);
		else if (n % 12 && strchr(" :+-\t\r\n", *p) == NULL)
			return false;   // something that isn't a code
	hex[n] = 0;
	if (!n || n % 12)
		return false;
	out[0] = 0;
	for (size_t i = 0; i < n; i += 12) {
		if (o + 14 >= len)
			return false;
		o += snprintf(out + o, len - o, "%s%.8s %.4s", o ? "+" : "", hex + i, hex + i + 8);
	}
	return true;
}

// Plain text: a name on its own line, then its code lines under it
static void parse_plain(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	char line[256], codes[1024] = "", name[128] = "";
	for (;;) {
		bool more = fgets(line, sizeof(line), f) != NULL;
		char norm[256];
		if (more) {
			line[strcspn(line, "\r\n")] = 0;
			if (!line[0])
				continue;
			if (normalize_codes(line, norm, sizeof(norm))) {
				size_t l = strlen(codes);
				snprintf(codes + l, sizeof(codes) - l, "%s%s", l ? "+" : "", norm);
				continue;
			}
		}
		// a new name (or the end): finish the cheat before it
		if (name[0] && codes[0] && ncheats < MAX_CHEATS) {
			cheats[ncheats].desc = strdup(name);
			cheats[ncheats].code = strdup(codes);
			ncheats++;
		}
		if (!more)
			break;
/* Bounded truncation is intentional; preserve the validated release code layout. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
		snprintf(name, sizeof(name), "%s", line);
#pragma GCC diagnostic pop
		codes[0] = 0;
	}
	fclose(f);
}

static void apply(void)
{
	retro_cheat_reset();
	int k = 0;
	for (int i = 0; i < ncheats; i++)
		if (cheats[i].on || cheats[i].forced)
			retro_cheat_set(k++, true, cheats[i].code);
	if (k)
		log_printf("cheats: %d on\n", k);
}

static void on_path(char *out, size_t len)
{
/* Bounded truncation is intentional; preserve the validated release code layout. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
	snprintf(out, len, USER_DIR "/%s.on", game);
#pragma GCC diagnostic pop
}

static void save_on(void)
{
	char path[PATH_LEN];
	mkdir(USER_DIR, 0777);
	on_path(path, sizeof(path));
	FILE *f = safe_begin(path);
	if (!f)
		return;
	for (int i = 0; i < ncheats; i++)
		if (cheats[i].on)
			fprintf(f, "%s\n", cheats[i].desc);
	safe_finish(f, path, true);
}

void cheats_unload(void)
{
	psx3d_widescreen_hook = NULL;
	psx3d_widescreen_state_hook = NULL;
	ws_unload();
	for (int i = 0; i < ncheats; i++) {
		free(cheats[i].desc);
		free(cheats[i].code);
	}
	memset(cheats, 0, sizeof(cheats));
	ncheats = 0;
}

// a game was loaded: find its cheats and switch on the remembered ones
void cheats_load(const char *key)
{
	char path[PATH_LEN];
	cheats_unload();
	snprintf(game, sizeof(game), "%s", key);
	ws_load("romfs:/widescreen", CdromId, CdromExeHash, game_capabilities().widescreen);
	psx3d_widescreen_state_hook = ws_state_loaded;

/* Bounded truncation is intentional; preserve the validated release code layout. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
	snprintf(path, sizeof(path), USER_DIR "/%s.txt", key);   // added on the 3DS or pasted in
#pragma GCC diagnostic pop
	parse_plain(path);
/* Bounded truncation is intentional; preserve the validated release code layout. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
	snprintf(path, sizeof(path), USER_DIR "/%s.cht", key);
#pragma GCC diagnostic pop
	parse(path);

	// every database file that fits (exact name, same region or "World"),
	// best first; the same cheat in two lists is only kept once
	static char files[16][PATH_LEN];
	int fscore[16], nf = 0, best_score = 0;
	DIR *d = opendir(DB_DIR);
	if (d) {
		struct dirent *de;
		while ((de = readdir(d)) && nf < 16) {
			int sc = score(de->d_name, key);
			if (sc > best_score)
				best_score = sc;
			if (sc >= 40) {
				snprintf(files[nf], PATH_LEN, "%s", de->d_name);
				fscore[nf++] = sc;
			}
		}
		closedir(d);
	}
	if (!nf && best_score) {   // only a loose match (other region): use the best one
		d = opendir(DB_DIR);
		struct dirent *de;
		while (d && (de = readdir(d)))
			if (score(de->d_name, key) == best_score && nf < 1) {
				snprintf(files[0], PATH_LEN, "%s", de->d_name);
				fscore[nf++] = best_score;
			}
		if (d)
			closedir(d);
	}
	for (int a = 1; a < nf; a++)
		for (int b = a; b > 0 && fscore[b] > fscore[b - 1]; b--) {
			int t = fscore[b]; fscore[b] = fscore[b - 1]; fscore[b - 1] = t;
			char tmp[PATH_LEN];
			memcpy(tmp, files[b], PATH_LEN); memcpy(files[b], files[b - 1], PATH_LEN); memcpy(files[b - 1], tmp, PATH_LEN);
		}
	for (int k = 0; k < nf; k++) {
		int before = ncheats;
/* Bounded truncation is intentional; preserve the validated release code layout. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
		snprintf(path, sizeof(path), DB_DIR "/%s", files[k]);
#pragma GCC diagnostic pop
		parse(path);
		// drop cheats already listed (same description or same code)
		int n = before;
		for (int i = before; i < ncheats; i++) {
			bool dup = false;
			for (int j = 0; j < before && !dup; j++)
				dup = !strcasecmp(cheats[j].desc, cheats[i].desc) || !strcasecmp(cheats[j].code, cheats[i].code);
			if (dup) {
				free(cheats[i].desc);
				free(cheats[i].code);
			} else {
				cheats[n++] = cheats[i];
			}
		}
		for (int i = n; i < ncheats; i++)
			memset(&cheats[i], 0, sizeof(cheats[i]));
		ncheats = n;
	}
	const char *best = nf ? files[0] : "";
	log_printf("cheats: %d for %s (%d files, best %s)\n", ncheats, key, nf, best[0] ? best : "none");

	on_path(path, sizeof(path));
	safe_recover(path);
	FILE *f = fopen(path, "r");
	if (f) {
		char line[512];
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = 0;
			for (int i = 0; i < ncheats; i++)
				if (!strcmp(cheats[i].desc, line))
					cheats[i].on = true;
		}
		fclose(f);
	}
	apply();
}

// Identification also happens after a disc swap/reset. Retire the old disc's
// bundle before another executable can run, then resolve its exact identity.
void cheats_disc_changed(void)
{
	psx3d_widescreen_hook = NULL;
	psx3d_widescreen_state_hook = NULL;
	ws_load("romfs:/widescreen", CdromId, CdromExeHash, game_capabilities().widescreen);
	psx3d_widescreen_state_hook = ws_state_loaded;
	config_ws_sync();
}

// The Screen setting owns the complete curated bundle, independently of
// editable/user cheats. No name matching and no region guessing.
bool cheats_widescreen(bool on)
{
	bool found = ws_enable(on && sw_wsbundle);
	psx3d_widescreen_hook = on && sw_wsbundle && found ? ws_frame : NULL;
	return sw_wsbundle && found;
}

// "Add code": ask for a name and the codes with the system keyboard, append
// them to the game's .txt and switch the new cheat on. Returns a message.
const char *cheats_add_dialog(void)
{
	static char msg[64];
	char name[64] = "", codes[512] = "", norm[1024];
	SwkbdState kb;
	swkbdInit(&kb, SWKBD_TYPE_QWERTY, 2, 60);
	swkbdSetHintText(&kb, "Cheat name, e.g. Infinite Health");
	if (swkbdInputText(&kb, name, sizeof(name)) != SWKBD_BUTTON_CONFIRM || !name[0])
		return NULL;
	swkbdInit(&kb, SWKBD_TYPE_QWERTY, 2, 500);
	swkbdSetFeatures(&kb, SWKBD_MULTILINE);
	swkbdSetHintText(&kb, "GameShark codes: 8001EDDE 241B");
	if (swkbdInputText(&kb, codes, sizeof(codes)) != SWKBD_BUTTON_CONFIRM)
		return NULL;
	if (!normalize_codes(codes, norm, sizeof(norm))) {
		snprintf(msg, sizeof(msg), "Not a GameShark code (8 + 4 hex digits)");
		return msg;
	}
	char path[PATH_LEN];
	mkdir(USER_DIR, 0777);
/* Bounded truncation is intentional; preserve the validated release code layout. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
	snprintf(path, sizeof(path), USER_DIR "/%s.txt", game);
#pragma GCC diagnostic pop
	FILE *f = fopen(path, "a");
	if (!f) {
		snprintf(msg, sizeof(msg), "Could not save the code");
		return msg;
	}
	fprintf(f, "\n%s\n", name);
	for (char *c = norm; *c; ) {   // one code per line
		char *plus = strchr(c, '+');
		fprintf(f, "%.*s\n", plus ? (int)(plus - c) : (int)strlen(c), c);
		c = plus ? plus + 1 : c + strlen(c);
	}
	fclose(f);
	// reload so the new cheat shows up, keeping what's switched on
	save_on();
	char key[PATH_LEN];
	snprintf(key, sizeof(key), "%s", game);
	cheats_load(key);
	for (int i = 0; i < ncheats; i++)
		if (!strcmp(cheats[i].desc, name) && !cheats[i].on)
			cheats_toggle(i);
	snprintf(msg, sizeof(msg), "Added \"%.40s\"", name);
	return msg;
}

void cheats_toggle(int i)
{
	if (i < 0 || i >= ncheats)
		return;
	cheats[i].on = !cheats[i].on;
	apply();
	save_on();
}
