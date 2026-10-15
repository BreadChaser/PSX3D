#define _GNU_SOURCE   // strcasestr
// Game library: one entry per game with a clean title, built from the
// games folder. Discs of the same game ("... (Disc 1)", "... (Disc 2)")
// are grouped; .bin tracks behind a .cue are hidden. Only readdir() is used
// (no stat, no free-space queries), so it stays quick on huge SD cards, and
// the result is cached in library.txt.
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "psx3d.h"

#define LIB_CACHE PSX3D_DIR "/library.txt"
#define MAX_GAMES 2048
#define MAX_DISCS 8
#define MAX_DEPTH 3

static game_t games[MAX_GAMES];
static int ngames;

int library_count(void) { return ngames; }
const game_t *library_get(int i) { return (i >= 0 && i < ngames) ? &games[i] : NULL; }

static void library_free(void)
{
	for (int i = 0; i < ngames; i++) {
		free(games[i].title);
		free(games[i].cover_key);
		for (int d = 0; d < games[i].ndiscs; d++)
			free(games[i].discs[d]);
	}
	memset(games, 0, sizeof(games));
	ngames = 0;
}

static const char *ext_of(const char *name)
{
	const char *dot = strrchr(name, '.');
	return dot ? dot : "";
}

static bool is_disc_ext(const char *e)
{
	return !strcasecmp(e, ".cue") || !strcasecmp(e, ".chd") || !strcasecmp(e, ".pbp") ||
		!strcasecmp(e, ".iso") || !strcasecmp(e, ".img") || !strcasecmp(e, ".mdf") ||
		!strcasecmp(e, ".ccd") || !strcasecmp(e, ".bin");
}

// "Metal Gear Solid (USA) (Disc 2) (Rev 1)" -> disc 2, group key
// "Metal Gear Solid (USA) (Rev 1)"
static int disc_number(const char *name, char *key, size_t len)
{
	const char *p = strcasestr(name, "(Disc ");
	int disc = 0;
	if (p) {
		disc = atoi(p + 6);
		const char *close = strchr(p, ')');
		size_t pre = p - name;
		while (pre > 0 && name[pre - 1] == ' ')
			pre--;
		snprintf(key, len, "%.*s%s", (int)pre, name, close ? close + 1 : "");
	} else {
		snprintf(key, len, "%s", name);
	}
	return disc;
}

// strip "(...)" and "[...]" tags and the extension
static char *clean_title(const char *name)
{
	char out[PATH_LEN];
	size_t n = 0;
	int depth = 0;
	const char *end = name + strlen(name);
	const char *dot = strrchr(name, '.');
	if (dot && is_disc_ext(dot))
		end = dot;
	if (dot && !strcasecmp(dot, ".m3u"))
		end = dot;
	for (const char *p = name; p < end && n < sizeof(out) - 1; p++) {
		if (*p == '(' || *p == '[') { depth++; continue; }
		if (*p == ')' || *p == ']') { if (depth) depth--; continue; }
		if (depth)
			continue;
		if (*p == '_')
			out[n++] = ' ';
		else
			out[n++] = *p;
	}
	out[n] = 0;
	// collapse spaces, trim
	char *s = out, *d = out;
	bool space = true;
	for (; *s; s++) {
		if (*s == ' ') {
			if (!space)
				*d++ = ' ';
			space = true;
		} else {
			*d++ = *s;
			space = false;
		}
	}
	while (d > out && d[-1] == ' ')
		d--;
	*d = 0;
	return strdup(out[0] ? out : name);
}

// discs of one game share the name minus "(Disc N)", wherever they sit
// (many libraries put each disc in its own folder)
static game_t *find_group(u32 dir_id, const char *key)
{
	(void)dir_id;
	for (int i = ngames - 1; i >= 0; i--)
		if (games[i].group && !strcasecmp(games[i].group, key))
			return &games[i];
	return NULL;
}

static void add_disc(u32 dir_id, const char *dir, const char *name)
{
	char key[PATH_LEN], path[PATH_LEN];
	int disc = disc_number(name, key, sizeof(key));
	snprintf(path, sizeof(path), "%s/%s", dir, name);

	// strip the extension from the group key so "X (Disc 1).cue" and
	// "X (Disc 2).cue" match
	char *dot = strrchr(key, '.');
	if (dot && is_disc_ext(dot))
		*dot = 0;

	game_t *g = disc ? find_group(dir_id, key) : NULL;
	if (!g) {
		if (ngames == MAX_GAMES)
			return;
		g = &games[ngames++];
		memset(g, 0, sizeof(*g));
		g->title = clean_title(name);
		char ck[PATH_LEN];
		snprintf(ck, sizeof(ck), "%s", name);
		char *e = strrchr(ck, '.');
		if (e)
			*e = 0;
		g->cover_key = strdup(ck);
		g->group = strdup(key);
		g->dir_id = dir_id;
	}
	if (g->ndiscs < MAX_DISCS) {
		g->discs[g->ndiscs] = strdup(path);
		g->disc_no[g->ndiscs] = disc;
		g->ndiscs++;
	}
}

