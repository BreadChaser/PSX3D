// Cheats. Codes come from the libretro cheat database bundled in romfs
// (romfs:/cheats, CC BY-SA 4.0, see tools/art/CREDITS.md) or from the user's
// own sdmc:/3ds/PSX3D/cheats/<game>.cht, which wins. Files use the RetroArch
// .cht format. The cheats switched on are remembered per game in
// sdmc:/3ds/PSX3D/cheats/<game>.on (one description per line).
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "psx3d.h"

#define USER_DIR PSX3D_DIR "/cheats"
#define DB_DIR   "romfs:/cheats"
#define MAX_CHEATS 256

typedef struct { char *desc, *code; bool on; } cheat_t;

static cheat_t cheats[MAX_CHEATS];
static int ncheats;
static char game[PATH_LEN];

int cheats_count(void) { return ncheats; }
const char *cheats_desc(int i) { return i >= 0 && i < ncheats ? cheats[i].desc : ""; }
bool cheats_on(int i) { return i >= 0 && i < ncheats && cheats[i].on; }

// "Tekken 3 (World) (GameShark).cht" -> "Tekken 3"
static void base_title(const char *name, char *out, size_t len)
{
	snprintf(out, len, "%s", name);
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
		snprintf(out, len, "%.*s", (int)(b - a - 1), a + 1);
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

static void apply(void)
{
	retro_cheat_reset();
	int k = 0;
	for (int i = 0; i < ncheats; i++)
		if (cheats[i].on)
			retro_cheat_set(k++, true, cheats[i].code);
	if (k)
		log_printf("cheats: %d on\n", k);
}

static void on_path(char *out, size_t len)
{
	snprintf(out, len, USER_DIR "/%s.on", game);
}

static void save_on(void)
{
	char path[PATH_LEN];
	mkdir(USER_DIR, 0777);
	on_path(path, sizeof(path));
	FILE *f = fopen(path, "w");
	if (!f)
		return;
	for (int i = 0; i < ncheats; i++)
		if (cheats[i].on)
			fprintf(f, "%s\n", cheats[i].desc);
	fclose(f);
}

void cheats_unload(void)
{
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

	snprintf(path, sizeof(path), USER_DIR "/%s.cht", key);
	parse(path);

	DIR *d = opendir(DB_DIR);
	char best[PATH_LEN] = "";
	int best_score = 0;
	if (d) {
		struct dirent *de;
		while ((de = readdir(d))) {
			int s = score(de->d_name, key);
			if (s > best_score) {
				best_score = s;
				snprintf(best, sizeof(best), "%s", de->d_name);
			}
		}
		closedir(d);
	}
	if (best[0]) {
		snprintf(path, sizeof(path), DB_DIR "/%s", best);
		parse(path);
	}
	log_printf("cheats: %d for %s (%s)\n", ncheats, key, best[0] ? best : "no database match");

	on_path(path, sizeof(path));
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

void cheats_toggle(int i)
{
	if (i < 0 || i >= ncheats)
		return;
	cheats[i].on = !cheats[i].on;
	apply();
	save_on();
}
