#define _GNU_SOURCE   // strcasestr
// Game library: one entry per game with a clean title, built from the
// games folder. Discs of the same game ("... (Disc 1)", "... (Disc 2)")
// are grouped; the files a .cue or .m3u points at (tracks, discs) are hidden,
// and nothing else. Only readdir() is used (stat only when a folder entry has
// no type, no free-space queries), so it stays quick on huge SD cards, and
// the result is cached in library.txt. What a limit leaves out is counted
// (library_omitted) and logged, never dropped silently.
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "psx3d.h"
#include "safefile.h"

#define LIB_CACHE PSX3D_DIR "/library.txt"
#define MAX_GAMES 2048
#define MAX_DISCS 8
#define MAX_DEPTH 3
#define MAX_ENTRIES 4096    // disc files and folders read per folder
#define MAX_CANDS 8192      // disc files held between the two scan passes
#define MAX_REFS 8192       // files named by .cue and .m3u files
#define REF_BYTES (16 * 1024)   // read of one .cue or .m3u
#define MAX_PARTS 128       // folders in a path being compared

static game_t games[MAX_GAMES];
static int ngames;
static char lib_root[PATH_LEN];

// what the last scan left out, by reason
static struct {
	int files;     // folder or library over its limit, or a path too long
	int depth;     // folders below MAX_DEPTH, not opened
	int folders;   // folders that could not be opened
	int games;     // over MAX_GAMES
	int discs;     // over MAX_DISCS for one game
	int nomem;     // out of memory
	int refs;      // .cue/.m3u names not read: list too long (extra files may show)
} omit;

// series: games sharing a folder (or, failing that, a title stem)
#define MAX_SERIES 512
typedef struct { char *name; int n, *members; } series_t;
static series_t series[MAX_SERIES];
static int nseries;
static int game_series[MAX_GAMES];   // series index, or -1

int library_count(void) { return ngames; }
const game_t *library_get(int i) { return (i >= 0 && i < ngames) ? &games[i] : NULL; }

static void series_free(void)
{
	for (int i = 0; i < nseries; i++) {
		free(series[i].name);
		free(series[i].members);
	}
	nseries = 0;
}

// frees everything a game owns
static void game_free(game_t *g)
{
	free(g->title);
	free(g->cover_key);
	free(g->group);
	for (int d = 0; d < g->ndiscs; d++)
		free(g->discs[d]);
	memset(g, 0, sizeof(*g));
}

static void library_free(void)
{
	series_free();
	for (int i = 0; i < ngames; i++)
		game_free(&games[i]);
	memset(games, 0, sizeof(games));
	ngames = 0;
}

// a copy of s in *p; the old text stays if there is no memory
static void replace_str(char **p, const char *s)
{
	char *n = strdup(s);
	if (n) {
		free(*p);
		*p = n;
	}
}