static void scan_dir(const char *dir, int depth)
{
	DIR *d = opendir(dir);
	struct dirent *de;
	char *names[512];
	bool isdir[512];
	int n = 0;
	bool has_cue = false, has_m3u = false;

	if (!d)
		return;
	while ((de = readdir(d)) && n < 512) {
		if (de->d_name[0] == '.')
			continue;
		bool dir_ = de->d_type == DT_DIR;
		const char *e = ext_of(de->d_name);
		if (!dir_ && !is_disc_ext(e) && strcasecmp(e, ".m3u"))
			continue;
		if (!strcasecmp(e, ".cue")) has_cue = true;
		if (!strcasecmp(e, ".m3u")) has_m3u = true;
		names[n] = strdup(de->d_name);
		isdir[n] = dir_;
		n++;
	}
	closedir(d);

	static u32 dir_seq;
	u32 dir_id = ++dir_seq;   // discs only group within one folder
	for (int i = 0; i < n; i++) {
		const char *e = ext_of(names[i]);
		if (isdir[i]) {
			if (depth < MAX_DEPTH) {
				char sub[PATH_LEN];
				snprintf(sub, sizeof(sub), "%s/%s", dir, names[i]);
				scan_dir(sub, depth + 1);
			}
		} else if (has_m3u) {
			// a playlist covers the folder's discs
			if (!strcasecmp(e, ".m3u"))
				add_disc(dir_id, dir, names[i]);
		} else if (!strcasecmp(e, ".bin") && has_cue) {
			// tracks of a .cue
		} else {
			add_disc(dir_id, dir, names[i]);
		}
		free(names[i]);
	}
}

// "Legend of Dragoon, The" -> "The Legend of Dragoon"
static void fix_article(game_t *g)
{
	static const char *const arts[] = { ", The", ", A", ", An" };
	for (size_t i = 0; i < sizeof(arts) / sizeof(arts[0]); i++) {
		size_t tl = strlen(g->title), al = strlen(arts[i]);
		char *at = strstr(g->title, arts[i]);
		// ", The" at the end or before " - subtitle"
		if (at && (at[al] == 0 || !strncmp(at + al, " - ", 3))) {
			char buf[PATH_LEN];
			snprintf(buf, sizeof(buf), "%s %.*s%s", arts[i] + 2, (int)(at - g->title), g->title, at + al);
			(void)tl;
			free(g->title);
			g->title = strdup(buf);
			return;
		}
	}
}

// the "(...)" tags of a No-Intro name, minus the boring ones
static bool interesting_tag(const char *t, size_t n)
{
	static const char *const boring[] = { "USA", "Europe", "Japan", "World", "En", "Fr", "De",
		"Es", "It", "Pt", "Sv", "Rev", "Disc", "v1", "Beta", "Demo" };
	for (size_t i = 0; i < sizeof(boring) / sizeof(boring[0]); i++)
		if (!strncasecmp(t, boring[i], strlen(boring[i])))
			return false;
	return n > 0;
}

// two different games with the same clean title: keep what tells them apart
static void disambiguate(void)
{
	for (int i = 0; i < ngames; i++)
		for (int j = i + 1; j < ngames; j++) {
			if (strcasecmp(games[i].title, games[j].title))
				continue;
			game_t *gs[2] = { &games[i], &games[j] };
			for (int k = 0; k < 2; k++) {
				const char *p = gs[k]->cover_key;
				char tags[128] = "";
				while ((p = strchr(p, '('))) {
					const char *q = strchr(p, ')');
					if (!q)
						break;
					if (interesting_tag(p + 1, q - p - 1) && !tags[0])
						snprintf(tags, sizeof(tags), "%.*s", (int)(q - p - 1), p + 1);
					p = q;
				}
				if (tags[0]) {
					char buf[PATH_LEN];
					snprintf(buf, sizeof(buf), "%s (%s)", gs[k]->title, tags);
					free(gs[k]->title);
					gs[k]->title = strdup(buf);
				}
			}
		}
}

