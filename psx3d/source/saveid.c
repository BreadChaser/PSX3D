#define _GNU_SOURCE   // strcasestr
// Which game a save name belongs to (harsh review M2). Memory cards, states,
// the Continue save, per-game settings and cheats are named after disc 1's
// file name ("game" for Game A/game.bin), so two different games with the
// same file name in different folders would share them and overwrite each
// other's progress. saves/<name>.id records the game that owns a name: the
// size of its disc 1 data file and the serials it started with. A different
// game that comes with an owned name gets a name of its own,
// "<name> [SLUS01234]" (its serial, or a hash of its size when it has none),
// and the player is told once. The same game moved to another folder, or
// converted (.bin to .chd: same serial), keeps its saves.
//
// Saves from before this (no .id) belong to the first game started with that
// name, unless the library shows another game with the same name and the
// memory card holds only other games' saves; then that other game may still
// claim them. Nothing is moved or renamed, ever: unambiguous games keep using
// exactly the files they always used.
//
// Library and folder browser agree: a disc the browser starts that belongs to
// a library game (disc 2, a disc of a playlist) uses that game's name, and
// discs of one game share disc 1's name in both.
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "psx3d.h"
#include "safefile.h"

// Names are bounded by PATH_LEN; cutting one off is intentional.
#pragma GCC diagnostic ignored "-Wformat-truncation"

#define ID_SERIALS 8
#define ID_ALIASES 16      // other games' names for one file name ("[...]-2" ...)
#define CARD_BYTES (128 * 1024)

typedef struct {
	bool ok;                          // the file was there and readable
	long long size;                   // disc 1 data file, 0 = not known
	char first[10];                   // serial when started from disc 1
	char serials[ID_SERIALS][10];     // every serial it started with
	int nserials;
	char disc[PATH_LEN];              // where disc 1 was first seen (for people)
} rec_t;

// the game being started (saveid_begin -> saveid_confirm)
static struct {
	char base[PATH_LEN];     // its file name: the save name of old
	char name[PATH_LEN];     // what saveid_begin gave
	char disc[PATH_LEN];     // disc 1 data file, normalized
	long long size;
	bool first;              // starts from disc 1
	int twins;               // other library games with the same file name
} cur;

// ---------------------------------------------------------------- paths

// one spelling per file (FAT): no "sdmc:", "/" only, lower case
static void norm(const char *in, char *out, size_t len)
{
	size_t n = 0;
	if (!strncasecmp(in, "sdmc:", 5))
		in += 5;
	for (; *in && n + 1 < len; in++)
		out[n++] = *in == '\\' ? '/' : tolower((unsigned char)*in);
	out[n] = 0;
}

static bool same_file(const char *a, const char *b)
{
	char na[PATH_LEN], nb[PATH_LEN];
	norm(a, na, sizeof(na));
	norm(b, nb, sizeof(nb));
	return !strcmp(na, nb);
}

static const char *base_of(const char *path)
{
	const char *s = strrchr(path, '/');
	return s ? s + 1 : path;
}

static const char *ext_of(const char *path)
{
	const char *b = base_of(path), *dot = strrchr(b, '.');
	return dot ? dot : "";
}

// file name without folder and extension
static void stem(const char *path, char *out, size_t len)
{
	snprintf(out, len, "%s", base_of(path));
	char *dot = strrchr(out, '.');
	if (dot)
		*dot = 0;
}

// name as a list (.cue/.m3u at list) names it: relative to the list's folder;
// "/x" is the SD card's x (that is how the core reads it)
static void ref_path(const char *list, const char *name, char *out, size_t len)
{
	if (name[0] == '/' || name[0] == '\\')
		snprintf(out, len, "sdmc:%s", name);
	else if (!strncasecmp(name, "sdmc:", 5))
		snprintf(out, len, "%s", name);
	else
		snprintf(out, len, "%.*s%s", (int)(base_of(list) - list), list, name);
	for (char *p = out; *p; p++)
		if (*p == '\\')
			*p = '/';
}

