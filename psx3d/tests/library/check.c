// Host test of the game library scanner: runs the real psx3d/source/library.c
// (and safefile.c) on fixture folders built in a temp directory.
//
// "sdmc:/..." paths are plain relative folders here, so the test chdirs into
// its temp directory and the frontend's PSX3D_DIR becomes ./sdmc:/3ds/PSX3D.
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

// the few frontend calls library.c makes
static u64 osGetTime(void) { return 0; }
void log_printf(const char *fmt, ...)
{
	if (!getenv("LIBRARY_TEST_LOG"))
		return;
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

#include "library.c"

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
	printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

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

// root/rel with text in it (parents are made)
static void put(const char *root, const char *rel, const char *text)
{
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", root, rel);
	char *slash = strrchr(path, '/');
	*slash = 0;
	mkdirs(path);
	*slash = '/';
	FILE *f = fopen(path, "wb");
	if (!f) {
		printf("cannot create %s\n", path);
		exit(2);
	}
	fputs(text, f);
	fclose(f);
}

static const game_t *find(const char *title)
{
	for (int i = 0; i < library_count(); i++)
		if (!strcmp(library_get(i)->title, title))
			return library_get(i);
	return NULL;
}

static bool ends_with(const char *s, const char *tail)
{
	size_t a = strlen(s), b = strlen(tail);
	return a >= b && !strcmp(s + a - b, tail);
}

static bool starts_disc(const char *title, const char *tail)
{
	const game_t *g = find(title);
	return g && g->ndiscs >= 1 && ends_with(g->discs[0], tail);
}

static void scan(const char *root)
{
	library_load(root, true);
}

static int case_no;
static char root[PATH_MAX];
// a fresh, empty root for the next case (relative: "sdmc:/lib/<n>")
static const char *fresh(void)
{
	snprintf(root, sizeof(root), "sdmc:/lib/%d", ++case_no);
	mkdirs(root);
	return root;
}

static void test_reviewer_mixed(void)
{
	// the reviewer's repro: a playlist must not hide an unrelated game
	const char *r = fresh();
	put(r, "Final Fantasy VII.m3u", "Final Fantasy VII (Disc 1).chd\n");
	put(r, "Final Fantasy VII (Disc 1).chd", "x");
	put(r, "Crash.chd", "x");
	scan(r);
	CHECK(library_count() == 2);
	CHECK(starts_disc("Crash", "/Crash.chd"));
	CHECK(starts_disc("Final Fantasy VII", "/Final Fantasy VII.m3u"));
	CHECK(library_omitted() == 0);
}

static void test_cue_and_bins(void)
{
	const char *r = fresh();
	put(r, "Game.cue", "FILE \"Game.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");
	put(r, "Game.bin", "x");
	put(r, "Other.bin", "x");
	scan(r);
	CHECK(library_count() == 2);
	CHECK(starts_disc("Game", "/Game.cue"));
	CHECK(starts_disc("Other", "/Other.bin"));

	// two cues with their own (multi-track) bins
	r = fresh();
	put(r, "A.cue", "FILE \"A (Track 1).bin\" BINARY\nTRACK 01 MODE2/2352\nFILE \"A (Track 2).bin\" BINARY\nTRACK 02 AUDIO\n");
	put(r, "A (Track 1).bin", "x");
	put(r, "A (Track 2).bin", "x");
	put(r, "B.cue", "FILE \"B.bin\" BINARY\n");
	put(r, "B.bin", "x");
	scan(r);
	CHECK(library_count() == 2);
	CHECK(starts_disc("A", "/A.cue"));
	CHECK(starts_disc("B", "/B.cue"));
}

static void test_playlists(void)
{
	// a playlist pointing at cues in subfolders: one game, nothing twice
	const char *r = fresh();
	put(r, "Game.m3u", "Disc 1/Game (Disc 1).cue\nDisc 2/Game (Disc 2).cue\n");
	put(r, "Disc 1/Game (Disc 1).cue", "FILE \"Game (Disc 1).bin\" BINARY\n");
	put(r, "Disc 1/Game (Disc 1).bin", "x");
	put(r, "Disc 2/Game (Disc 2).cue", "FILE \"Game (Disc 2).bin\" BINARY\n");
	put(r, "Disc 2/Game (Disc 2).bin", "x");
	scan(r);
	CHECK(library_count() == 1);
	CHECK(starts_disc("Game", "/Game.m3u"));
	CHECK(library_get(0)->ndiscs == 1);

	// several playlists in one folder, and a loose game next to them
	r = fresh();
	put(r, "A.m3u", "A (Disc 1).chd\nA (Disc 2).chd\n");
	put(r, "A (Disc 1).chd", "x");
	put(r, "A (Disc 2).chd", "x");
	put(r, "B.m3u", "B.chd\n");
	put(r, "B.chd", "x");
	put(r, "C.chd", "x");
	scan(r);
	CHECK(library_count() == 3);
	CHECK(find("A") && find("B") && find("C"));
	CHECK(starts_disc("C", "/C.chd"));

	// a cue that nothing names stays next to a playlist
	r = fresh();
	put(r, "P.m3u", "P.chd\n");
	put(r, "P.chd", "x");
	put(r, "Loose.cue", "FILE \"Loose.bin\" BINARY\n");
	put(r, "Loose.bin", "x");
	scan(r);
	CHECK(library_count() == 2);
	CHECK(starts_disc("P", "/P.m3u"));
	CHECK(starts_disc("Loose", "/Loose.cue"));

	// comments, blank lines, CRLF, labels, spaces around the path
	r = fresh();
	put(r, "Q.m3u", "\xEF\xBB\xBF#EXTM3U\r\n\r\n#EXTINF:0,Q\r\n  Q (Disc 1).chd  \r\nQ (Disc 2).chd|Second disc\r\n");
	put(r, "Q (Disc 1).chd", "x");
	put(r, "Q (Disc 2).chd", "x");
	scan(r);
	CHECK(library_count() == 1);
	CHECK(starts_disc("Q", "/Q.m3u"));

	// a playlist that names itself still shows
	r = fresh();
	put(r, "S.m3u", "S.m3u\nS.chd\n");
	put(r, "S.chd", "x");
	scan(r);
	CHECK(library_count() == 1);
	CHECK(starts_disc("S", "/S.m3u"));
}

static void test_cue_syntax(void)
{
	const char *r = fresh();
	put(r, "My Game.cue",
		"REM comment\r\n"
		"FILE \"My Game (Track 1).bin\" BINARY\r\n"
		"  TRACK 01 MODE2/2352\r\n"
		"    INDEX 01 00:00:00\r\n"
		"FILE My Game (Track 2).bin BINARY\r\n"
		"  TRACK 02 AUDIO\r\n"
		"file 'x' junk\r\n"
		"   FILE   \"music.wav\"   WAVE\r\n"
		"FILE plain.bin BINARY\r\n");
	put(r, "My Game (Track 1).bin", "x");
	put(r, "My Game (Track 2).bin", "x");
	put(r, "music.wav", "x");
	put(r, "plain.bin", "x");
	put(r, "Stray.bin", "x");
	scan(r);
	CHECK(library_count() == 2);
	CHECK(starts_disc("My Game", "/My Game.cue"));
	CHECK(starts_disc("Stray", "/Stray.bin"));

	// only CR line ends, and a byte order mark
	r = fresh();
	put(r, "Mac.cue", "\xEF\xBB\xBF" "FILE \"Mac.bin\" BINARY\rTRACK 01 MODE2/2352\r");
	put(r, "Mac.bin", "x");
	scan(r);
	CHECK(library_count() == 1);

	// a list longer than the read limit: what was read still counts
	r = fresh();
	char *big = malloc(REF_BYTES * 2);
	size_t n = snprintf(big, REF_BYTES, "FILE \"Big.bin\" BINARY\n");
	while (n < REF_BYTES + 100)
		n += sprintf(big + n, "REM padding padding padding padding padding padding padding\n");
	put(r, "Big.cue", big);
	free(big);
	put(r, "Big.bin", "x");
	scan(r);
	CHECK(library_count() == 1);
	CHECK(omit.refs == 1);
}

static void test_names_and_paths(void)
{
	// FAT: case does not matter; either slash
	const char *r = fresh();
	put(r, "game.cue", "FILE \"GAME.BIN\" BINARY\n");
	put(r, "Game.bin", "x");
	put(r, "Set.m3u", "SUB\\DISC.CHD\n");
	put(r, "sub/disc.chd", "x");
	scan(r);
	CHECK(library_count() == 2);
	CHECK(starts_disc("game", "/game.cue"));
	CHECK(starts_disc("Set", "/Set.m3u"));

	// absolute names, with and without "sdmc:"; "." and ".." parts
	r = fresh();
	char list[PATH_MAX * 2];
	snprintf(list, sizeof(list), "%s/a.chd\n/lib/%d/b.chd\n./sub/../c.chd\nsub/../../%d/d.chd\n", r, case_no, case_no);
	put(r, "All.m3u", list);
	put(r, "a.chd", "x");
	put(r, "b.chd", "x");
	put(r, "c.chd", "x");
	put(r, "d.chd", "x");
	put(r, "e.chd", "x");
	scan(r);
	CHECK(library_count() == 2);
	CHECK(starts_disc("All", "/All.m3u"));
	CHECK(starts_disc("e", "/e.chd"));

	// a playlist in a subfolder reaching up
	r = fresh();
	put(r, "sub/Up.m3u", "../Up.chd\n");
	put(r, "Up.chd", "x");
	put(r, "Free.chd", "x");
	scan(r);
	CHECK(library_count() == 2);
	CHECK(starts_disc("Up", "/sub/Up.m3u"));
	CHECK(starts_disc("Free", "/Free.chd"));

	// a real absolute host path as the games folder
	char abs_root[PATH_MAX];
	char cwd[PATH_MAX];
	CHECK(getcwd(cwd, sizeof(cwd)));
	snprintf(abs_root, sizeof(abs_root), "%s/abs", cwd);
	mkdirs(abs_root);
	snprintf(list, sizeof(list), "%s/Z.chd\n", abs_root);
	put(abs_root, "Z.m3u", list);
	put(abs_root, "Z.chd", "x");
	put(abs_root, "Y.chd", "x");
	scan(abs_root);
	CHECK(library_count() == 2);
	CHECK(starts_disc("Z", "/Z.m3u"));

	// path comparison itself
	char a[PATH_MAX], b[PATH_MAX];
	norm_path("sdmc:/Games/X", "../y/./Z.BIN", a, sizeof(a));
	norm_path("", "/games/y/z.bin", b, sizeof(b));
	CHECK(!strcmp(a, b));
	norm_path("", "SDMC:/games/y/z.bin", a, sizeof(a));
	CHECK(!strcmp(a, b));
}

static void test_groups_and_titles(void)
{
	// grouping, clean titles and article fixing are unchanged
	const char *r = fresh();
	put(r, "Metal Gear Solid (USA) (Disc 2).cue", "FILE \"Metal Gear Solid (USA) (Disc 2).bin\" BINARY\n");
	put(r, "Metal Gear Solid (USA) (Disc 2).bin", "x");
	put(r, "Metal Gear Solid (USA) (Disc 1).cue", "FILE \"Metal Gear Solid (USA) (Disc 1).bin\" BINARY\n");
	put(r, "Metal Gear Solid (USA) (Disc 1).bin", "x");
	put(r, "Legend of Dragoon, The (USA).chd", "x");
	scan(r);
	CHECK(library_count() == 2);
	const game_t *g = find("Metal Gear Solid");
	CHECK(g && g->ndiscs == 2);
	CHECK(g && ends_with(g->discs[0], "(Disc 1).cue") && ends_with(g->discs[1], "(Disc 2).cue"));
	CHECK(g && !g->group);
	CHECK(find("The Legend of Dragoon"));
}

static void test_limits(void)
{
	// folders nested to the depth limit and one beyond
	const char *r = fresh();
	put(r, "r.chd", "x");
	put(r, "a/a.chd", "x");
	put(r, "a/b/b.chd", "x");
	put(r, "a/b/c/c.chd", "x");
	put(r, "a/b/c/d/d1.chd", "x");
	put(r, "a/b/c/d/d2.chd", "x");
	put(r, "a/b/c/d/notes.txt", "x");
	scan(r);
	CHECK(library_count() == 4);
	CHECK(find("r") && find("a") && find("b") && find("c"));
	CHECK(!find("d1"));
	CHECK(library_omitted() == 2);

	// nothing of interest below the limit: nothing reported
	r = fresh();
	put(r, "a/b/c/c.chd", "x");
	put(r, "a/b/c/d/readme.txt", "x");
	scan(r);
	CHECK(library_count() == 1);
	CHECK(library_omitted() == 0);

	// more discs than a game holds
	r = fresh();
	for (int d = 1; d <= 10; d++) {
		char name[64];
		snprintf(name, sizeof(name), "Big (Disc %d).chd", d);
		put(r, name, "x");
	}
	scan(r);
	CHECK(library_count() == 1);
	CHECK(library_get(0)->ndiscs == MAX_DISCS);
	CHECK(library_omitted() == 2);

	// a folder over the per-folder cap: the entries past it are counted
	r = fresh();
	int total = MAX_ENTRIES + 10;
	for (int i = 0; i < total; i++) {
		char name[64];
		snprintf(name, sizeof(name), "g%05d.chd", i);
		put(r, name, "");
	}
	scan(r);
	CHECK(library_count() == MAX_GAMES);
	// 10 not read, and the games the cap on games turned away
	CHECK(library_omitted() == 10 + (MAX_ENTRIES - MAX_GAMES));

	// more games than the library holds, spread over folders
	r = fresh();
	for (int i = 0; i < MAX_GAMES + 52; i++) {
		char name[96];
		snprintf(name, sizeof(name), "f%d/g%05d.chd", i % 3, i);
		put(r, name, "");
	}
	scan(r);
	CHECK(library_count() == MAX_GAMES);
	CHECK(library_omitted() == 52);

	// a folder that cannot be opened (the games folder itself is gone)
	CHECK(!library_load("sdmc:/lib/missing", true));
	CHECK(library_omitted() == 1);
	// ... then a good one resets the count
	r = fresh();
	put(r, "ok.chd", "x");
	scan(r);
	CHECK(library_omitted() == 0);
}

static void test_unknown_type(void)
{
	// readdir() may not know a type (DT_UNKNOWN): stat() decides
	const char *r = fresh();
	put(r, "sub/x.chd", "x");
	put(r, "file.chd", "x");
	struct dirent de;
	memset(&de, 0, sizeof(de));
	de.d_type = DT_UNKNOWN;
	snprintf(de.d_name, sizeof(de.d_name), "sub");
	CHECK(is_dir_entry(r, &de));
	snprintf(de.d_name, sizeof(de.d_name), "file.chd");
	CHECK(!is_dir_entry(r, &de));
	snprintf(de.d_name, sizeof(de.d_name), "nothing");
	CHECK(!is_dir_entry(r, &de));
	de.d_type = DT_REG;
	snprintf(de.d_name, sizeof(de.d_name), "sub");   // a known type is trusted
	CHECK(!is_dir_entry(r, &de));
	de.d_type = DT_DIR;
	CHECK(is_dir_entry(r, &de));
}

static void test_teardown(void)
{
	// the scan-time group of an entry is freed with the rest
	library_free();
	add_disc(1, "sdmc:/lib/x/G (Disc 1).chd");
	add_disc(1, "sdmc:/lib/x/G (Disc 2).chd");
	CHECK(ngames == 1 && games[0].group && games[0].ndiscs == 2);
	library_free();
	CHECK(ngames == 0);

	// rescans over and over, from the cache, and with series (LeakSanitizer
	// finds anything left behind when the process ends)
	const char *r = fresh();
	put(r, "Tekken.chd", "x");
	put(r, "Tekken 2.chd", "x");
	put(r, "Tekken 3.chd", "x");
	put(r, "FF.m3u", "FF (Disc 1).chd\nFF (Disc 2).chd\n");
	put(r, "FF (Disc 1).chd", "x");
	put(r, "FF (Disc 2).chd", "x");
	put(r, "Two (Disc 1).cue", "FILE \"Two (Disc 1).bin\" BINARY\n");
	put(r, "Two (Disc 1).bin", "x");
	put(r, "Two (Disc 2).cue", "FILE \"Two (Disc 2).bin\" BINARY\n");
	put(r, "Two (Disc 2).bin", "x");
	put(r, "Spyro (USA).chd", "x");
	put(r, "Spyro (Europe).chd", "x");
	for (int i = 0; i < 100; i++) {
		CHECK(library_load(r, true));
		CHECK(library_count() == 7);
		CHECK(library_load(r, false));   // from the cache
		CHECK(library_count() == 7);
	}
	CHECK(find("Two") && find("Two")->ndiscs == 2);
	CHECK(library_series_count() == 2);   // the Tekkens and the Spyros
	library_load(NULL, true);
	CHECK(library_count() == 0);
}

static char *slurp(const char *path)
{
	static char buf[4096];
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;
	return buf;
}

static bool exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

static void test_launch_path(void)
{
	const char *r = fresh();
	put(r, "Solo.chd", "x");
	put(r, "Duo (Disc 1).chd", "x");
	put(r, "Duo (Disc 2).chd", "x");
	scan(r);
	const game_t *solo = find("Solo"), *duo = find("Duo");
	CHECK(solo && duo && duo->ndiscs == 2);
	char buf[PATH_LEN];

	// one disc: the disc itself, nothing written
	CHECK(library_launch_path(solo, buf, sizeof(buf)) == buf);
	CHECK(ends_with(buf, "/Solo.chd"));

	// several: a playlist, in place when it returns
	const char *m3u = PSX3D_DIR "/m3u/Duo _Disc 1_.m3u";
	CHECK(library_launch_path(duo, buf, sizeof(buf)) == buf);
	CHECK(!strcmp(buf, m3u));
	char want[PATH_MAX];
	snprintf(want, sizeof(want), "/lib/%d/Duo (Disc 1).chd\n/lib/%d/Duo (Disc 2).chd\n", case_no, case_no);
	CHECK(slurp(m3u) && !strcmp(slurp(m3u), want));
	CHECK(!exists(PSX3D_DIR "/m3u/Duo _Disc 1_.m3u.tmp"));
	CHECK(!exists(PSX3D_DIR "/m3u/Duo _Disc 1_.m3u.bak"));

	// the disc swap order follows the game, a rewrite replaces the old list
	char *old = strdup(slurp(m3u));
	CHECK(library_launch_path(duo, buf, sizeof(buf)) == buf);
	CHECK(!strcmp(slurp(m3u), old));

	// the card is full: the write fails at the end (/dev/full), the player
	// gets NULL, the previous list stays as it was
	const char *tmp = PSX3D_DIR "/m3u/Duo _Disc 1_.m3u.tmp";
	if (symlink("/dev/full", tmp) == 0) {
		CHECK(library_launch_path(duo, buf, sizeof(buf)) == NULL);
		CHECK(slurp(m3u) && !strcmp(slurp(m3u), old));
		CHECK(!exists(PSX3D_DIR "/m3u/Duo _Disc 1_.m3u.bak"));
		struct stat st;
		CHECK(lstat(tmp, &st) != 0);   // nothing left behind
	} else
		printf("skip: no symlink to /dev/full\n");

	// the list cannot be created (read-only folder)
	chmod(PSX3D_DIR "/m3u", 0555);
	if (access(PSX3D_DIR "/m3u", W_OK) != 0) {
		CHECK(library_launch_path(duo, buf, sizeof(buf)) == NULL);
		CHECK(slurp(m3u) && !strcmp(slurp(m3u), old));
	} else
		printf("skip: running as root, a folder cannot be made read-only\n");
	chmod(PSX3D_DIR "/m3u", 0777);

	// and it works again once there is room
	CHECK(library_launch_path(duo, buf, sizeof(buf)) == buf);
	CHECK(!strcmp(slurp(m3u), old));
	char old2[4096];
	snprintf(old2, sizeof(old2), "%s", old);
	free(old);

	// no folder for the lists yet: it is made
	snprintf(want, sizeof(want), "%s.gone", PSX3D_DIR "/m3u");
	CHECK(rename(PSX3D_DIR "/m3u", want) == 0);
	CHECK(library_launch_path(duo, buf, sizeof(buf)) == buf);
	CHECK(slurp(m3u) && !strcmp(slurp(m3u), old2));
}

int main(void)
{
	char dir[] = "/tmp/psx3d-library-XXXXXX";
	if (!mkdtemp(dir) || chdir(dir)) {
		printf("no temp directory\n");
		return 2;
	}
	mkdirs("sdmc:/3ds/PSX3D");
	mkdirs("sdmc:/lib");

	test_reviewer_mixed();
	test_cue_and_bins();
	test_playlists();
	test_cue_syntax();
	test_names_and_paths();
	test_groups_and_titles();
	test_limits();
	test_unknown_type();
	test_teardown();
	test_launch_path();
	library_load(NULL, true);

	char cmd[PATH_MAX + 16];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	if (system(cmd))
		printf("could not remove %s\n", dir);
	printf("library: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
