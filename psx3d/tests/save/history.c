// Host test for history.c, the save backups (run.sh: ASan/UBSan, small
// HIST_BUDGET): entries are only ever renamed in or written crash-safe, the
// ring keeps a game's newest HIST_RING, card copies their own counts (one
// session, one per day for HIST_CARD_DAYS days, pins), the budget drops
// long-unplayed games' older entries first and never the newest, thumbnails
// travel with their state, and nothing but entries is listed.
#define _GNU_SOURCE
#include "history.h"
#include "safefile.h"
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

static int failures, checks;
static char ctx[120];
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #c, ctx); } } while (0)
void log_printf(const char *fmt, ...) { (void)fmt; }
size_t safe_test_fwrite(const void *p, size_t size, size_t n, FILE *f) { return fwrite(p, size, n, f); }
size_t safe_test_fread(void *p, size_t size, size_t n, FILE *f) { return fread(p, size, n, f); }
int safe_test_fclose(FILE *f) { return fclose(f); }
int safe_test_rename(const char *from, const char *to) { return access(to, F_OK) == 0 ? -1 : rename(from, to); }

static void put(const char *path, int seed, size_t n)
{
	FILE *f = fopen(path, "wb");
	for (size_t i = 0; i < n; i++) fputc((int)(i * seed + seed) & 0xff, f);
	fclose(f);
}
static int seed_of(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	int c = fgetc(f);
	fclose(f);
	return c;
}
static bool exists(const char *p) { return access(p, F_OK) == 0; }
static void age(const char *path, long seconds)
{
	struct stat st;
	if (stat(path, &st)) return;
	struct utimbuf t = { st.st_atime - seconds, st.st_mtime - seconds };
	utime(path, &t);
}
static int count(const char *group, bool cards, int kind)
{
	hist_entry_t e[64];
	int n = history_list(group, cards, e, 64), k = 0;
	for (int i = 0; i < n; i++) k += kind < 0 || (int)e[i].kind == kind;
	return k;
}
static long ring_total(void)
{
	long t = 0;
	DIR *d = opendir("h");
	struct dirent *de;
	while (d && (de = readdir(d))) {
		if (de->d_name[0] == '.') continue;
		hist_entry_t e[64];
		int n = history_list(de->d_name, false, e, 64);
		for (int i = 0; i < n; i++) t += e[i].size;
	}
	if (d) closedir(d);
	return t;
}