static char *trim(char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	char *e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1]))
		*--e = 0;
	return s;
}

// FILE "name.bin" BINARY, or FILE name.bin BINARY: the name; else NULL
static char *cue_file(char *l)
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
	char *w = l + strlen(l);
	while (w > l && !isspace((unsigned char)w[-1]))
		w--;
	if (w > l)
		w[-1] = 0;
	return trim(l);
}

// one path per line, "#" comments, "path|label"
static char *m3u_file(char *l)
{
	l = trim(l);
	if (*l == '#')
		return NULL;
	char *bar = strchr(l, '|');
	if (bar)
		*bar = 0;
	l = trim(l);
	return *l ? l : NULL;
}

// The files a .cue or .m3u names, in order: each() gets each one, until it
// returns true. true if one did.
static bool list_each(const char *list, bool (*each)(const char *path, void *ud), void *ud)
{
	static char buf[16 * 1024 + 1];
	bool cue = !strcasecmp(ext_of(list), ".cue");
	FILE *f = fopen(list, "rb");
	if (!f)
		return false;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;
	char *l = buf;
	if (!memcmp(l, "\xEF\xBB\xBF", 3))
		l += 3;
	while (*l) {
		size_t len = strcspn(l, "\r\n");
		char next = l[len];
		l[len] = 0;
		char *name = cue ? cue_file(l) : m3u_file(l);
		if (name && *name) {
			char path[PATH_LEN];
			ref_path(list, name, path, sizeof(path));
			if (each(path, ud))
				return true;
		}
		if (!next)
			break;
		l += len + 1;
	}
	return false;
}

static bool take_first(const char *path, void *ud)
{
	snprintf(ud, PATH_LEN, "%s", path);
	return true;
}

// The file holding disc 1's data: a playlist's first disc, a cue sheet's
// first track, a .ccd's .img; else the file itself.
static void data_file(const char *path, char *out)
{
	snprintf(out, PATH_LEN, "%s", path);
	for (int depth = 0; depth < 3; depth++) {
		const char *e = ext_of(out);
		char next[PATH_LEN];
		if (!strcasecmp(e, ".m3u") || !strcasecmp(e, ".cue")) {
			if (!list_each(out, take_first, next))
				return;
		} else if (!strcasecmp(e, ".ccd")) {
			snprintf(next, sizeof(next), "%.*s.img", (int)(e - out), out);
		} else
			return;
		snprintf(out, PATH_LEN, "%s", next);
	}
}

static long long size_of(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 ? (long long)st.st_size : 0;
}

// ---------------------------------------------------------------- library

typedef struct { const char *want; } find_t;
static bool names_want(const char *path, void *ud)
{
	return same_file(path, ((find_t *)ud)->want);
}

// The library game a disc file started from the browser belongs to: one of
// its discs, or a file its playlist (anywhere) or cue sheet (same folder)
// names. NULL: none.
static const game_t *library_game_of(const char *path)
{
	int n = library_count();
	for (int i = 0; i < n; i++) {
		const game_t *g = library_get(i);
		for (int d = 0; d < g->ndiscs; d++)
			if (same_file(g->discs[d], path))
				return g;
	}
	find_t f = { path };
	size_t dir = base_of(path) - path;
	for (int i = 0; i < n; i++) {
		const game_t *g = library_get(i);
		for (int d = 0; d < g->ndiscs; d++) {
			const char *e = ext_of(g->discs[d]);
			bool here = (size_t)(base_of(g->discs[d]) - g->discs[d]) == dir && !strncasecmp(g->discs[d], path, dir);
			if ((!strcasecmp(e, ".m3u") || (here && !strcasecmp(e, ".cue"))) && list_each(g->discs[d], names_want, &f))
				return g;
		}
	}
	return NULL;
}

// "Game (Disc 2).cue" -> 2 (0: no disc number)
static int disc_no(const char *name)
{
	const char *p = strcasestr(name, "(Disc ");
	return p ? atoi(p + 6) : 0;
}