int library_omitted(void)
{
	return omit.files + omit.depth + omit.folders + omit.games + omit.discs + omit.nomem;
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

// a new game named after the file; NULL (nothing kept) if out of memory
static game_t *new_game(u32 dir_id, const char *name, const char *key)
{
	game_t *g = &games[ngames];
	memset(g, 0, sizeof(*g));
	char ck[PATH_LEN];
	snprintf(ck, sizeof(ck), "%s", name);
	char *e = strrchr(ck, '.');
	if (e)
		*e = 0;
	g->title = clean_title(name);
	g->cover_key = strdup(ck);
	g->group = strdup(key);
	g->dir_id = dir_id;
	if (!g->title || !g->cover_key || !g->group) {
		game_free(g);
		return NULL;
	}
	ngames++;
	return g;
}

static void add_disc(u32 dir_id, const char *path)
{
	const char *slash = strrchr(path, '/');
	const char *name = slash ? slash + 1 : path;
	char key[PATH_LEN];
	int disc = disc_number(name, key, sizeof(key));

	// strip the extension from the group key so "X (Disc 1).cue" and
	// "X (Disc 2).cue" match
	char *dot = strrchr(key, '.');
	if (dot && is_disc_ext(dot))
		*dot = 0;

	game_t *g = disc ? find_group(dir_id, key) : NULL;
	if (g && g->ndiscs >= MAX_DISCS) {
		omit.discs++;
		return;
	}
	if (!g && ngames == MAX_GAMES) {
		omit.games++;
		return;
	}
	char *p = strdup(path);
	if (p && !g)
		g = new_game(dir_id, name, key);
	if (!p || !g) {
		free(p);
		omit.nomem++;
		return;
	}
	g->discs[g->ndiscs] = p;
	g->disc_no[g->ndiscs] = disc;
	g->ndiscs++;
}

// ---------------------------------------------------------------- scanning
// Pass 1 walks the folders, collecting the disc files and every file a .cue
// or .m3u names. Pass 2 adds the collected files that nothing names.

typedef struct { char *path; u32 dir_id; } cand_t;
typedef struct { char *name; bool isdir; } entry_t;
typedef struct { const char *s; size_t n; } part_t;

static cand_t *cands;
static int ncands, cands_cap;
static char **refs;      // normalized paths named by .cue and .m3u files, sorted
static int nrefs, refs_cap;

// arr with room for element n (doubling, at most max); NULL if there is none
// (arr itself stays valid then)
static void *grow(void *arr, int n, int *cap, size_t size, int max)
{
	if (n >= max)
		return NULL;
	if (n < *cap)
		return arr;
	int c = *cap ? *cap * 2 : 64;
	if (c > max)
		c = max;
	void *p = realloc(arr, (size_t)c * size);
	if (p)
		*cap = c;
	return p;
}

static const char *skip_sdmc(const char *p)
{
	return strncasecmp(p, "sdmc:", 5) ? p : p + 5;
}

// the folders of s, "." and ".." applied, appended to v[0..nv)
static int path_parts(part_t *v, int nv, const char *s)
{
	while (*s) {
		while (*s == '/' || *s == '\\')
			s++;
		size_t n = strcspn(s, "/\\");
		if (n == 2 && s[0] == '.' && s[1] == '.') {
			if (nv)
				nv--;
		} else if (n && !(n == 1 && s[0] == '.') && nv < MAX_PARTS) {
			v[nv].s = s;
			v[nv].n = n;
			nv++;
		}
		s += n;
	}
	return nv;
}

// One spelling per file, to compare paths: no "sdmc:" ("/x" is the same file),
// no "." or "..", either slash, lower case (FAT). A relative ref starts at dir.
static void norm_path(const char *dir, const char *ref, char *out, size_t len)
{
	part_t v[MAX_PARTS];
	int nv = 0;
	const char *r = skip_sdmc(ref);
	bool rooted = r[0] == '/' || r[0] == '\\';
	if (!rooted) {
		dir = skip_sdmc(dir);
		rooted = dir[0] == '/' || dir[0] == '\\';
		nv = path_parts(v, nv, dir);
	}
	nv = path_parts(v, nv, r);
	size_t n = 0;
	for (int i = 0; i < nv; i++) {
		if ((rooted || i) && n + 1 < len)
			out[n++] = '/';
		for (size_t k = 0; k < v[i].n && n + 1 < len; k++)
			out[n++] = tolower((unsigned char)v[i].s[k]);
	}
	if (rooted && !n && len > 1)
		out[n++] = '/';
	out[n] = 0;
}

// s without the spaces around it (cut in place)
static char *trim(char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	char *end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = 0;
	return s;
}

// FILE "name.bin" BINARY, or FILE name.bin BINARY: the name; else NULL
static char *cue_name(char *l)
{
	l = trim(l);
	if (strncasecmp(l, "FILE", 4) || !isspace((unsigned char)l[4]))
		return NULL;
	l = trim(l + 4);
	if (*l == '"') {
		char *q = strchr(++l, '"');
		if (q)
			*q = 0;
		return l;
	}
	// unquoted: the last word is the track type
	char *w = l + strlen(l);
	while (w > l && !isspace((unsigned char)w[-1]))
		w--;
	if (w > l)
		w[-1] = 0;
	return trim(l);
}

// one path per line, "#" lines are comments, "path|label" names a disc
static char *m3u_name(char *l)
{
	l = trim(l);
	if (*l == '#')
		return NULL;
	char *bar = strchr(l, '|');
	if (bar)
		*bar = 0;
	return trim(l);
}

static void add_ref(const char *dir, const char *name, const char *self)
{
	char ref[PATH_LEN];
	norm_path(dir, name, ref, sizeof(ref));
	if (!strcmp(ref, self))   // a file never hides itself
		return;
	char **g = grow(refs, nrefs, &refs_cap, sizeof(*refs), MAX_REFS);
	char *p = g ? strdup(ref) : NULL;
	if (g)
		refs = g;
	if (!p) {
		omit.refs++;
		return;
	}
	refs[nrefs++] = p;
}

// the files named by the .cue or .m3u at path (read up to REF_BYTES)
static void read_refs(const char *path, const char *dir, bool cue)
{
	static char buf[REF_BYTES + 1];
	FILE *f = fopen(path, "rb");
	if (!f)
		return;
	size_t n = fread(buf, 1, REF_BYTES, f);
	bool more = n == REF_BYTES && fgetc(f) != EOF;
	fclose(f);
	buf[n] = 0;
	if (more) {   // drop the line that was cut off
		while (n && buf[n - 1] != '\n' && buf[n - 1] != '\r')
			n--;
		buf[n] = 0;
		omit.refs++;
	}
	char self[PATH_LEN];
	norm_path("", path, self, sizeof(self));
	char *l = buf;
	if (!memcmp(l, "\xEF\xBB\xBF", 3))
		l += 3;
	while (*l) {
		size_t len = strcspn(l, "\r\n");
		char next = l[len];
		l[len] = 0;
		char *name = cue ? cue_name(l) : m3u_name(l);
		if (name && *name)
			add_ref(dir, name, self);
		if (!next)
			break;
		l += len + 1;
	}
}

static void note_file(u32 dir_id, const char *dir, const char *name)
{
	char path[PATH_LEN];
	if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path)) {
		omit.files++;
		return;
	}
	const char *e = ext_of(name);
	if (!strcasecmp(e, ".cue") || !strcasecmp(e, ".m3u"))
		read_refs(path, dir, !strcasecmp(e, ".cue"));
	cand_t *c = grow(cands, ncands, &cands_cap, sizeof(*cands), MAX_CANDS);
	char *p = c ? strdup(path) : NULL;
	if (c)
		cands = c;
	if (!p) {
		if (ncands >= MAX_CANDS)
			omit.files++;
		else
			omit.nomem++;
		return;
	}
	cands[ncands].path = p;
	cands[ncands].dir_id = dir_id;
	ncands++;
}

