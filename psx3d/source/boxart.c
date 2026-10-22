// Box art matching and the memory of what the server does not have.
// See boxart.h. Mirrors tools/boxart/make_table.py (title_key, rank).
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boxart.h"
#include "safefile.h"

// ---------------------------------------------------------------- serials

static bool is_alpha(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static bool is_digit(int c) { return c >= '0' && c <= '9'; }

// A serial at the start of s: letters, an optional separator, digits (a '.'
// may sit inside them: "SLUS_006.94"). strict: the 4 + 5 shape of a normal
// disc, for text that merely contains a serial (a file name).
static bool serial_at(const char *s, bool strict, char out[10])
{
	int nl = 0, nd = 0;
	char l[5], d[6];
	int skip;
	while (is_alpha(s[nl]) && nl < 5)
		nl++;
	skip = nl;
	if (nl == 5 && (s[4] == 'P' || s[4] == 'p'))   // boot file "SLUSP012.06" is serial SLUS-01206 (Dragon Warrior VII)
		nl = 4;
	if (nl < (strict ? 4 : 2) || nl > 4)
		return false;
	for (int i = 0; i < nl; i++)
		l[i] = toupper((unsigned char)s[i]);
	s += skip;
	if (*s == '-' || *s == '_' || *s == ' ')
		s++;
	while (nd < 5 && (is_digit(*s) || (*s == '.' && nd == 3 && is_digit(s[1])))) {
		if (*s != '.')
			d[nd++] = *s;
		s++;
	}
	if (is_digit(*s) || nd < (strict ? 5 : 3))
		return false;
	memcpy(out, l, nl);
	memcpy(out + nl, d, nd);
	out[nl + nd] = 0;
	return true;
}

bool boxart_serial_norm(const char *in, char out[10])
{
	while (*in == ' ' || *in == '\t')
		in++;
	return serial_at(in, false, out);
}

// the first serial inside text (a file name), not in the middle of a word
static bool serial_in_text(const char *text, char out[10])
{
	for (const char *p = text; *p; p++)
		if (is_alpha(*p) && (p == text || !is_alpha(p[-1])) && serial_at(p, true, out))
			return true;
	return false;
}

bool boxart_serial_from_cnf(const char *cnf, char out[10])
{
	for (const char *p = cnf; *p; p++) {
		if (p != cnf && p[-1] != '\n' && p[-1] != '\r')
			continue;
		if (strncasecmp(p, "BOOT", 4))
			continue;
		const char *q = p + 4;
		while (*q == ' ' || *q == '\t')
			q++;
		if (*q++ != '=')
			continue;   // BOOT2, BOOTSTRAP ...
		while (*q == ' ' || *q == '\t')
			q++;
		const char *end = q;
		while (*end && *end != '\r' && *end != '\n' && *end != ';' && *end != ' ' && *end != '\t')
			end++;
		const char *name = q;
		for (const char *c = q; c < end; c++)
			if (*c == '\\' || *c == '/' || *c == ':')
				name = c + 1;
		char tok[32];
		size_t n = (size_t)(end - name);
		if (n >= sizeof(tok))
			return false;
		memcpy(tok, name, n);
		tok[n] = 0;
		return boxart_serial_norm(tok, out);
	}
	return false;
}

// ---------------------------------------------------------------- disc images

static bool read_sector(FILE *f, int ss, int off, uint32_t lba, uint8_t buf[2048])
{
	return fseek(f, (long)lba * ss + off, SEEK_SET) == 0 && fread(buf, 1, 2048, f) == 2048;
}

static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool image_serial(FILE *f, char out[10])
{
	static const int layouts[][2] = { { 2352, 16 }, { 2352, 24 }, { 2048, 0 }, { 2336, 8 } };
	uint8_t buf[2048];
	for (size_t l = 0; l < sizeof(layouts) / sizeof(layouts[0]); l++) {
		int ss = layouts[l][0], off = layouts[l][1];
		if (!read_sector(f, ss, off, 16, buf) || buf[0] != 1 || memcmp(buf + 1, "CD001", 5))
			continue;
		uint32_t dir = le32(buf + 158), dsize = le32(buf + 166);
		for (uint32_t s = 0; s < (dsize + 2047) / 2048 && s < 8; s++) {
			uint8_t d[2048];
			if (!read_sector(f, ss, off, dir + s, d))
				return false;
			for (int pos = 0; pos < 2048 && d[pos];) {
				int rl = d[pos], nl = pos + 32 < 2048 ? d[pos + 32] : 0;
				if (pos + 33 + nl > 2048)
					break;
				if (nl >= 10 && !strncasecmp((const char *)d + pos + 33, "SYSTEM.CNF", 10)) {
					uint32_t ext = le32(d + pos + 2), size = le32(d + pos + 10);
					uint8_t c[2049];
					if (size > 2048)
						size = 2048;
					if (!size || !read_sector(f, ss, off, ext, c))
						return false;
					c[size] = 0;
					return boxart_serial_from_cnf((const char *)c, out);
				}
				pos += rl;
			}
		}
		return false;
	}
	return false;
}

static const char *ext_of(const char *name)
{
	const char *dot = strrchr(name, '.');
	return dot ? dot : "";
}

// the first FILE of a cue sheet, next to the cue
static bool cue_first_file(const char *cue, char *out, size_t len)
{
	FILE *f = fopen(cue, "r");
	if (!f)
		return false;
	char line[512];
	bool ok = false;
	while (!ok && fgets(line, sizeof(line), f)) {
		const char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (strncasecmp(p, "FILE", 4) || !isspace((unsigned char)p[4]))
			continue;
		p += 4;
		while (*p == ' ' || *p == '\t')
			p++;
		const char *e;
		if (*p == '"') {
			p++;
			e = strchr(p, '"');
		} else {
			e = p + strcspn(p, " \t\r\n");
		}
		if (!e || e == p)
			continue;
		const char *slash = strrchr(cue, '/');
		size_t dir = slash ? (size_t)(slash - cue) + 1 : 0;
		if (dir + (size_t)(e - p) >= len)
			continue;
		memcpy(out, cue, dir);
		memcpy(out + dir, p, (size_t)(e - p));
		out[dir + (size_t)(e - p)] = 0;
		ok = true;
	}
	fclose(f);
	return ok;
}

bool boxart_disc_serial(const char *path, char out[10])
{
	const char *ext = ext_of(path);
	char bin[512];
	if (!strcasecmp(ext, ".cue")) {
		if (!cue_first_file(path, bin, sizeof(bin)))
			return false;
		path = bin;
	} else if (strcasecmp(ext, ".bin") && strcasecmp(ext, ".img") && strcasecmp(ext, ".iso") &&
	           strcasecmp(ext, ".mdf")) {
		return false;
	}
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	bool ok = image_serial(f, out);
	fclose(f);
	return ok;
}

// ---------------------------------------------------------------- names

void boxart_sanitize(const char *name, char *out, size_t len)
{
	size_t n = 0;
	for (const char *p = name; *p && n + 1 < len; p++)
		out[n++] = strchr("&*/:`<>?\\|\"", *p) ? '_' : *p;
	out[n] = 0;
}

static bool is_article(const char *w, size_t n)
{
	return (n == 3 && !memcmp(w, "the", 3)) || (n == 1 && w[0] == 'a') || (n == 2 && !memcmp(w, "an", 2));
}

void boxart_title_key(const char *name, char *out, size_t len)
{
	size_t n = 0;
	int depth = 0;
	char word[64];
	size_t wl = 0;
	for (const char *p = name;; p++) {
		int c = *p;
		bool alnum = c && depth == 0 && (is_alpha(c) || is_digit(c));
		if (alnum) {
			if (wl < sizeof(word))
				word[wl++] = tolower(c);
			continue;
		}
		if (wl && wl <= sizeof(word) && !is_article(word, wl)) {
			for (size_t i = 0; i < wl && n + 1 < len; i++)
				out[n++] = word[i];
		}
		wl = 0;
		if (c == '(' || c == '[')
			depth++;
		else if ((c == ')' || c == ']') && depth)
			depth--;
		if (!c)
			break;
	}
	out[n] = 0;
}

// binary search of a sorted text file, one name per line
static bool names_find(FILE *f, const char *key, long *at)
{
	if (fseek(f, 0, SEEK_END))
		return false;
	long lo = 0, hi = ftell(f);
	char line[BOXART_NAME + 8];
	while (lo < hi) {
		long mid = lo + (hi - lo) / 2, ls = mid;
		if (mid > lo) {   // the first line that starts at or after mid
			if (fseek(f, mid - 1, SEEK_SET))
				return false;
			int c;
			while ((c = fgetc(f)) != EOF && c != '\n')
				;
			ls = ftell(f);
		}
		if (ls >= hi) {
			hi = mid;
			continue;
		}
		if (fseek(f, ls, SEEK_SET) || !fgets(line, sizeof(line), f))
			return false;
		size_t l = strlen(line);
		bool full = l && line[l - 1] == '\n';
		if (full)
			line[--l] = 0;
		int cmp = strcmp(key, line);
		if (!cmp) {
			if (at)
				*at = ls;
			return true;
		}
		if (cmp < 0)
			hi = ls;
		else
			lo = ls + (long)l + 1;
	}
	return false;
}

bool boxart_name_known(const char *names_path, const char *key)
{
	FILE *f = fopen(names_path, "rb");
	if (!f)
		return false;
	bool ok = names_find(f, key, NULL);
	fclose(f);
	return ok;
}

// the picture a serial points at
static bool serial_name(const char *idx_path, const char *names_path, const char *serial, char *out, size_t len)
{
	char want[10];
	snprintf(want, sizeof(want), "%-9s", serial);
	FILE *f = fopen(idx_path, "rb");
	if (!f)
		return false;
	bool ok = false;
	long off = -1;
	if (!fseek(f, 0, SEEK_END)) {
		long lo = 0, hi = ftell(f) / 18;
		while (lo < hi && off < 0) {
			long mid = lo + (hi - lo) / 2;
			char rec[18];
			if (fseek(f, mid * 18, SEEK_SET) || fread(rec, 1, 18, f) != 18)
				break;
			int cmp = memcmp(want, rec, 9);
			if (!cmp)
				off = strtol(rec + 10, NULL, 10);
			else if (cmp < 0)
				hi = mid;
			else
				lo = mid + 1;
		}
	}
	fclose(f);
	if (off >= 0 && (f = fopen(names_path, "rb"))) {
		char line[BOXART_NAME];
		if (!fseek(f, off, SEEK_SET) && fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = 0;
			snprintf(out, len, "%s", line);
			ok = line[0] != 0;
		}
		fclose(f);
	}
	return ok;
}

// ---------------------------------------------------------------- fuzzy

static const char *const regions[] = { "USA", "Europe", "Japan", "World", "Asia", "Australia", "Korea",
	"Germany", "France", "Spain", "Italy", "Netherlands", "Sweden", "Brazil" };
#define NREGIONS ((int)(sizeof(regions) / sizeof(regions[0])))

// the "(...)" groups of a name
typedef struct { char t[16][48]; int n; } tags_t;

static void get_tags(const char *name, tags_t *out)
{
	out->n = 0;
	for (const char *p = name; (p = strchr(p, '(')) && out->n < 16;) {
		const char *q = strchr(p, ')');
		if (!q)
			break;
		size_t n = (size_t)(q - p - 1);
		if (n >= sizeof(out->t[0]))
			n = sizeof(out->t[0]) - 1;
		memcpy(out->t[out->n], p + 1, n);
		out->t[out->n++][n] = 0;
		p = q + 1;
	}
}

// the region a group of tags names, as an index into regions (-1: none)
static int tag_region(const tags_t *t)
{
	for (int r = 0; r < NREGIONS; r++)
		for (int i = 0; i < t->n; i++) {
			size_t rl = strlen(regions[r]);
			for (const char *p = t->t[i]; *p;) {
				const char *e = strstr(p, ", ");
				size_t l = e ? (size_t)(e - p) : strlen(p);
				if (l == rl && !strncasecmp(p, regions[r], rl))
					return r;
				if (!e)
					break;
				p = e + 2;
			}
		}
	return -1;
}

static const char *tag_disc(const tags_t *t)
{
	for (int i = 0; i < t->n; i++)
		if (!strncasecmp(t->t[i], "Disc ", 5))
			return t->t[i];
	return NULL;
}

// smaller is better, in hundredths (same rules as make_table.py rank())
static long rank_name(const char *cand, const tags_t *want)
{
	tags_t t;
	get_tags(cand, &t);
	long score = 0;
	int region = tag_region(&t), want_region = tag_region(want);
	if (want_region >= 0)
		score += region == want_region ? 0 : 1000;
	score += (region >= 0 ? region : NREGIONS) * 100;
	const char *wd = tag_disc(want), *d = tag_disc(&t);
	if (wd)
		score += d && !strcasecmp(wd, d) ? 0 : 10000;
	else
		score += !d || !strcasecmp(d, "Disc 1") ? 0 : 10000;
	static const char *const odd[] = { "Beta", "Demo", "Proto", "Sample", "Promo", "Kiosk", "Preview" };
	bool rev = false;
	for (int i = 0; i < t.n; i++) {
		for (size_t k = 0; k < sizeof(odd) / sizeof(odd[0]); k++)
			if (!strncasecmp(t.t[i], odd[k], strlen(odd[k])))
				score += 100000;
		rev |= !strncasecmp(t.t[i], "Rev", 3);
	}
	return score + (rev ? 100 : 0) + (long)strlen(cand);
}

// the two best pictures of the same title (other region, revision ...)
static int fuzzy_pick(const char *names_path, const char *key, char out[][BOXART_NAME], int max)
{
	char want_key[BOXART_NAME];
	boxart_title_key(key, want_key, sizeof(want_key));
	if (strlen(want_key) < 2 || max <= 0)
		return 0;
	FILE *f = fopen(names_path, "rb");
	if (!f)
		return 0;
	tags_t want;
	get_tags(key, &want);
	long best[2] = { -1, -1 };
	char name[2][BOXART_NAME];
	char line[BOXART_NAME], lk[BOXART_NAME];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		boxart_title_key(line, lk, sizeof(lk));
		if (strcmp(lk, want_key))
			continue;
		long r = rank_name(line, &want);
		if (best[0] < 0 || r < best[0]) {
			best[1] = best[0];
			memcpy(name[1], name[0], BOXART_NAME);
			best[0] = r;
			snprintf(name[0], BOXART_NAME, "%s", line);
		} else if (best[1] < 0 || r < best[1]) {
			best[1] = r;
			snprintf(name[1], BOXART_NAME, "%s", line);
		}
	}
	fclose(f);
	int n = 0;
	for (int i = 0; i < 2 && n < max; i++)
		if (best[i] >= 0)
			memcpy(out[n++], name[i], BOXART_NAME);   // both are NUL terminated: filled by snprintf
	return n;
}