// ---------------------------------------------------------------- records

static void id_path(const char *name, char *out, size_t len)
{
	snprintf(out, len, PSX3D_SAVE_DIR "/%s.id", name);
}

static bool rec_has(const rec_t *r, const char *serial)
{
	for (int i = 0; i < r->nserials; i++)
		if (!strcmp(r->serials[i], serial))
			return true;
	return false;
}

static void rec_add(rec_t *r, const char *serial)
{
	if (serial[0] && !rec_has(r, serial) && r->nserials < ID_SERIALS)
		snprintf(r->serials[r->nserials++], sizeof(r->serials[0]), "%s", serial);
}

static void rec_read(const char *name, rec_t *r)
{
	char path[PATH_LEN], line[PATH_LEN + 16];
	memset(r, 0, sizeof(*r));
	id_path(name, path, sizeof(path));
	safe_recover(path);
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	r->ok = true;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		char *eq = strchr(line, '=');
		if (!eq || line[0] == '#')
			continue;
		*eq++ = 0;
		if (!strcmp(line, "size"))
			r->size = strtoll(eq, NULL, 10);
		else if (!strcmp(line, "first"))
			snprintf(r->first, sizeof(r->first), "%.9s", eq);
		else if (!strcmp(line, "serial"))
			rec_add(r, eq);
		else if (!strcmp(line, "disc"))
			snprintf(r->disc, sizeof(r->disc), "%s", eq);
	}
	fclose(f);
}

static bool rec_write(const char *name, const rec_t *r)
{
	char path[PATH_LEN];
	mkdir(PSX3D_SAVE_DIR, 0777);
	id_path(name, path, sizeof(path));
	FILE *f = safe_begin(path);
	if (!f) {
		log_printf("saves: cannot write %s\n", path);
		return false;
	}
	bool ok = fprintf(f, "# PSX3D: the game these saves belong to\nversion=1\nsize=%lld\n", r->size) > 0;
	if (r->first[0])
		ok = ok && fprintf(f, "first=%s\n", r->first) > 0;
	for (int i = 0; i < r->nserials; i++)
		ok = ok && fprintf(f, "serial=%s\n", r->serials[i]) > 0;
	ok = ok && fprintf(f, "disc=%s\n", r->disc) > 0;
	if (!safe_finish(f, path, ok)) {
		log_printf("saves: cannot write %s\n", path);
		return false;
	}
	return true;
}

// a real disc serial (4 letters, 5 digits), upper case without "-_.", in out;
// "" for none (the core's SLUS99999 stand-in, names made from the EXE)
static void serial_norm(const char *in, char *out)
{
	int n = 0;
	for (; in && *in && n < 9; in++) {
		unsigned char c = *in;
		if (c == '-' || c == '_' || c == '.')
			continue;
		if (n < 4 ? !isalpha(c) : !isdigit(c))
			break;
		out[n++] = toupper(c);
	}
	out[n] = 0;
	if (n != 9 || (in && *in) || !strcmp(out, "SLUS99999"))
		out[0] = 0;
}

// Is the game being started the one r belongs to? A different disc 1 serial
// says no; the same data size or a serial it started with says yes.
static bool rec_same(const rec_t *r, const char *serial)
{
	if (serial[0] && cur.first && r->first[0] && strcmp(r->first, serial))
		return false;
	if (r->size && cur.size)
		return r->size == cur.size || (serial[0] && rec_has(r, serial));
	return true;   // nothing to tell them apart by: as before
}

// this game owns name (its record r, or a new one): record what it brings
static void rec_claim(const char *name, rec_t *r, const char *serial)
{
	bool dirty = !r->ok;
	if (!r->size && cur.size) {
		r->size = cur.size;
		dirty = true;
	}
	if (cur.first && serial[0] && !r->first[0]) {
		snprintf(r->first, sizeof(r->first), "%s", serial);
		dirty = true;
	}
	if (serial[0] && !rec_has(r, serial) && r->nserials < ID_SERIALS) {
		rec_add(r, serial);
		dirty = true;
	}
	if (!r->disc[0] && cur.disc[0]) {
		snprintf(r->disc, sizeof(r->disc), "%s", cur.disc);
		dirty = true;
	}
	if (dirty && rec_write(name, r))
		r->ok = true;
}

