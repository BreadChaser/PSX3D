// Per-game settings: sdmc:/3ds/PSX3D/games/<game>.ini, "key=value" lines
// (screen mode, rewind, button mapping...). Loaded when a game starts,
// written back whenever a value changes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "psx3d.h"

#define DIR_ PSX3D_DIR "/games"
#define MAX_KEYS 64

static struct { char key[32], value[64]; } kv[MAX_KEYS];
static int nkv;
static char game[PATH_LEN];

static void path_of(char *out, size_t len)
{
	snprintf(out, len, DIR_ "/%s.ini", game);
}

void gamecfg_load(const char *name)
{
	char path[PATH_LEN], line[128];
	snprintf(game, sizeof(game), "%s", name);
	nkv = 0;
	path_of(path, sizeof(path));
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f) && nkv < MAX_KEYS) {
		line[strcspn(line, "\r\n")] = 0;
		char *eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = 0;
		snprintf(kv[nkv].key, sizeof(kv[nkv].key), "%s", line);
		snprintf(kv[nkv].value, sizeof(kv[nkv].value), "%s", eq + 1);
		nkv++;
	}
	fclose(f);
}

static void save(void)
{
	char path[PATH_LEN];
	if (!game[0])
		return;
	mkdir(DIR_, 0777);
	path_of(path, sizeof(path));
	FILE *f = fopen(path, "w");
	if (!f)
		return;
	for (int i = 0; i < nkv; i++)
		fprintf(f, "%s=%s\n", kv[i].key, kv[i].value);
	fclose(f);
}

const char *gamecfg_get(const char *key)
{
	for (int i = 0; i < nkv; i++)
		if (!strcmp(kv[i].key, key))
			return kv[i].value;
	return NULL;
}

int gamecfg_int(const char *key, int def)
{
	const char *v = gamecfg_get(key);
	return v ? atoi(v) : def;
}

void gamecfg_set(const char *key, const char *value)
{
	int i;
	for (i = 0; i < nkv; i++)
		if (!strcmp(kv[i].key, key))
			break;
	if (i == nkv) {
		if (nkv == MAX_KEYS)
			return;
		nkv++;
		snprintf(kv[i].key, sizeof(kv[i].key), "%s", key);
		kv[i].value[0] = 0;
	} else if (!strcmp(kv[i].value, value)) {
		return;   // unchanged
	}
	snprintf(kv[i].value, sizeof(kv[i].value), "%s", value);
	save();
}

void gamecfg_set_int(const char *key, int v)
{
	char b[16];
	snprintf(b, sizeof(b), "%d", v);
	gamecfg_set(key, b);
}

void gamecfg_end(void)
{
	game[0] = 0;
	nkv = 0;
}