static void add_candidate(char out[][BOXART_NAME], int *n, int max, const char *name)
{
	for (int i = 0; i < *n; i++)
		if (!strcmp(out[i], name))
			return;
	if (*n < max)
		snprintf(out[(*n)++], BOXART_NAME, "%s", name);
}

int boxart_candidates(const char *names_path, const char *idx_path, const char *key, const char *serial,
                      char out[][BOXART_NAME], int max)
{
	int n = 0;
	char own[BOXART_NAME];
	boxart_sanitize(key, own, sizeof(own));
	if (boxart_name_known(names_path, own)) {
		add_candidate(out, &n, max, own);
		return n;
	}
	char ser[10] = "", pic[BOXART_NAME];
	if (!(serial && boxart_serial_norm(serial, ser)) && !serial_in_text(key, ser))
		ser[0] = 0;
	if (ser[0] && serial_name(idx_path, names_path, ser, pic, sizeof(pic)))
		add_candidate(out, &n, max, pic);
	char fz[2][BOXART_NAME];
	int nf = fuzzy_pick(names_path, key, fz, 2);
	for (int i = 0; i < nf; i++)
		add_candidate(out, &n, max, fz[i]);
	if (!n)
		add_candidate(out, &n, max, own);   // the table may be older than the server
	return n;
}