// the other names of base ("base [...]"), each passed to each() until it
// returns true; the number of them
static int aliases(bool (*each)(const char *name, void *ud), void *ud)
{
	DIR *d = opendir(PSX3D_SAVE_DIR);
	if (!d)
		return 0;
	size_t bl = strlen(cur.base);
	int n = 0;
	struct dirent *de;
	while ((de = readdir(d))) {
		size_t l = strlen(de->d_name);
		if (l < bl + 6 || strncasecmp(de->d_name, cur.base, bl) || strncmp(de->d_name + bl, " [", 2) ||
		    strcasecmp(de->d_name + l - 3, ".id") || !strchr(de->d_name + bl, ']'))
			continue;
		char name[PATH_LEN];
		snprintf(name, sizeof(name), "%.*s", (int)(l - 3), de->d_name);
		n++;
		if (each && each(name, ud))
			break;
	}
	closedir(d);
	return n;
}

static bool alias_by_size(const char *name, void *ud)
{
	rec_t r;
	rec_read(name, &r);
	if (r.ok && r.size && r.size == cur.size) {
		snprintf(ud, PATH_LEN, "%s", name);
		return true;
	}
	return false;
}
// The save codes on a memory card ("BASLUS-00594..." -> SLUS00594): true
// when it has some and none is serial.
static bool card_of_others(const char *serial)
{
	char path[PATH_LEN];
	snprintf(path, sizeof(path), PSX3D_SAVE_DIR "/%s.srm", cur.base);
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	static unsigned char card[128 * 16];   // header + directory frames
	size_t n = fread(card, 1, sizeof(card), f);
	fclose(f);
	if (n != sizeof(card) || size_of(path) != CARD_BYTES || card[0] != 'M' || card[1] != 'C')
		return false;
	int codes = 0;
	for (int i = 1; i < 16; i++) {
		const unsigned char *e = card + 128 * i;
		if (e[0] != 0x51)   // first block of a save
			continue;
		char code[16], s[10];
		snprintf(code, sizeof(code), "%.4s%.5s", (const char *)e + 12, (const char *)e + 17);
		serial_norm(code, s);
		if (!s[0])
			continue;
		if (!strcmp(s, serial))
			return false;
		codes++;
	}
	return codes > 0;
}

// ---------------------------------------------------------------- API

void saveid_begin(const char *path, const char *base, char *name, size_t len)
{
	char disc1[PATH_LEN], data[PATH_LEN];
	memset(&cur, 0, sizeof(cur));
	snprintf(disc1, sizeof(disc1), "%s", path);
	cur.first = true;
	if (base)
		snprintf(cur.base, sizeof(cur.base), "%s", base);
	else {
		// the folder browser: the library's name if the disc is one of its games'
		const game_t *g = library_game_of(path);
		if (g) {
			snprintf(cur.base, sizeof(cur.base), "%s", g->cover_key);
			snprintf(disc1, sizeof(disc1), "%s", g->discs[0]);
			char a[PATH_LEN], b[PATH_LEN];
			data_file(path, a);
			data_file(g->discs[0], b);
			cur.first = same_file(a, b);   // started from disc 1
		} else {
			stem(path, cur.base, sizeof(cur.base));
			// "X (Disc 2)" next to its "X (Disc 1)": disc 1's name, as the library has it
			const char *b = base_of(path), *tag = strcasestr(b, "(Disc ");
			if (disc_no(b) > 1) {
				const char *close = strchr(tag, ')');
				char one[PATH_LEN];
				snprintf(one, sizeof(one), "%.*s(Disc 1%s", (int)(tag - path), path, close ? close : ")");
				struct stat st;
				if (!stat(one, &st)) {
					snprintf(disc1, sizeof(disc1), "%s", one);
					stem(one, cur.base, sizeof(cur.base));
					cur.first = false;
				}
			}
		}
	}
	// the library's own playlist for a multi-disc game starts with disc 1
	data_file(disc1, data);
	cur.size = size_of(data);
	norm(data, cur.disc, sizeof(cur.disc));
	for (int i = 0; i < library_count(); i++) {
		const game_t *g = library_get(i);
		char other[PATH_LEN];
		if (strcasecmp(g->cover_key, cur.base) || !g->ndiscs)
			continue;
		data_file(g->discs[0], other);
		cur.twins += !same_file(other, data);
	}

	// before loading: the owner's size matches, or another name of ours does
	rec_t r;
	rec_read(cur.base, &r);
	snprintf(cur.name, sizeof(cur.name), "%s", cur.base);
	if (r.ok && r.size && cur.size && r.size != cur.size)
		aliases(alias_by_size, cur.name);
	snprintf(name, len, "%s", cur.name);
	log_printf("saves: %s (disc 1 %s, %lld bytes%s)\n", cur.name, cur.disc, cur.size,
		cur.twins ? ", same file name as another library game" : "");
}

