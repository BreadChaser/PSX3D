// Host test of save names (harsh review M2): runs the real
// psx3d/source/saveid.c (and safefile.c) on fixture folders in a temp
// directory, against a stand-in library. A "launch" is what emu.c does:
// saveid_begin before loading, saveid_confirm with the disc serial after.
//
// "sdmc:/..." paths are plain relative folders here: the test chdirs into its
// temp directory, so PSX3D_SAVE_DIR is ./sdmc:/3ds/PSX3D/saves.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "3ds.h"
#include "psx3d.h"

// the stand-in library (library.c's public view)
static game_t lib[16];
static int nlib;
int library_count(void) { return nlib; }
const game_t *library_get(int i) { return i >= 0 && i < nlib ? &lib[i] : NULL; }
void log_printf(const char *fmt, ...)
{
	if (!getenv("SAVEID_TEST_LOG"))
		return;
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

#include "saveid.c"

static int failures, checks;
static const char *ctx = "";
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
	printf("FAIL %s:%d: %s [%s]\n", __FILE__, __LINE__, #c, ctx); } } while (0)

#define SAVES PSX3D_SAVE_DIR

static void mkdirs(const char *path)
{
	char p[PATH_MAX];
	snprintf(p, sizeof(p), "%s", path);
	for (char *s = p + 1; *s; s++)
		if (*s == '/') {
			*s = 0;
			mkdir(p, 0777);
			*s = '/';
		}
	mkdir(p, 0777);
}

static void put(const char *path, const void *data, size_t n)
{
	char dir[PATH_MAX];
	snprintf(dir, sizeof(dir), "%s", path);
	*strrchr(dir, '/') = 0;
	mkdirs(dir);
	FILE *f = fopen(path, "wb");
	if (!f) {
		printf("cannot create %s\n", path);
		exit(2);
	}
	fwrite(data, 1, n, f);
	fclose(f);
}

static void put_text(const char *path, const char *text) { put(path, text, strlen(text)); }

// a disc data file of n bytes
static void disc(const char *path, size_t n)
{
	char *b = calloc(1, n ? n : 1);
	put(path, b, n);
	free(b);
}

// disc data file + cue sheet naming it
static void cue_disc(const char *cue, const char *bin_name, size_t n)
{
	char bin[PATH_MAX], text[PATH_MAX + 64];
	snprintf(bin, sizeof(bin), "%.*s%s", (int)(strrchr(cue, '/') + 1 - cue), cue, bin_name);
	disc(bin, n);
	snprintf(text, sizeof(text), "FILE \"%s\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n", bin_name);
	put_text(cue, text);
}

static bool exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

static long file_size(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static char *slurp(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *b = malloc(len + 1);
	*n = fread(b, 1, len, f);
	b[*n] = 0;
	fclose(f);
	return b;
}

static bool same_bytes(const char *path, const void *want, size_t n)
{
	size_t got;
	char *b = slurp(path, &got);
	bool ok = b && got == n && !memcmp(b, want, n);
	free(b);
	return ok;
}

// a 128 KiB memory card with saves under these product codes ("SLUS-00594")
static unsigned char cardbuf[128 * 1024];
static void card_with(const char *const *codes, int n, int seed)
{
	memset(cardbuf, 0, sizeof(cardbuf));
	memcpy(cardbuf, "MC", 2);
	for (int i = 1; i < 16; i++)
		cardbuf[128 * i] = 0xA0;
	for (int i = 0; i < n; i++) {
		unsigned char *e = cardbuf + 128 * (i + 1);
		e[0] = 0x51;
		snprintf((char *)e + 10, 21, "BA%sGAMEDATA", codes[i]);
	}
	for (size_t i = 128 * 16; i < sizeof(cardbuf); i++)
		cardbuf[i] = (unsigned char)(i * seed);
}

static void lib_clear(void)
{
	for (int i = 0; i < nlib; i++) {
		free(lib[i].title);
		free(lib[i].cover_key);
		for (int d = 0; d < lib[i].ndiscs; d++)
			free(lib[i].discs[d]);
	}
	memset(lib, 0, sizeof(lib));
	nlib = 0;
}

// a library game named key with these discs (what library.c makes: the
// cover key is disc 1's file name)
static game_t *lib_add(const char *key, int ndiscs, ...)
{
	game_t *g = &lib[nlib++];
	g->title = strdup(key);
	g->cover_key = strdup(key);
	va_list ap;
	va_start(ap, ndiscs);
	for (int d = 0; d < ndiscs; d++)
		g->discs[g->ndiscs++] = strdup(va_arg(ap, const char *));
	va_end(ap);
	return g;
}

// the playlist library_launch_path() writes for a multi-disc game: discs
// without "sdmc:" (the core takes "/x" as the SD card's x)
static const char *lib_m3u(const game_t *g)
{
	static char path[PATH_LEN];
	char text[4096] = "";
	snprintf(path, sizeof(path), PSX3D_DIR "/m3u/%s.m3u", g->cover_key);
	for (int d = 0; d < g->ndiscs; d++) {
		strcat(text, g->discs[d] + 5);
		strcat(text, "\n");
	}
	put_text(path, text);
	return path;
}

typedef struct { char begun[PATH_LEN], name[PATH_LEN]; bool changed, created; } launch_t;

// path started with save name base (library) or NULL (folder browser); serial
// as the core read it
static launch_t launch(const char *path, const char *base, const char *serial)
{
	launch_t l;
	saveid_begin(path, base, l.begun, sizeof(l.begun));
	snprintf(l.name, sizeof(l.name), "%s", l.begun);
	l.changed = saveid_confirm(serial, l.name, sizeof(l.name), &l.created);
	CHECK(l.changed == (strcmp(l.begun, l.name) != 0));
	return l;
}

static const game_t *lib_find(const char *key)
{
	for (int i = 0; i < nlib; i++)
		if (!strcmp(lib[i].cover_key, key))
			return &lib[i];
	return NULL;
}

// a library launch, as main.c does it
static launch_t lib_launch(const char *key, const char *serial)
{
	const game_t *g = lib_find(key);
	if (!g) {
		printf("no library game %s\n", key);
		exit(2);
	}
	return launch(g->ndiscs > 1 ? lib_m3u(g) : g->discs[0], g->cover_key, serial);
}

static void id_of(const char *name, rec_t *r) { rec_read(name, r); }

static void card_save(const char *name, int seed)
{
	char path[PATH_LEN];
	snprintf(path, sizeof(path), SAVES "/%s.srm", name);
	unsigned char *c = malloc(sizeof(cardbuf));
	for (size_t i = 0; i < sizeof(cardbuf); i++) c[i] = (unsigned char)(i * seed + 7);
	c[0] = 'M'; c[1] = 'C';
	put(path, c, sizeof(cardbuf));
	free(c);
}

static int count_ids(void)
{
	DIR *d = opendir(SAVES);
	int n = 0;
	struct dirent *de;
	while (d && (de = readdir(d)))
		n += strlen(de->d_name) > 3 && !strcmp(de->d_name + strlen(de->d_name) - 3, ".id");
	if (d)
		closedir(d);
	return n;
}

static void reset(void)
{
	lib_clear();
	if (system("rm -rf sdmc:"))
		exit(2);
	mkdirs(SAVES);
}

// ---------------------------------------------------------------- cases

// The review's case: Game A/game.bin and Game B/game.bin
static void reviewer(void)
{
	ctx = "reviewer";
	reset();
	cue_disc("sdmc:/roms/psx/Game A/game.cue", "game.bin", 1000);
	cue_disc("sdmc:/roms/psx/Game B/game.cue", "game.bin", 2000);
	lib_add("game", 1, "sdmc:/roms/psx/Game A/game.cue");
	lib_add("game", 1, "sdmc:/roms/psx/Game B/game.cue");

	launch_t a = lib_launch("game", "SLUS00001");
	CHECK(!strcmp(a.name, "game") && !a.created && !a.changed);
	CHECK(exists(SAVES "/game.id"));
	card_save("game", 3);
	size_t n;
	char *card_a = slurp(SAVES "/game.srm", &n);

	// B: same file name, other game -> its own saves, A's untouched
	game_t *gb = &lib[1];
	launch_t b = launch(gb->discs[0], gb->cover_key, "SLUS00002");
	CHECK(!strcmp(b.begun, "game"));               // not known before loading
	CHECK(!strcmp(b.name, "game [SLUS00002]") && b.created && b.changed);
	CHECK(same_bytes(SAVES "/game.srm", card_a, n));
	card_save(b.name, 5);
	CHECK(same_bytes(SAVES "/game.srm", card_a, n));

	// again: A keeps "game", B finds its name before loading
	a = lib_launch("game", "SLUS00001");
	CHECK(!strcmp(a.name, "game") && !a.changed && !a.created);
	b = launch(gb->discs[0], gb->cover_key, "SLUS00002");
	CHECK(!strcmp(b.begun, "game [SLUS00002]") && !b.changed && !b.created);

	// the folder browser agrees: the cue, and the bin itself
	a = launch("sdmc:/roms/psx/Game A/game.cue", NULL, "SLUS00001");
	CHECK(!strcmp(a.name, "game"));
	b = launch("sdmc:/roms/psx/Game B/game.cue", NULL, "SLUS00002");
	CHECK(!strcmp(b.begun, "game [SLUS00002]") && !strcmp(b.name, "game [SLUS00002]"));
	b = launch("sdmc:/roms/psx/Game B/game.bin", NULL, "SLUS00002");
	CHECK(!strcmp(b.name, "game [SLUS00002]") && !b.created);
	a = launch("sdmc:/roms/psx/Game A/game.bin", NULL, "SLUS00001");
	CHECK(!strcmp(a.name, "game"));

	// outside the library too (no library at all)
	lib_clear();
	b = launch("sdmc:/roms/psx/Game B/game.cue", NULL, "SLUS00002");
	CHECK(!strcmp(b.begun, "game [SLUS00002]") && !strcmp(b.name, "game [SLUS00002]"));
	a = launch("sdmc:/roms/psx/Game A/game.cue", NULL, "SLUS00001");
	CHECK(!strcmp(a.name, "game"));
	CHECK(same_bytes(SAVES "/game.srm", card_a, n));
	CHECK(count_ids() == 2);
	free(card_a);
}

// track01.bin / game.bin without a serial: told apart by size
static void generic_names(void)
{
	ctx = "generic";
	reset();
	disc("sdmc:/roms/one/track01.bin", 4000);
	disc("sdmc:/roms/two/track01.bin", 5000);
	disc("sdmc:/roms/copy/track01.bin", 4000);   // the first one, copied
	launch_t a = launch("sdmc:/roms/one/track01.bin", NULL, "SLUS99999");
	CHECK(!strcmp(a.name, "track01") && !a.created);
	launch_t b = launch("sdmc:/roms/two/track01.bin", NULL, "SLUS99999");
	CHECK(!strncmp(b.name, "track01 [", 9) && strlen(b.name) == strlen("track01 [12345678]") && b.created);
	launch_t c = launch("sdmc:/roms/copy/track01.bin", NULL, "SLUS99999");
	CHECK(!strcmp(c.name, "track01") && !c.created);   // same data: same game
	launch_t b2 = launch("sdmc:/roms/two/track01.bin", NULL, "");
	CHECK(!strcmp(b2.name, b.name) && !strcmp(b2.begun, b.name));
	// names made from the EXE are not serials
	launch_t d = launch("sdmc:/roms/two/track01.bin", NULL, "MAINEXE");
	CHECK(!strcmp(d.name, b.name));

	// three games: game.bin, three sizes, two with serials
	disc("sdmc:/x/1/game.bin", 100);
	disc("sdmc:/x/2/game.bin", 200);
	disc("sdmc:/x/3/game.bin", 300);
	launch_t g1 = launch("sdmc:/x/1/game.bin", NULL, "SCES00010");
	launch_t g2 = launch("sdmc:/x/2/game.bin", NULL, "SCES00020");
	launch_t g3 = launch("sdmc:/x/3/game.bin", NULL, "");
	CHECK(!strcmp(g1.name, "game") && !strcmp(g2.name, "game [SCES00020]") && !strncmp(g3.name, "game [", 6));
	CHECK(strcmp(g3.name, g2.name) && g3.created);
	CHECK(!strcmp(launch("sdmc:/x/3/game.bin", NULL, "").begun, g3.name));
	CHECK(!strcmp(launch("sdmc:/x/2/game.bin", NULL, "SCES00020").begun, g2.name));
	CHECK(!strcmp(launch("sdmc:/x/1/game.bin", NULL, "SCES00010").name, "game"));
}

// Saves from before .id files: the first game started with the name adopts
// them; with a twin in the library, the memory card decides
static void legacy(void)
{
	ctx = "legacy";
	reset();
	disc("sdmc:/roms/Legacy.bin", 700);
	const char *codes[] = { "SLUS-00010" };
	card_with(codes, 1, 9);
	put(SAVES "/Legacy.srm", cardbuf, sizeof(cardbuf));
	put_text(PSX3D_DIR "/states/Legacy.sta", "state");
	launch_t l = launch("sdmc:/roms/Legacy.bin", NULL, "SLUS00010");
	CHECK(!strcmp(l.name, "Legacy") && !l.created && !l.changed);
	CHECK(same_bytes(SAVES "/Legacy.srm", cardbuf, sizeof(cardbuf)));
	rec_t r;
	id_of("Legacy", &r);
	CHECK(r.ok && r.size == 700 && !strcmp(r.first, "SLUS00010") && rec_has(&r, "SLUS00010"));
	CHECK(!strcmp(r.disc, "/roms/legacy.bin"));

	// the common case: unique name, card of another serial, no twin -> adopted
	disc("sdmc:/roms/Solo.bin", 701);
	const char *other[] = { "SLUS-00999" };
	card_with(other, 1, 4);
	put(SAVES "/Solo.srm", cardbuf, sizeof(cardbuf));
	CHECK(!strcmp(launch("sdmc:/roms/Solo.bin", NULL, "SLUS00011").name, "Solo"));
	CHECK(same_bytes(SAVES "/Solo.srm", cardbuf, sizeof(cardbuf)));

	// twins, card says A: B started first gets its own name, A then adopts
	reset();
	cue_disc("sdmc:/roms/A/dup.cue", "dup.bin", 800);
	cue_disc("sdmc:/roms/B/dup.cue", "dup.bin", 900);
	lib_add("dup", 1, "sdmc:/roms/A/dup.cue");
	lib_add("dup", 1, "sdmc:/roms/B/dup.cue");
	const char *a_codes[] = { "SLUS-00020", "SLUS-00020" };
	card_with(a_codes, 2, 2);
	put(SAVES "/dup.srm", cardbuf, sizeof(cardbuf));
	launch_t b = launch(lib[1].discs[0], "dup", "SLUS00021");
	CHECK(!strcmp(b.name, "dup [SLUS00021]") && b.created);
	CHECK(!exists(SAVES "/dup.id"));
	launch_t a = launch(lib[0].discs[0], "dup", "SLUS00020");
	CHECK(!strcmp(a.name, "dup") && !a.created);
	CHECK(same_bytes(SAVES "/dup.srm", cardbuf, sizeof(cardbuf)));
	CHECK(!strcmp(launch(lib[1].discs[0], "dup", "SLUS00021").begun, "dup [SLUS00021]"));

	// twins, card holds both games' saves: the first one started adopts
	reset();
	cue_disc("sdmc:/roms/A/dup.cue", "dup.bin", 800);
	cue_disc("sdmc:/roms/B/dup.cue", "dup.bin", 900);
	lib_add("dup", 1, "sdmc:/roms/A/dup.cue");
	lib_add("dup", 1, "sdmc:/roms/B/dup.cue");
	const char *both[] = { "SLUS-00020", "SLUS-00021" };
	card_with(both, 2, 2);
	put(SAVES "/dup.srm", cardbuf, sizeof(cardbuf));
	b = launch(lib[1].discs[0], "dup", "SLUS00021");
	CHECK(!strcmp(b.name, "dup"));
	a = launch(lib[0].discs[0], "dup", "SLUS00020");
	CHECK(!strcmp(a.name, "dup [SLUS00020]") && a.created);

	// twins, empty card / no card / states only: the first one adopts
	reset();
	cue_disc("sdmc:/roms/A/dup.cue", "dup.bin", 800);
	cue_disc("sdmc:/roms/B/dup.cue", "dup.bin", 900);
	lib_add("dup", 1, "sdmc:/roms/A/dup.cue");
	lib_add("dup", 1, "sdmc:/roms/B/dup.cue");
	card_with(NULL, 0, 2);
	put(SAVES "/dup.srm", cardbuf, sizeof(cardbuf));
	CHECK(!strcmp(launch(lib[1].discs[0], "dup", "SLUS00021").name, "dup"));
	CHECK(!strcmp(launch(lib[0].discs[0], "dup", "SLUS00020").name, "dup [SLUS00020]"));

	// twins, card of a third game only: B gets its own name, A (the last one
	// without a name) still adopts
	reset();
	cue_disc("sdmc:/roms/A/dup.cue", "dup.bin", 800);
	cue_disc("sdmc:/roms/B/dup.cue", "dup.bin", 900);
	lib_add("dup", 1, "sdmc:/roms/A/dup.cue");
	lib_add("dup", 1, "sdmc:/roms/B/dup.cue");
	const char *third[] = { "SLUS-00777" };
	card_with(third, 1, 2);
	put(SAVES "/dup.srm", cardbuf, sizeof(cardbuf));
	CHECK(!strcmp(launch(lib[1].discs[0], "dup", "SLUS00021").name, "dup [SLUS00021]"));
	CHECK(!strcmp(launch(lib[0].discs[0], "dup", "SLUS00020").name, "dup"));
	CHECK(same_bytes(SAVES "/dup.srm", cardbuf, sizeof(cardbuf)));

	// file names differing in case only are the same save name (FAT)
	reset();
	cue_disc("sdmc:/roms/A/Case.cue", "Case.bin", 800);
	cue_disc("sdmc:/roms/B/case.cue", "case.bin", 900);
	lib_add("Case", 1, "sdmc:/roms/A/Case.cue");
	lib_add("case", 1, "sdmc:/roms/B/case.cue");
	card_with(a_codes, 2, 2);
	put(SAVES "/Case.srm", cardbuf, sizeof(cardbuf));
	put(SAVES "/case.srm", cardbuf, sizeof(cardbuf));   // one file on FAT
	CHECK(!strcmp(launch(lib[1].discs[0], "case", "SLUS00021").name, "case [SLUS00021]"));
	CHECK(!strcmp(launch(lib[0].discs[0], "Case", "SLUS00020").name, "Case"));

	// a damaged / wrong-size card is no evidence: adopted
	reset();
	cue_disc("sdmc:/roms/A/dup.cue", "dup.bin", 800);
	cue_disc("sdmc:/roms/B/dup.cue", "dup.bin", 900);
	lib_add("dup", 1, "sdmc:/roms/A/dup.cue");
	lib_add("dup", 1, "sdmc:/roms/B/dup.cue");
	card_with(a_codes, 2, 2);
	put(SAVES "/dup.srm", cardbuf, 5000);
	CHECK(!strcmp(launch(lib[1].discs[0], "dup", "SLUS00021").name, "dup"));
}

// renaming: a new file name is a new (empty) save name, the old files stay;
// taking another game's name is a collision
static void renamed(void)
{
	ctx = "renamed";
	reset();
	disc("sdmc:/roms/Old Name.bin", 1234);
	CHECK(!strcmp(launch("sdmc:/roms/Old Name.bin", NULL, "SLUS00100").name, "Old Name"));
	card_save("Old Name", 2);
	if (rename("sdmc:/roms/Old Name.bin", "sdmc:/roms/New Name.bin"))
		exit(2);
	CHECK(!strcmp(launch("sdmc:/roms/New Name.bin", NULL, "SLUS00100").name, "New Name"));
	CHECK(exists(SAVES "/Old Name.srm") && exists(SAVES "/Old Name.id"));
	// another game renamed to "Old Name"
	disc("sdmc:/roms/other/Old Name.bin", 4321);
	launch_t o = launch("sdmc:/roms/other/Old Name.bin", NULL, "SLUS00200");
	CHECK(!strcmp(o.name, "Old Name [SLUS00200]") && o.created);
	// the first one renamed back: still the owner
	if (rename("sdmc:/roms/New Name.bin", "sdmc:/roms/Old Name.bin"))
		exit(2);
	CHECK(!strcmp(launch("sdmc:/roms/Old Name.bin", NULL, "SLUS00100").name, "Old Name"));
	// FAT ignores case: "old name.bin" is the same save name
	disc("sdmc:/roms/lower/old name.bin", 1234);
	CHECK(!strcmp(launch("sdmc:/roms/lower/old name.bin", NULL, "SLUS00100").name, "old name"));
}

// moved and converted games keep their saves
static void moved(void)
{
	ctx = "moved";
	reset();
	cue_disc("sdmc:/roms/here/Moving.cue", "Moving.bin", 3000);
	CHECK(!strcmp(launch("sdmc:/roms/here/Moving.cue", NULL, "SLES01000").name, "Moving"));
	disc("sdmc:/roms/there/deeper/Moving.bin", 3000);
	CHECK(!strcmp(launch("sdmc:/roms/there/deeper/Moving.bin", NULL, "SLES01000").name, "Moving"));
	// converted to .chd (other size, same serial)
	disc("sdmc:/roms/chd/Moving.chd", 1700);
	launch_t c = launch("sdmc:/roms/chd/Moving.chd", NULL, "SLES01000");
	CHECK(!strcmp(c.name, "Moving") && !c.created);
	// converted, but a different game (other serial)
	disc("sdmc:/roms/fake/Moving.chd", 1800);
	c = launch("sdmc:/roms/fake/Moving.chd", NULL, "SLES02000");
	CHECK(!strcmp(c.name, "Moving [SLES02000]") && c.created);
	// same size, other disc 1 serial: another game
	disc("sdmc:/roms/same/Moving.bin", 3000);
	c = launch("sdmc:/roms/same/Moving.bin", NULL, "SLES03000");
	CHECK(!strcmp(c.name, "Moving [SLES03000]") && c.created);
	rec_t r;
	id_of("Moving", &r);
	CHECK(r.size == 3000 && !strcmp(r.disc, "/roms/here/moving.bin"));
}

// discs of one game share disc 1's name: library playlist, browser disc 2
static void multi_disc(void)
{
	ctx = "multi-disc";
	reset();
	cue_disc("sdmc:/roms/Epic/CD1/Epic (USA) (Disc 1).cue", "Epic (USA) (Disc 1).bin", 6000);
	cue_disc("sdmc:/roms/Epic/CD2/Epic (USA) (Disc 2).cue", "Epic (USA) (Disc 2).bin", 6100);
	lib_add("Epic (USA) (Disc 1)", 2, "sdmc:/roms/Epic/CD1/Epic (USA) (Disc 1).cue",
		"sdmc:/roms/Epic/CD2/Epic (USA) (Disc 2).cue");
	// disc 2 from the browser first
	launch_t d2 = launch("sdmc:/roms/Epic/CD2/Epic (USA) (Disc 2).cue", NULL, "SLUS00402");
	CHECK(!strcmp(d2.name, "Epic (USA) (Disc 1)") && !d2.created);
	rec_t r;
	id_of("Epic (USA) (Disc 1)", &r);
	CHECK(r.size == 6000 && !r.first[0] && rec_has(&r, "SLUS00402"));
	// the library (playlist, disc 1)
	launch_t l = lib_launch("Epic (USA) (Disc 1)", "SLUS00401");
	CHECK(!strcmp(l.name, "Epic (USA) (Disc 1)") && !l.created);
	id_of("Epic (USA) (Disc 1)", &r);
	CHECK(!strcmp(r.first, "SLUS00401") && rec_has(&r, "SLUS00401") && rec_has(&r, "SLUS00402"));
	CHECK(!strcmp(launch("sdmc:/roms/Epic/CD1/Epic (USA) (Disc 1).cue", NULL, "SLUS00401").name, "Epic (USA) (Disc 1)"));
	CHECK(!strcmp(launch("sdmc:/roms/Epic/CD2/Epic (USA) (Disc 2).bin", NULL, "SLUS00402").name, "Epic (USA) (Disc 1)"));
	CHECK(count_ids() == 1);

	// a playlist game: its discs from the browser use the playlist's name
	put_text("sdmc:/roms/FF/Fantasy.m3u", "CD1/Fantasy (Disc 1).cue\nCD2/Fantasy (Disc 2).cue\n");
	cue_disc("sdmc:/roms/FF/CD1/Fantasy (Disc 1).cue", "Fantasy (Disc 1).bin", 7000);
	cue_disc("sdmc:/roms/FF/CD2/Fantasy (Disc 2).cue", "Fantasy (Disc 2).bin", 7100);
	lib_add("Fantasy", 1, "sdmc:/roms/FF/Fantasy.m3u");
	CHECK(!strcmp(lib_launch("Fantasy", "SCUS94163").name, "Fantasy"));
	CHECK(!strcmp(launch("sdmc:/roms/FF/CD2/Fantasy (Disc 2).cue", NULL, "SCUS94164").name, "Fantasy"));
	CHECK(!strcmp(launch("sdmc:/roms/FF/CD1/Fantasy (Disc 1).cue", NULL, "SCUS94163").name, "Fantasy"));
	CHECK(!strcmp(launch("sdmc:/roms/FF/Fantasy.m3u", NULL, "SCUS94163").name, "Fantasy"));

	// outside the library: disc 2 next to its disc 1
	lib_clear();
	disc("sdmc:/loose/Saga (Disc 1).bin", 500);
	disc("sdmc:/loose/Saga (Disc 2).bin", 510);
	disc("sdmc:/loose/Lone (Disc 2).bin", 520);   // no disc 1 here
	CHECK(!strcmp(launch("sdmc:/loose/Saga (Disc 2).bin", NULL, "SLES00002").name, "Saga (Disc 1)"));
	CHECK(!strcmp(launch("sdmc:/loose/Saga (Disc 1).bin", NULL, "SLES00001").name, "Saga (Disc 1)"));
	CHECK(!strcmp(launch("sdmc:/loose/Lone (Disc 2).bin", NULL, "SLES00003").name, "Lone (Disc 2)"));

	// two editions with the same disc names in different folders
	cue_disc("sdmc:/ed/A/Twin (Disc 1).cue", "Twin (Disc 1).bin", 900);
	cue_disc("sdmc:/ed/A/Twin (Disc 2).cue", "Twin (Disc 2).bin", 901);
	cue_disc("sdmc:/ed/B/Twin (Disc 1).cue", "Twin (Disc 1).bin", 950);
	cue_disc("sdmc:/ed/B/Twin (Disc 2).cue", "Twin (Disc 2).bin", 951);
	lib_add("Twin (Disc 1)", 2, "sdmc:/ed/A/Twin (Disc 1).cue", "sdmc:/ed/A/Twin (Disc 2).cue");
	lib_add("Twin (Disc 1)", 2, "sdmc:/ed/B/Twin (Disc 1).cue", "sdmc:/ed/B/Twin (Disc 2).cue");
	CHECK(!strcmp(launch(lib_m3u(&lib[0]), "Twin (Disc 1)", "SLUS01001").name, "Twin (Disc 1)"));
	launch_t e = launch(lib_m3u(&lib[1]), "Twin (Disc 1)", "SLES01001");
	CHECK(!strcmp(e.name, "Twin (Disc 1) [SLES01001]") && e.created);
	CHECK(!strcmp(launch("sdmc:/ed/B/Twin (Disc 2).cue", NULL, "SLES01002").name, "Twin (Disc 1) [SLES01001]"));
	CHECK(!strcmp(launch("sdmc:/ed/A/Twin (Disc 2).cue", NULL, "SLUS01002").name, "Twin (Disc 1)"));
}

// the record file: damaged, unwritable, odd sizes
static void records(void)
{
	ctx = "records";
	reset();
	disc("sdmc:/r/Junk.bin", 10);
	put_text(SAVES "/Junk.id", "garbage\nsize=\n");
	CHECK(!strcmp(launch("sdmc:/r/Junk.bin", NULL, "SLUS00001").name, "Junk"));
	rec_t r;
	id_of("Junk", &r);
	CHECK(r.size == 10 && !strcmp(r.first, "SLUS00001"));

	// a replace interrupted after the old record moved away: recovered
	disc("sdmc:/r/Cut.bin", 20);
	CHECK(!strcmp(launch("sdmc:/r/Cut.bin", NULL, "SLUS00002").name, "Cut"));
	if (rename(SAVES "/Cut.id", SAVES "/Cut.id.bak"))
		exit(2);
	disc("sdmc:/r/other/Cut.bin", 21);
	CHECK(!strcmp(launch("sdmc:/r/other/Cut.bin", NULL, "SLUS00003").name, "Cut [SLUS00003]"));

	// cannot write the record: the game still starts, nothing else is touched
	disc("sdmc:/r/Ro.bin", 30);
	chmod(SAVES, 0555);
	if (access(SAVES, W_OK) != 0) {
		launch_t l = launch("sdmc:/r/Ro.bin", NULL, "SLUS00004");
		CHECK(!strcmp(l.name, "Ro") && !exists(SAVES "/Ro.id"));
	}
	chmod(SAVES, 0777);

	// a missing disc file (size unknown) is not told apart: as before
	launch_t m = launch("sdmc:/r/missing.bin", NULL, "");
	CHECK(!strcmp(m.name, "missing"));
	disc("sdmc:/r/x/missing.bin", 40);
	CHECK(!strcmp(launch("sdmc:/r/x/missing.bin", NULL, "").name, "missing"));
	id_of("missing", &r);
	CHECK(r.size == 40);

	// a name already taken by yet another game: the next one
	disc("sdmc:/r/Taken.bin", 50);
	disc("sdmc:/r/t2/Taken.bin", 51);
	CHECK(!strcmp(launch("sdmc:/r/Taken.bin", NULL, "SLUS00005").name, "Taken"));
	put_text(SAVES "/Taken [SLUS00006].id", "version=1\nsize=999\nfirst=SLUS00007\nserial=SLUS00007\n");
	launch_t t = launch("sdmc:/r/t2/Taken.bin", NULL, "SLUS00006");
	CHECK(!strcmp(t.name, "Taken [SLUS00006]-2") && t.created);
	CHECK(!strcmp(launch("sdmc:/r/t2/Taken.bin", NULL, "SLUS00006").name, "Taken [SLUS00006]-2"));

	// many games with one name and one serial-less size each
	for (int i = 0; i < 20; i++) {
		char p[64];
		snprintf(p, sizeof(p), "sdmc:/many/%d/m.bin", i);
		disc(p, 100 + i);
		launch_t l = launch(p, NULL, "");
		CHECK(i ? strncmp(l.name, "m [", 3) == 0 : !strcmp(l.name, "m"));
		CHECK(!strcmp(launch(p, NULL, "").begun, l.name));
	}
	CHECK(file_size(SAVES "/m.id") > 0);
}

// serial spellings and the memory card reader
static void serials(void)
{
	ctx = "serials";
	char s[10];
	serial_norm("SLUS00594", s); CHECK(!strcmp(s, "SLUS00594"));
	serial_norm("slus-005.94", s); CHECK(!strcmp(s, "SLUS00594"));
	serial_norm("SLUS99999", s); CHECK(!s[0]);
	serial_norm("MAINEXE", s); CHECK(!s[0]);
	serial_norm("SLUS005941", s); CHECK(!s[0]);
	serial_norm("", s); CHECK(!s[0]);
	serial_norm(NULL, s); CHECK(!s[0]);
	reset();
	snprintf(cur.base, sizeof(cur.base), "c");
	const char *codes[] = { "SCUS-94163", "SLUS-00594" };
	card_with(codes, 2, 1);
	put(SAVES "/c.srm", cardbuf, sizeof(cardbuf));
	CHECK(!card_of_others("SLUS00594"));
	CHECK(!card_of_others("SCUS94163"));
	CHECK(card_of_others("SLUS00001"));
	cardbuf[128 * 2] = 0x52;   // a middle block only: not a save's first block
	put(SAVES "/c.srm", cardbuf, sizeof(cardbuf));
	CHECK(card_of_others("SLUS00594"));
	cardbuf[128] = 0xA1;       // deleted
	put(SAVES "/c.srm", cardbuf, sizeof(cardbuf));
	CHECK(!card_of_others("SLUS00001"));
}

int main(void)
{
	char dir[] = "/tmp/saveid-XXXXXX";
	if (!mkdtemp(dir) || chdir(dir))
		return 2;
	reviewer();
	generic_names();
	legacy();
	renamed();
	moved();
	multi_disc();
	records();
	serials();
	lib_clear();
	if (chdir("/"))
		return 2;
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
	if (system(cmd)) {}
	printf("saveid: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