// ---------------------------------------------------------------- memory

typedef struct { char *key; uint64_t ms; } miss_t;
typedef struct { char *key; long size, mtime; } user_t;
static miss_t *misses;
static int nmiss, cap_miss;
static user_t *users;      // covers made from the player's own picture
static int nusers, cap_users;
static bool dirty;

static bool push_miss(const char *key, uint64_t ms)
{
	for (int i = 0; i < nmiss; i++)
		if (!strcmp(misses[i].key, key)) {
			misses[i].ms = ms;
			return true;
		}
	if (nmiss == cap_miss) {
		int c = cap_miss ? cap_miss * 2 : 32;
		miss_t *m = realloc(misses, c * sizeof(*m));
		if (!m)
			return false;
		misses = m;
		cap_miss = c;
	}
	char *k = strdup(key);
	if (!k)
		return false;
	misses[nmiss].key = k;
	misses[nmiss++].ms = ms;
	return true;
}

static bool push_user(const char *key, long size, long mtime)
{
	for (int i = 0; i < nusers; i++)
		if (!strcmp(users[i].key, key)) {
			users[i].size = size;
			users[i].mtime = mtime;
			return true;
		}
	if (nusers == cap_users) {
		int c = cap_users ? cap_users * 2 : 16;
		user_t *u = realloc(users, c * sizeof(*u));
		if (!u)
			return false;
		users = u;
		cap_users = c;
	}
	char *k = strdup(key);
	if (!k)
		return false;
	users[nusers++] = (user_t){ k, size, mtime };
	return true;
}