int main(void)
{
	char dir[] = "/tmp/psx3d-history-XXXXXX";
	if (!mkdtemp(dir) || chdir(dir)) return 2;
	history_set_root("h");
	hist_entry_t e[64], got;

	snprintf(ctx, sizeof(ctx), "adopt");
	put("Game.st3", 3, 1000);
	put("Game.st3.thm", 4, 100);
	CHECK(history_adopt("Game", HIST_SAVE, ".st3", "Game.st3", "Game.st3.thm", &got));
	CHECK(!exists("Game.st3") && !exists("Game.st3.thm") && got.size == 1000 && !strcmp(got.path, "h/Game/00000001-save.st3"));
	CHECK(history_list("Game", false, e, 64) == 1 && e[0].seq == 1 && e[0].kind == HIST_SAVE && !strcmp(e[0].ext, ".st3"));
	CHECK(e[0].thumb && e[0].size == 1000 && seed_of(e[0].path) == 3);
	CHECK(history_find("Game", got.mtime, got.size, &e[1]) && !strcmp(e[1].path, got.path));
	CHECK(!history_find("Game", got.mtime, got.size + 1, &e[1]));
	CHECK(!history_adopt("Game", HIST_SAVE, ".st3", "missing", NULL, NULL));
	CHECK(!history_adopt("../x", HIST_SAVE, ".st3", "Game.st3", NULL, NULL) && !history_add("", HIST_TIMER, ".sta", "a", 1));

	// taking an entry back (Undo delete / save): a rename, thumbnail too
	snprintf(ctx, sizeof(ctx), "take");
	CHECK(history_take(&got, "Game.st3") && seed_of("Game.st3") == 3 && seed_of("Game.st3.thm") == 4);
	CHECK(count("Game", false, -1) == 0);
	put("Game.st3", 9, 10);
	CHECK(history_adopt("Game", HIST_DELETE, ".st3", "Game.st3", "Game.st3.thm", &got));
	put("Game.st3", 8, 10);
	CHECK(!history_take(&got, "Game.st3") && exists(got.path));   // never over a file (FAT)

	// the ring: the newest HIST_RING of any kind, the oldest dropped with
	// its thumbnail; card copies counted apart
	snprintf(ctx, sizeof(ctx), "ring");
	for (int i = 0; i < HIST_RING + 5; i++) {
		put("x", 10 + i, 500);
		put("x.thm", 1, 10);
		CHECK(history_adopt("Game", (hist_kind_t)(i % (HIST_RESTORE + 1)), ".sta", "x", "x.thm", NULL));
	}
	CHECK(history_add("Game", HIST_CARD_PIN, ".srm", "card", 4));
	int n = history_list("Game", false, e, 64);
	CHECK(n == HIST_RING && seed_of(e[0].path) == 10 + HIST_RING + 4 && count("Game", true, -1) == 1);
	for (int i = 1; i < n; i++) CHECK(e[i - 1].seq > e[i].seq);
	{
		DIR *d = opendir("h/Game");
		struct dirent *de;
		int thm = 0;
		while ((de = readdir(d))) thm += strstr(de->d_name, ".thm") != NULL;
		closedir(d);
		CHECK(thm == HIST_RING);
	}

	// strangers in the folder are never listed or counted
	snprintf(ctx, sizeof(ctx), "parse");
	const char *odd[] = { "h/Game/00000099-save.st1.tmp", "h/Game/99-save.st1", "h/Game/00000099-sav.st1",
		"h/Game/00000099-pin", "h/Game/readme.txt", "h/Game/00000099xsave.st1", "h/Game/00000099-save." };
	for (size_t i = 0; i < sizeof(odd) / sizeof(odd[0]); i++) put(odd[i], 1, 1);
	CHECK(history_list("Game", false, e, 64) == HIST_RING && count("Game", true, -1) == 1);

	// card copies: one session copy, one day copy per day (HIST_CARD_DAYS)
	snprintf(ctx, sizeof(ctx), "cards");
	for (int i = 0; i < 3; i++) CHECK(history_add("Card", HIST_CARD_SESSION, ".mcd", "abcd", 4));
	CHECK(count("Card", true, HIST_CARD_SESSION) == 1);
	CHECK(history_add("Card", HIST_CARD_DAY, ".mcd", "d0", 2) && history_add("Card", HIST_CARD_DAY, ".mcd", "d1", 2));
	CHECK(count("Card", true, HIST_CARD_DAY) == 1);   // the same day: the newer one is not kept
	for (int i = 0; i < HIST_CARD_DAYS + 3; i++) {
		history_list("Card", true, e, 64);
		for (int k = 0; k < count("Card", true, -1); k++) if (e[k].kind == HIST_CARD_DAY) age(e[k].path, 86400);
		CHECK(history_add("Card", HIST_CARD_DAY, ".mcd", "dd", 2));
	}
	CHECK(count("Card", true, HIST_CARD_DAY) == HIST_CARD_DAYS);
	CHECK(history_add("Card", HIST_CARD_DAY, ".srm", "dd", 2) && count("Card", true, HIST_CARD_DAY) == HIST_CARD_DAYS + 1);
	for (int i = 0; i < HIST_CARD_PINS + 4; i++) CHECK(history_add("Card", HIST_CARD_PIN, ".mcd", "p", 1));
	CHECK(count("Card", true, HIST_CARD_PIN) == HIST_CARD_PINS);
	CHECK(count("Card", false, -1) == 0);

	// budget (test: HIST_BUDGET bytes) over all games' ring entries: an
	// unplayed game keeps its newest HIST_IDLE_KEEP first, the newest entry
	// is never dropped, cards never count
	snprintf(ctx, sizeof(ctx), "budget");
	CHECK(ring_total() <= HIST_BUDGET);
	for (int i = 0; i < 6; i++) {
		put("old", 60 + i, 400);
		CHECK(history_adopt("Idle", HIST_EXIT, ".sta", "old", NULL, NULL));
	}
	history_list("Idle", false, e, 64);
	for (int i = 0; i < 6; i++) age(e[i].path, 86400L * (HIST_IDLE_DAYS + 5) + i);
	long before_game = count("Game", false, -1);
	put("big", 90, 5000);
	CHECK(history_adopt("Third", HIST_LOAD, ".sta", "big", NULL, NULL));
	CHECK(ring_total() <= HIST_BUDGET);
	CHECK(count("Third", false, -1) == 1 && seed_of(e[0].path) >= 0);
	CHECK(count("Idle", false, -1) == 5 && count("Game", false, -1) == before_game);   // only as much as needed, idle game first
	for (int i = 0; i < 8; i++) {
		put("big", 100 + i, 5000);
		CHECK(history_adopt("Third", HIST_LOAD, ".sta", "big", NULL, NULL));
		CHECK(ring_total() <= HIST_BUDGET || count("Third", false, -1) == 1);
		if (count("Game", false, -1) < before_game) CHECK(count("Idle", false, -1) == 0);   // then the oldest
	}
	history_list("Third", false, e, 64);
	CHECK(seed_of(e[0].path) == 107);   // the newest survives
	CHECK(count("Card", true, HIST_CARD_PIN) == HIST_CARD_PINS);   // cards untouched

	// history_add writes crash-safe: no .tmp left, exact size
	snprintf(ctx, sizeof(ctx), "add");
	char data[300];
	memset(data, 7, sizeof(data));
	CHECK(history_add("Other", HIST_RESTORE, ".sta", data, sizeof(data)));
	CHECK(history_list("Other", false, e, 64) == 1 && e[0].size == 300 && !exists("h/Other/00000001-restore.sta.tmp"));

	if (system("rm -rf h x x.thm big old Game.st3")) {}
	if (chdir("/")) {}
	rmdir(dir);
	printf("backups: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
