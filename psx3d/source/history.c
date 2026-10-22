// Save backups (see history.h). Files are only ever renamed into the
// backups or written through safefile.c, so a power cut leaves either the
// old or the new set of entries, never a cut-off one under a final name.
#include "history.h"
#include "safefile.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef HISTORY_ROOT
#define HISTORY_ROOT "sdmc:/3ds/PSX3D/backups"
#endif

void log_printf(const char *fmt, ...);

static char root[300] = HISTORY_ROOT;
static const char *const kind_name[HIST_KINDS] = {
	[HIST_TIMER] = "timer", [HIST_EXIT] = "exit", [HIST_LOAD] = "load", [HIST_SAVE] = "save",
	[HIST_RESET] = "reset", [HIST_START] = "start", [HIST_DELETE] = "delete", [HIST_RESTORE] = "restore",
	[HIST_CARD_SESSION] = "session", [HIST_CARD_DAY] = "day", [HIST_CARD_PIN] = "pin",
};

void history_set_root(const char *r)
{
	snprintf(root, sizeof(root), "%s", r);
}

const char *history_kind_name(hist_kind_t kind)
{
	return kind < HIST_KINDS ? kind_name[kind] : "?";
}

// "<seq>-<kind><ext>" -> entry; false for anything else (thumbnails, temp files)
static bool parse(const char *name, hist_entry_t *e)
{
	char *end;
	unsigned long seq = strtoul(name, &end, 10);
	if (end != name + 8 || *end != '-')
		return false;
	end++;
	for (int k = 0; k < HIST_KINDS; k++) {
		size_t n = strlen(kind_name[k]);
		if (strncmp(end, kind_name[k], n) || end[n] != '.')
			continue;
		const char *ext = end + n;
		size_t len = strlen(ext);
		if (len < 2 || len >= sizeof(e->ext) || strchr(ext + 1, '.'))   // .thm, .tmp, .bak...
			return false;
		e->seq = (unsigned)seq;
		e->kind = (hist_kind_t)k;
		memcpy(e->ext, ext, len + 1);
		return true;
	}
	return false;
}

static int newest_first(const void *a, const void *b)
{
	const hist_entry_t *x = a, *y = b;
	return x->seq < y->seq ? 1 : x->seq > y->seq ? -1 : 0;
}

// every entry of a group, newest first, in a new array (count in *n)
static hist_entry_t *scan(const char *group, int *n)
{
	char dir[600];
	*n = 0;
	if (snprintf(dir, sizeof(dir), "%s/%s", root, group) >= (int)sizeof(dir))
		return NULL;
	DIR *d = opendir(dir);
	if (!d)
		return NULL;
	int cap = 0;
	hist_entry_t *all = NULL;
	struct dirent *de;
	while ((de = readdir(d))) {
		hist_entry_t e;
		struct stat st;
		if (!parse(de->d_name, &e))
			continue;
		if (snprintf(e.path, sizeof(e.path), "%s/%s", dir, de->d_name) >= (int)sizeof(e.path) || stat(e.path, &st))
			continue;
		e.mtime = st.st_mtime;
		e.size = (long)st.st_size;
		char thm[620];
		snprintf(thm, sizeof(thm), "%s.thm", e.path);
		e.thumb = stat(thm, &st) == 0;
		if (*n == cap) {
			hist_entry_t *more = realloc(all, (cap = cap ? cap * 2 : 16) * sizeof(*all));
			if (!more)
				break;
			all = more;
		}
		all[(*n)++] = e;
	}
	closedir(d);
	if (*n)
		qsort(all, *n, sizeof(*all), newest_first);
	return all;
}

int history_list(const char *group, bool cards, hist_entry_t *out, int max)
{
	int n, k = 0;
	hist_entry_t *all = scan(group, &n);
	for (int i = 0; i < n && k < max; i++)
		if (hist_is_card(all[i].kind) == cards)
			out[k++] = all[i];
	free(all);
	return k;
}

bool history_find(const char *group, time_t mtime, long size, hist_entry_t *out)
{
	int n;
	bool found = false;
	hist_entry_t *all = scan(group, &n);
	for (int i = 0; i < n && !found; i++)
		if (!hist_is_card(all[i].kind) && all[i].mtime == mtime && all[i].size == size) {
			*out = all[i];
			found = true;
		}
	free(all);
	return found;
}

static void drop(const hist_entry_t *e)
{
	char thm[620];
	snprintf(thm, sizeof(thm), "%s.thm", e->path);
	remove(thm);
	remove(e->path);
	log_printf("backups: dropped %s\n", e->path);
}

// the calendar day (the 3DS clock and file times are local time already;
// no time zone library code is linked for this)
static long day_of(time_t t)
{
	return (long)(t / 86400);
}

// The per-group limits, newest kept: HIST_RING ring entries; per card file
// one session copy, HIST_CARD_DAYS day copies (one per day), HIST_CARD_PINS pins.
static void trim(const char *group)
{
	int n, ring = 0;
	hist_entry_t *all = scan(group, &n);
	for (int i = 0; i < n; i++) {
		hist_entry_t *e = &all[i];
		if (!hist_is_card(e->kind)) {
			if (++ring > HIST_RING)
				drop(e);
			continue;
		}
		int same = 0, limit = e->kind == HIST_CARD_SESSION ? 1 : e->kind == HIST_CARD_DAY ? HIST_CARD_DAYS : HIST_CARD_PINS;
		bool twin_day = false;
		for (int j = 0; j < i; j++)
			if (all[j].path[0] && all[j].kind == e->kind && !strcmp(all[j].ext, e->ext)) {
				same++;
				twin_day |= e->kind == HIST_CARD_DAY && day_of(all[j].mtime) == day_of(e->mtime);
			}
		if (same >= limit || twin_day) {
			drop(e);
			e->path[0] = 0;
		}
	}
	free(all);
}