void boxart_misses_free(void)
{
	for (int i = 0; i < nmiss; i++)
		free(misses[i].key);
	for (int i = 0; i < nusers; i++)
		free(users[i].key);
	free(misses);
	free(users);
	misses = NULL;
	users = NULL;
	nmiss = cap_miss = nusers = cap_users = 0;
	dirty = false;
}

// state file:  "#boxart <stamp>", then "M <ms> <key>" and "U <size> <mtime> <key>" lines
void boxart_misses_load(const char *path, const char *stamp)
{
	boxart_misses_free();
	safe_recover(path);
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	char line[BOXART_NAME + 64];
	bool same = false;
	if (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		same = !strncmp(line, "#boxart ", 8) && !strcmp(line + 8, stamp);
	}
	// a state written by another version: the table or the server list may
	// have changed, so ask again. The player's own pictures stay.
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		char *end;
		if (line[0] == 'M' && line[1] == ' ' && same) {
			uint64_t ms = 0;   // by hand: strtoull would link a new library function
			end = line + 2;
			while (*end >= '0' && *end <= '9')
				ms = ms * 10 + (uint64_t)(*end++ - '0');
			if (end > line + 2 && *end == ' ' && end[1])
				push_miss(end + 1, ms);
		} else if (line[0] == 'U' && line[1] == ' ') {
			long size = strtol(line + 2, &end, 10);
			char *e2;
			long mtime = strtol(end, &e2, 10);
			if (e2 > end && *e2 == ' ' && e2[1])
				push_user(e2 + 1, size, mtime);
		}
	}
	fclose(f);
	dirty = !same;
}

