// Play history and favourites for the library (LIBRARY-DESIGN.md section 7 in
// the UI mock). Both are keyed by a game's save name, so a moved or renamed
// folder keeps its history and its star.
//   saves/playlog.ini     [<save name>] last=<unix time> seconds=<play time>
//   saves/favourites.ini  one save name per line
// Written through safefile (replace, never half a file), the play log once at
// the end of each game.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "psx3d.h"
#include "safefile.h"

#define PLAYLOG_FILE PSX3D_SAVE_DIR "/playlog.ini"
#define FAVS_FILE PSX3D_SAVE_DIR "/favourites.ini"
#define MAX_LOG 1024
#define NAME_LEN 96

typedef struct { char name[NAME_LEN]; u32 last, seconds; } logent_t;

static logent_t *log_ents;
static int nlog;
static char (*favs)[NAME_LEN];
static int nfavs;
static bool loaded;

static void trim(char *s)
{
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
		s[--n] = 0;
}

static logent_t *find(const char *name, bool add)
{
	for (int i = 0; i < nlog; i++)
		if (!strcmp(log_ents[i].name, name))
			return &log_ents[i];
	if (!add || nlog >= MAX_LOG)
		return NULL;
	logent_t *e = &log_ents[nlog++];
	memset(e, 0, sizeof(*e));
	snprintf(e->name, sizeof(e->name), "%s", name);
	return e;
}

static void load(void)
{
	if (loaded)
		return;
	loaded = true;
	log_ents = calloc(MAX_LOG, sizeof(*log_ents));
	favs = calloc(MAX_LOG, sizeof(*favs));
	if (!log_ents || !favs)
		return;
	safe_recover(PLAYLOG_FILE);
	safe_recover(FAVS_FILE);
	char line[160];
	FILE *f = fopen(PLAYLOG_FILE, "r");
	logent_t *cur = NULL;
	while (f && fgets(line, sizeof(line), f)) {
		trim(line);
		if (line[0] == '[') {
			char *end = strrchr(line, ']');
			if (end) {
				*end = 0;
				cur = find(line + 1, true);
			}
		} else if (cur && !strncmp(line, "last=", 5))
			cur->last = (u32)strtoul(line + 5, NULL, 10);
		else if (cur && !strncmp(line, "seconds=", 8))
			cur->seconds = (u32)strtoul(line + 8, NULL, 10);
	}
	if (f)
		fclose(f);
	f = fopen(FAVS_FILE, "r");
	while (f && nfavs < MAX_LOG && fgets(line, sizeof(line), f)) {
		trim(line);
		if (line[0])
			snprintf(favs[nfavs++], NAME_LEN, "%.*s", NAME_LEN - 1, line);
	}
	if (f)
		fclose(f);
}

static void save_log(void)
{
	FILE *f = safe_begin(PLAYLOG_FILE);
	if (!f)
		return;
	bool ok = true;
	for (int i = 0; i < nlog; i++)
		ok &= fprintf(f, "[%s]\nlast=%lu\nseconds=%lu\n", log_ents[i].name,
			(unsigned long)log_ents[i].last, (unsigned long)log_ents[i].seconds) > 0;
	if (!safe_finish(f, PLAYLOG_FILE, ok))
		log_printf("playlog: could not write %s\n", PLAYLOG_FILE);
}

static void save_favs(void)
{
	FILE *f = safe_begin(FAVS_FILE);
	if (!f)
		return;
	bool ok = true;
	for (int i = 0; i < nfavs; i++)
		ok &= fprintf(f, "%s\n", favs[i]) > 0;
	if (!safe_finish(f, FAVS_FILE, ok))
		log_printf("playlog: could not write %s\n", FAVS_FILE);
}

u32 playlog_last(const char *name)
{
	load();
	logent_t *e = log_ents ? find(name, false) : NULL;
	return e ? e->last : 0;
}

u32 playlog_seconds(const char *name)
{
	load();
	logent_t *e = log_ents ? find(name, false) : NULL;
	return e ? e->seconds : 0;
}

bool playlog_favourite(const char *name)
{
	load();
	for (int i = 0; i < nfavs; i++)
		if (!strcmp(favs[i], name))
			return true;
	return false;
}

bool playlog_toggle_favourite(const char *name)
{
	load();
	if (!favs)
		return false;
	for (int i = 0; i < nfavs; i++)
		if (!strcmp(favs[i], name)) {
			memmove(favs[i], favs[i + 1], (size_t)(nfavs - i - 1) * NAME_LEN);
			nfavs--;
			save_favs();
			return false;
		}
	if (nfavs < MAX_LOG)
		snprintf(favs[nfavs++], NAME_LEN, "%s", name);
	save_favs();
	return true;
}

// ---------------------------------------------------------------- a game's time
// Only time the game runs in the foreground counts: the pause menu and the
// HOME Menu / sleep stop the clock.

static char cur_name[NAME_LEN];
static u64 run_since;     // tick the clock last started, 0 while stopped
static u64 run_ticks;
static int stops;         // reasons the clock is stopped (menu, system)

void playlog_begin(const char *name)
{
	load();
	snprintf(cur_name, sizeof(cur_name), "%s", name ? name : "");
	run_ticks = 0;
	stops = 0;
	run_since = svcGetSystemTick();
}

void playlog_pause(bool paused)
{
	if (!cur_name[0])
		return;
	if (paused) {
		if (stops++ == 0 && run_since) {
			run_ticks += svcGetSystemTick() - run_since;
			run_since = 0;
		}
	} else if (stops > 0 && --stops == 0)
		run_since = svcGetSystemTick();
}

void playlog_end(void)
{
	if (!cur_name[0] || !log_ents)
		return;
	if (run_since)
		run_ticks += svcGetSystemTick() - run_since;
	run_since = 0;
	logent_t *e = find(cur_name, true);
	if (e) {
		e->last = (u32)time(NULL);
		e->seconds += (u32)(run_ticks / SYSCLOCK_ARM11);
		save_log();
	}
	cur_name[0] = 0;
}