// All games' ring entries together stay under HIST_BUDGET (PSX3D never asks
// the SD card for its free space). First, games not played for
// HIST_IDLE_DAYS keep only their newest HIST_IDLE_KEEP; then the oldest
// entries of any game go. keep: the entry just added is never dropped.
typedef struct { hist_entry_t e; int group, rank; time_t group_newest; } budget_t;
static void enforce_budget(const char *keep)
{
	DIR *d = opendir(root);
	if (!d)
		return;
	budget_t *all = NULL;
	int n = 0, cap = 0, groups = 0;
	unsigned long long total = 0;
	time_t now = time(NULL);
	struct dirent *de;
	while ((de = readdir(d))) {
		int m, rank = 0;
		if (de->d_name[0] == '.')
			continue;
		hist_entry_t *g = scan(de->d_name, &m);
		time_t newest = 0;
		for (int i = 0; i < m; i++)
			if (!hist_is_card(g[i].kind) && g[i].mtime > newest)
				newest = g[i].mtime;
		for (int i = 0; i < m; i++) {
			if (hist_is_card(g[i].kind))
				continue;
			if (n == cap) {
				budget_t *more = realloc(all, (cap = cap ? cap * 2 : 32) * sizeof(*all));
				if (!more)
					break;
				all = more;
			}
			all[n++] = (budget_t){ g[i], groups, rank++, newest };
			total += (unsigned long long)g[i].size;
		}
		free(g);
		groups++;
	}
	closedir(d);
	for (int phase = 0; phase < 2 && total > HIST_BUDGET; phase++)
		while (total > HIST_BUDGET) {
			int victim = -1;
			for (int i = 0; i < n; i++) {
				budget_t *b = &all[i];
				if (!b->e.path[0] || (keep && !strcmp(b->e.path, keep)))
					continue;
				if (phase == 0 && (b->rank < HIST_IDLE_KEEP || now - b->group_newest < HIST_IDLE_DAYS * 86400L))
					continue;
				if (victim < 0 || b->e.mtime < all[victim].e.mtime ||
				    (b->e.mtime == all[victim].e.mtime && b->group == all[victim].group && b->e.seq < all[victim].e.seq))
					victim = i;
			}
			if (victim < 0)
				break;
			drop(&all[victim].e);
			total -= (unsigned long long)all[victim].e.size;
			all[victim].e.path[0] = 0;
		}
	free(all);
}

bool history_next(const char *group, hist_kind_t kind, const char *ext, char *out, size_t len)
{
	char dir[600];
	int n;
	if (!group || !*group || strchr(group, '/') || !ext || kind >= HIST_KINDS)
		return false;
	mkdir(root, 0777);
	if (snprintf(dir, sizeof(dir), "%s/%s", root, group) >= (int)sizeof(dir))
		return false;
	mkdir(dir, 0777);
	struct stat st;
	if (stat(dir, &st) || !S_ISDIR(st.st_mode))
		return false;
	hist_entry_t *all = scan(group, &n);
	unsigned seq = n ? all[0].seq + 1 : 1;
	free(all);
	int w = snprintf(out, len, "%s/%08u-%s%s", dir, seq, kind_name[kind], ext);
	return w > 0 && (size_t)w < len;
}

void history_added(const char *group, const char *path)
{
	trim(group);
	enforce_budget(path);
}

bool history_adopt(const char *group, hist_kind_t kind, const char *ext, const char *file, const char *thm,
	hist_entry_t *out)
{
	char to[600], to_thm[620];
	struct stat st;
	if (!file || stat(file, &st))
		return false;
	// entries are named so that hist_entry_t holds their path
	if (!history_next(group, kind, ext, to, sizeof(((hist_entry_t *)0)->path)) || rename(file, to)) {
		log_printf("backups: could not keep %s\n", file);
		return false;
	}
	if (thm && stat(thm, &st) == 0) {
		snprintf(to_thm, sizeof(to_thm), "%s.thm", to);
		rename(thm, to_thm);
	}
	log_printf("backups: kept %s as %s\n", file, to);
	if (out) {
		memset(out, 0, sizeof(*out));
		memcpy(out->path, to, strlen(to) + 1);   // fits: history_next checked it
		out->kind = kind;
		snprintf(out->ext, sizeof(out->ext), "%s", ext);
		if (stat(to, &st) == 0) {
			out->mtime = st.st_mtime;
			out->size = (long)st.st_size;
		}
	}
	history_added(group, to);
	return true;
}

bool history_add(const char *group, hist_kind_t kind, const char *ext, const void *data, size_t size)
{
	char to[600];
	safe_format_t fmt = { size, 0, NULL, NULL, NULL, NULL };
	if (!history_next(group, kind, ext, to, sizeof(((hist_entry_t *)0)->path)) || !safe_write(to, &fmt, data, false))
		return false;
	history_added(group, to);
	return true;
}

bool history_take(const hist_entry_t *e, const char *file)
{
	char from[620], to[620];
	struct stat st;
	if (stat(file, &st) == 0 || rename(e->path, file))   // never over a file
		return false;
	snprintf(from, sizeof(from), "%s.thm", e->path);
	snprintf(to, sizeof(to), "%s.thm", file);
	remove(to);
	rename(from, to);
	return true;
}