// a folder entry that is a folder; only an entry without a type costs a stat()
static bool is_dir_entry(const char *dir, const struct dirent *de)
{
	if (de->d_type != DT_UNKNOWN)
		return de->d_type == DT_DIR;
	char path[PATH_LEN];
	struct stat st;
	snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool wanted_entry(const char *dir, const struct dirent *de, bool *dir_)
{
	*dir_ = false;
	if (de->d_name[0] == '.')
		return false;
	*dir_ = is_dir_entry(dir, de);
	const char *e = ext_of(de->d_name);
	return *dir_ || is_disc_ext(e) || !strcasecmp(e, ".m3u");
}

// a folder too deep to open: count what is in it
static void count_deep(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *de;
	bool dir_;
	if (!d) {
		omit.folders++;
		return;
	}
	while ((de = readdir(d)))
		if (wanted_entry(dir, de, &dir_))
			omit.depth++;
	closedir(d);
}

static void scan_dir(const char *dir, int depth)
{
	static u32 dir_seq;
	DIR *d = opendir(dir);
	struct dirent *de;
	entry_t *ents = NULL;
	int n = 0, cap = 0;
	bool dir_;

	if (!d) {
		omit.folders++;
		log_printf("library: cannot open %s\n", dir);
		return;
	}
	while ((de = readdir(d))) {
		if (!wanted_entry(dir, de, &dir_))
			continue;
		entry_t *g = grow(ents, n, &cap, sizeof(*ents), MAX_ENTRIES);
		char *name = g ? strdup(de->d_name) : NULL;
		if (g)
			ents = g;
		if (!name) {
			if (n >= MAX_ENTRIES)
				omit.files++;
			else
				omit.nomem++;
			continue;
		}
		ents[n].name = name;
		ents[n].isdir = dir_;
		n++;
	}
	closedir(d);

	u32 dir_id = ++dir_seq;   // discs only group within one folder
	for (int i = 0; i < n; i++) {
		if (ents[i].isdir) {
			char sub[PATH_LEN];
			bool fits = snprintf(sub, sizeof(sub), "%s/%s", dir, ents[i].name) < (int)sizeof(sub);
			if (!fits)
				omit.folders++;
			else if (depth < MAX_DEPTH)
				scan_dir(sub, depth + 1);
			else
				count_deep(sub);
		} else {
			note_file(dir_id, dir, ents[i].name);
		}
		free(ents[i].name);
	}
	free(ents);
}

static int cmp_ref(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static void scan_free(void)
{
	for (int i = 0; i < ncands; i++)
		free(cands[i].path);
	for (int i = 0; i < nrefs; i++)
		free(refs[i]);
	free(cands);
	free(refs);
	cands = NULL;
	refs = NULL;
	ncands = cands_cap = nrefs = refs_cap = 0;
}

static void scan_library(const char *root)
{
	scan_dir(root, 0);
	if (nrefs)
		qsort(refs, nrefs, sizeof(refs[0]), cmp_ref);
	for (int i = 0; i < ncands; i++) {
		char key[PATH_LEN], *k = key;
		norm_path("", cands[i].path, key, sizeof(key));
		if (!nrefs || !bsearch(&k, refs, nrefs, sizeof(refs[0]), cmp_ref))
			add_disc(cands[i].dir_id, cands[i].path);
	}
	scan_free();
	if (library_omitted())
		log_printf("library: left out %d: %d over a folder or library limit, %d in folders nested too deep, "
			"%d folders not opened, %d over %d games, %d over %d discs of a game, %d out of memory\n",
			library_omitted(), omit.files, omit.depth, omit.folders, omit.games, MAX_GAMES,
			omit.discs, MAX_DISCS, omit.nomem);
	if (omit.refs)
		log_printf("library: %d disc list entries not read (list too long); extra files may show\n", omit.refs);
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
			replace_str(&g->title, buf);
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
					replace_str(&gs[k]->title, buf);
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
		replace_str(&g->cover_key, ck);
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
	bool ok = true;
	while (ok && fgets(line, sizeof(line), f) && ngames < MAX_GAMES) {
		line[strcspn(line, "\r\n")] = 0;
		char *tok = strtok(line, "\t");
		if (!tok)
			continue;
		game_t *g = &games[ngames];
		memset(g, 0, sizeof(*g));
		g->title = strdup(tok);
		tok = strtok(NULL, "\t");
		g->cover_key = strdup(tok ? tok : "");
		while (ok && (tok = strtok(NULL, "\t")) && g->ndiscs < MAX_DISCS) {
			g->discs[g->ndiscs] = strdup(tok);
			if (g->discs[g->ndiscs])
				g->ndiscs++;
			else
				ok = false;
		}
		ok = ok && g->title && g->cover_key;
		if (ok && g->ndiscs)
			ngames++;
		else
			game_free(g);   // no disc, or out of memory
	}
	fclose(f);
	return ok && ngames > 0;
}

// ---------------------------------------------------------------- series

// a word that's a number: "2", "III"; value or 0
static int number_word(const char *w, size_t n)
{
	if (n && isdigit((unsigned char)w[0])) {
		for (size_t i = 0; i < n; i++)
			if (!isdigit((unsigned char)w[i]))
				return 0;
		return atoi(w);
	}
	static const char *const roman[] = { "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
		"XI", "XII", "XIII", "XIV", "XV" };
	for (int i = 0; i < 15; i++)
		if (strlen(roman[i]) == n && !strncmp(w, roman[i], n))
			return i + 1;
	return 0;
}

// natural order inside a series: "Tekken" < "Tekken 2" < "Tekken 3",
// "Final Fantasy VII" < "VIII" < "IX", numbered games before named ones
static int cmp_natural(const char *a, const char *b)
{
	while (*a && *b) {
		size_t na = strcspn(a, " "), nb = strcspn(b, " ");
		int va = number_word(a, na), vb = number_word(b, nb);
		int c;
		if (va && vb)
			c = va - vb;
		else if (va || vb)
			c = va ? -1 : 1;
		else {
			c = strncasecmp(a, b, na < nb ? na : nb);
			if (!c)
				c = (int)na - (int)nb;
		}
		if (c)
			return c;
		a += na; b += nb;
		while (*a == ' ') a++;
		while (*b == ' ') b++;
	}
	return *a ? 1 : *b ? -1 : 0;
}

// the folder right under the games folder, when the game sits deeper
static void folder_key(const game_t *g, char *out, size_t len)
{
	out[0] = 0;
	size_t rl = strlen(lib_root);
	const char *p = g->ndiscs ? g->discs[0] : "";
	if (strncmp(p, lib_root, rl) || p[rl] != '/')
		return;
	p += rl + 1;
	const char *slash = strchr(p, '/');
	if (!slash)
		return;   // directly in the games folder
	char name[PATH_LEN];
	snprintf(name, sizeof(name), "%.*s", (int)(slash - p), p);
	char *t = clean_title(name);
	if (!t)
		return;
	snprintf(out, len, "%s", t);
	free(t);
}

// "Spyro 2 - Ripto's Rage!" -> "Spyro", "Final Fantasy VII" -> "Final Fantasy"
static void title_key(const game_t *g, char *out, size_t len)
{
	snprintf(out, len, "%s", g->title);
	char *cut = strstr(out, " - ");
	if (cut) *cut = 0;
	if ((cut = strchr(out, ':'))) *cut = 0;
	for (;;) {
		char *sp = strrchr(out, ' ');
		if (!sp || !number_word(sp + 1, strlen(sp + 1)))
			break;
		*sp = 0;
	}
}

static int find_series(const char *name)
{
	for (int i = 0; i < nseries; i++)
		if (!strcasecmp(series[i].name, name))
			return i;
	return -1;
}

static const char *sort_title(const game_t *g)
{
	return strncasecmp(g->title, "The ", 4) ? g->title : g->title + 4;
}

static int cmp_members(const void *a, const void *b)
{
	return cmp_natural(sort_title(&games[*(const int *)a]), sort_title(&games[*(const int *)b]));
}

static void build_series(void)
{
	static char keys[MAX_GAMES][96];
	series_free();
	// folders first: a folder holding two or more games is a series
	for (int i = 0; i < ngames; i++)
		folder_key(&games[i], keys[i], sizeof(keys[i]));
	for (int i = 0; i < ngames; i++) {
		int same = 0;
		for (int j = 0; j < ngames && keys[i][0]; j++)
			same += !strcasecmp(keys[i], keys[j]);
		if (same < 2)   // not grouped by folder: try the title
			title_key(&games[i], keys[i], sizeof(keys[i]));
	}
	for (int i = 0; i < ngames; i++) {
		game_series[i] = -1;
		int same = 0;
		for (int j = 0; j < ngames; j++)
			same += !strcasecmp(keys[i], keys[j]);
		if (same < 2 || !keys[i][0])
			continue;
		int s = find_series(keys[i]);
		if (s < 0) {
			if (nseries == MAX_SERIES)
				continue;
			char *nm = strdup(keys[i]);
			int *mem = malloc(sizeof(int) * same);
			if (!nm || !mem) {   // no series for these, they stay in the list
				free(nm);
				free(mem);
				continue;
			}
			s = nseries++;
			series[s].name = nm;
			series[s].members = mem;
			series[s].n = 0;
		}
		series[s].members[series[s].n++] = i;
		game_series[i] = s;
	}
	for (int s = 0; s < nseries; s++)
		qsort(series[s].members, series[s].n, sizeof(int), cmp_members);
	log_printf("library: %d series\n", nseries);
}

int library_series_count(void) { return nseries; }
int library_game_series(int game) { return game >= 0 && game < ngames ? game_series[game] : -1; }
const char *library_series_name(int s) { return s >= 0 && s < nseries ? series[s].name : ""; }
int library_series_size(int s) { return s >= 0 && s < nseries ? series[s].n : 0; }
int library_series_game(int s, int k) { return s >= 0 && s < nseries && k >= 0 && k < series[s].n ? series[s].members[k] : -1; }

bool library_load(const char *root, bool rescan)
{
	library_free();
	memset(&omit, 0, sizeof(omit));
	if (!root || !root[0])
		return false;
	snprintf(lib_root, sizeof(lib_root), "%s", root);
	if (!rescan && load_cache(root)) {
		log_printf("library: %d games from cache\n", ngames);
		build_series();
		return true;
	}
	library_free();
	u64 t0 = osGetTime();
	scan_library(root);
	for (int i = 0; i < ngames; i++) {
		sort_discs(&games[i]);
		fix_article(&games[i]);
		free(games[i].group);
		games[i].group = NULL;
	}
	disambiguate();
	qsort(games, ngames, sizeof(games[0]), cmp_games);
	log_printf("library: scanned %d games in %llums\n", ngames, (unsigned long long)(osGetTime() - t0));
	save_cache(root);
	build_series();
	return ngames > 0;
}

// Path to start a game with: the disc itself, or for multi-disc games a
// playlist (so the core can swap discs). NULL if the playlist could not be
// written; the one from before is then left as it was, and the game must not
// start with just disc 1.
const char *library_launch_path(const game_t *g, char *buf, size_t len)
{
	if (g->ndiscs < 1)
		return NULL;
	if (g->ndiscs == 1) {
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
	FILE *f = safe_begin(buf);
	if (!f) {
		log_printf("library: cannot write %s\n", buf);
		return NULL;
	}
	// the core only treats "/..." as absolute; on the 3DS "/x" is sdmc:/x
	bool ok = true;
	for (int d = 0; d < g->ndiscs; d++) {
		const char *p = g->discs[d];
		if (!strncmp(p, "sdmc:", 5))
			p += 5;
		if (fprintf(f, "%s\n", p) < 0)
			ok = false;
	}
	if (!safe_finish(f, buf, ok)) {
		log_printf("library: cannot write %s\n", buf);
		return NULL;
	}
	return buf;
}