bool saveid_confirm(const char *core_serial, char *name, size_t len, bool *created)
{
	char serial[10], final[PATH_LEN] = "";
	rec_t r;
	serial_norm(core_serial, serial);
	*created = false;

	// a name of ours found before loading
	if (strcasecmp(cur.name, cur.base)) {
		rec_read(cur.name, &r);
		if (r.ok && rec_same(&r, serial)) {
			snprintf(final, sizeof(final), "%s", cur.name);
			rec_claim(final, &r, serial);
		}
	}
	if (!final[0]) {
		rec_read(cur.base, &r);
		bool mine;
		if (!r.ok)
			// saves from before .id files (or none): this game's, unless its
			// memory card shows only other games' saves and a library game
			// with the same file name has no name of its own yet
			mine = !(cur.twins && serial[0] && cur.first && card_of_others(serial) &&
				aliases(NULL, NULL) < cur.twins);
		else
			mine = rec_same(&r, serial);
		if (mine) {
			snprintf(final, sizeof(final), "%s", cur.base);
			rec_claim(final, &r, serial);
		} else {
			log_printf("saves: %s belongs to another game (%s, %lld bytes)\n", cur.base,
				r.ok ? (r.first[0] ? r.first : "no serial") : "its memory card", r.size);
		}
	}
	if (!final[0]) {
		// this game's own name: "<name> [serial]" ([size hash] without one);
		// made the first time, found again later (also after a move)
		char tag[16];
		if (serial[0])
			snprintf(tag, sizeof(tag), "%s", serial);
		else {
			u32 h = 2166136261u;
			for (int i = 0; i < 8; i++)
				h = (h ^ (u8)(cur.size >> (8 * i))) * 16777619u;
			snprintf(tag, sizeof(tag), "%08lX", (unsigned long)h);
		}
		for (int k = 1; k <= ID_ALIASES && !final[0]; k++) {
			char try_[PATH_LEN];
			if (k == 1)
				snprintf(try_, sizeof(try_), "%s [%s]", cur.base, tag);
			else
				snprintf(try_, sizeof(try_), "%s [%s]-%d", cur.base, tag, k);
			rec_read(try_, &r);
			if (r.ok && !rec_same(&r, serial))
				continue;   // taken by yet another game
			snprintf(final, sizeof(final), "%s", try_);
			*created = !r.ok;
			rec_claim(final, &r, serial);
		}
		if (!final[0])   // all taken (never seen): keep the saves of the others safe
			snprintf(final, sizeof(final), "%s [%s]-x", cur.base, tag);
		log_printf("saves: this game saves as %s%s\n", final, *created ? " (new)" : "");
	}
	bool changed = strcmp(final, cur.name) != 0;
	snprintf(cur.name, sizeof(cur.name), "%s", final);
	snprintf(name, len, "%s", final);
	return changed;
}