bool boxart_miss_known(const char *key, uint64_t now_ms)
{
	for (int i = 0; i < nmiss; i++)
		if (!strcmp(misses[i].key, key))
			return now_ms >= misses[i].ms && now_ms - misses[i].ms < BOXART_MISS_RETRY_MS;
	return false;
}

void boxart_miss_add(const char *key, uint64_t now_ms)
{
	if (push_miss(key, now_ms))
		dirty = true;
}

void boxart_misses_clear(void)
{
	for (int i = 0; i < nmiss; i++)
		free(misses[i].key);
	if (nmiss)
		dirty = true;
	nmiss = 0;
}

bool boxart_user_get(const char *key, long *size, long *mtime)
{
	for (int i = 0; i < nusers; i++)
		if (!strcmp(users[i].key, key)) {
			*size = users[i].size;
			*mtime = users[i].mtime;
			return true;
		}
	return false;
}

void boxart_user_set(const char *key, long size, long mtime)
{
	long s, m;
	if (boxart_user_get(key, &s, &m) && s == size && m == mtime)
		return;
	if (push_user(key, size, mtime))
		dirty = true;
}

void boxart_user_clear(const char *key)
{
	for (int i = 0; i < nusers; i++)
		if (!strcmp(users[i].key, key)) {
			free(users[i].key);
			users[i] = users[--nusers];
			dirty = true;
			return;
		}
}

bool boxart_misses_save(const char *path, const char *stamp)
{
	if (!dirty)
		return true;
	FILE *f = safe_begin(path);
	if (!f)
		return false;
	fprintf(f, "#boxart %s\n", stamp);
	for (int i = 0; i < nmiss; i++)
		fprintf(f, "M %llu %s\n", (unsigned long long)misses[i].ms, misses[i].key);
	for (int i = 0; i < nusers; i++)
		fprintf(f, "U %ld %ld %s\n", users[i].size, users[i].mtime, users[i].key);
	bool ok = safe_finish(f, path, true);
	if (ok)
		dirty = false;
	return ok;
}