static int cmp_games(const void *a, const void *b)
{
	return strcasecmp(((const game_t *)a)->title, ((const game_t *)b)->title);
}

static void sort_discs(game_t *g)
{
	for (int i = 1; i < g->ndiscs; i++)
		for (int j = i; j > 0 && g->disc_no[j] < g->disc_no[j - 1]; j--) {
			char *t = g->discs[j]; g->discs[j] = g->discs[j - 1]; g->discs[j - 1] = t;
			int n = g->disc_no[j]; g->disc_no[j] = g->disc_no[j - 1]; g->disc_no[j - 1] = n;
		}
	// cover art is named after disc 1
	if (g->ndiscs > 1) {
		const char *base = strrchr(g->discs[0], '/');
		char ck[PATH_LEN];
		snprintf(ck, sizeof(ck), "%s", base ? base + 1 : g->discs[0]);
		char *e = strrchr(ck, '.');
		if (e)
			*e = 0;
		free(g->cover_key);
		g->cover_key = strdup(ck);
	}
}

static void save_cache(const char *root)
{
	FILE *f = fopen(LIB_CACHE, "w");
	if (!f)
		return;
	fprintf(f, "#root\t%s\n", root);
	for (int i = 0; i < ngames; i++) {
		fprintf(f, "%s\t%s", games[i].title, games[i].cover_key);
		for (int d = 0; d < games[i].ndiscs; d++)
			fprintf(f, "\t%s", games[i].discs[d]);
		fputc('\n', f);
	}
	fclose(f);
}

static bool load_cache(const char *root)
{
	static char line[PATH_LEN * (MAX_DISCS + 2)];
	FILE *f = fopen(LIB_CACHE, "r");
	if (!f)
		return false;
	if (!fgets(line, sizeof(line), f) || strncmp(line, "#root\t", 6) ||
	    strncmp(line + 6, root, strlen(root)) || line[6 + strlen(root)] != '\n') {
		fclose(f);
		return false;
	}
	while (fgets(line, sizeof(line), f) && ngames < MAX_GAMES) {
		line[strcspn(line, "\r\n")] = 0;
		char *tok = strtok(line, "\t");
		if (!tok)
			continue;
		game_t *g = &games[ngames];
		memset(g, 0, sizeof(*g));
		g->title = strdup(tok);
		tok = strtok(NULL, "\t");
		g->cover_key = strdup(tok ? tok : "");
		while ((tok = strtok(NULL, "\t")) && g->ndiscs < MAX_DISCS)
			g->discs[g->ndiscs++] = strdup(tok);
		if (g->ndiscs)
			ngames++;
		else {
			free(g->title);
			free(g->cover_key);
		}
	}
	fclose(f);
	return ngames > 0;
}

bool library_load(const char *root, bool rescan)
{
	library_free();
	if (!root || !root[0])
		return false;
	if (!rescan && load_cache(root)) {
		log_printf("library: %d games from cache\n", ngames);
		return true;
	}
	library_free();
	u64 t0 = osGetTime();
	scan_dir(root, 0);
	for (int i = 0; i < ngames; i++) {
		sort_discs(&games[i]);
		fix_article(&games[i]);
		free(games[i].group);
		games[i].group = NULL;
	}
	disambiguate();
	qsort(games, ngames, sizeof(games[0]), cmp_games);
	log_printf("library: scanned %d games in %llums\n", ngames, osGetTime() - t0);
	save_cache(root);
	return ngames > 0;
}

// Path to start a game with: the disc itself, or for multi-disc games a
// playlist (so the core can swap discs).
const char *library_launch_path(const game_t *g, char *buf, size_t len)
{
	if (g->ndiscs <= 1) {
		snprintf(buf, len, "%s", g->discs[0]);
		return buf;
	}
	mkdir(PSX3D_DIR "/m3u", 0777);
	char safe[128];
	size_t n = 0;
	for (const char *p = g->cover_key; *p && n < sizeof(safe) - 1; p++)
		safe[n++] = (isalnum((unsigned char)*p) || *p == ' ' || *p == '-') ? *p : '_';
	safe[n] = 0;
	snprintf(buf, len, PSX3D_DIR "/m3u/%s.m3u", safe);
	FILE *f = fopen(buf, "w");
	if (!f) {
		snprintf(buf, len, "%s", g->discs[0]);
		return buf;
	}
	for (int d = 0; d < g->ndiscs; d++)
		fprintf(f, "%s\n", g->discs[d]);
	fclose(f);
	return buf;
}
