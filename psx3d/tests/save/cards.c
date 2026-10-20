// Host test for cards.c, the real memory card code (run.sh: ASan/UBSan) on
// top of the real safefile.c with its fault hooks. A card is loaded only from
// a good copy; a damaged one is kept; a failed write or repair keeps the card
// due (never "shadowed" as if it were on the SD card), is tried again after
// CARD_RETRY_MS and is reported; core-managed cards (card 2) get the same.
#define _GNU_SOURCE
#include "psx3d.h"
#include "clip.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CARD (128 * 1024)
static unsigned char ram[CARD];             // the core's SAVE_RAM (card 1)
char Mcd1Data[CARD], Mcd2Data[CARD];        // the core's cards
bool clip_replay_session;
static u64 now_ms = 1000;
static char last_osd[160];
static int osd_count, real_loads;
static char real_path[600];
static int failures, checks;
static char ctx[120];
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #c, ctx); } } while (0)

void *retro_get_memory_data(unsigned id) { (void)id; return ram; }
size_t retro_get_memory_size(unsigned id) { (void)id; return sizeof(ram); }
u64 osGetTime(void) { return now_ms; }
void log_printf(const char *fmt, ...) { (void)fmt; }
void emu_osd(const char *text) { snprintf(last_osd, sizeof(last_osd), "%s", text); osd_count++; }
void __real_LoadMcd(int mcd, char *str) { (void)mcd; real_loads++; snprintf(real_path, sizeof(real_path), "%s", str ? str : "(null)"); }
void __wrap_LoadMcd(int mcd, char *str);

// safefile.c's fault hooks (SAFEFILE_TEST)
static int write_fail, rename_fail_all;
int safe_test_rename(const char *from, const char *to)
{
	if (rename_fail_all || access(to, F_OK) == 0) return -1;
	return rename(from, to);
}
size_t safe_test_fwrite(const void *p, size_t size, size_t n, FILE *f) { return write_fail ? 0 : fwrite(p, size, n, f); }
size_t safe_test_fread(void *p, size_t size, size_t n, FILE *f) { return fread(p, size, n, f); }
int safe_test_fclose(FILE *f) { return fclose(f); }

static void blank(void *card)   // what the core starts with
{
	memset(card, 0, CARD);
	memcpy(card, "MC", 2);
}
static void pattern(void *card, int seed)
{
	unsigned char *c = card;
	for (int i = 0; i < CARD; i++) c[i] = (unsigned char)(i * seed + seed);
	c[0] = 'M'; c[1] = 'C';
}
static void put(const char *path, const void *d, size_t n)
{
	FILE *f = fopen(path, "wb");
	fwrite(d, 1, n, f);
	fclose(f);
}
static long size_of(const char *path)
{
	struct stat st;
	return stat(path, &st) ? -1 : st.st_size;
}
static bool file_is(const char *path, const void *want)
{
	static unsigned char buf[CARD + 1];
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	return n == CARD && !memcmp(buf, want, CARD);
}
static void reset(void)
{
	cards_end();
	safe_remove("game.srm");
	safe_remove("card2.mcd");
	remove("card2.mcd.bad");
	write_fail = rename_fail_all = 0;
	clip_replay_session = false;
	osd_count = real_loads = 0;
	last_osd[0] = 0;
	blank(ram);
	blank(Mcd1Data);
	blank(Mcd2Data);
}

static void card1(void)
{
	unsigned char a[CARD], b[CARD];
	pattern(a, 3);
	pattern(b, 5);

	// no file: blank card, nothing written until the game changes it
	reset();
	snprintf(ctx, sizeof(ctx), "new card");
	cards_begin("game.srm");
	CHECK(!cards_check() && size_of("game.srm") < 0);
	memcpy(ram, a, CARD);
	CHECK(cards_check() && file_is("game.srm", a));
	memcpy(ram, b, CARD);
	CHECK(cards_check() && file_is("game.srm", b) && file_is("game.srm.bak", a));
	CHECK(!cards_check());   // unchanged: no write

	// wrong size, no backup: not loaded (empty card), the file kept, and it
	// becomes .bad (byte for byte) when the game first writes the card
	reset();
	put("game.srm", "garbage", 7);
	snprintf(ctx, sizeof(ctx), "wrong size");
	cards_begin("game.srm");
	unsigned char empty[CARD];
	blank(empty);
	CHECK(!memcmp(ram, empty, CARD) && size_of("game.srm") == 7 && strstr(last_osd, "damaged"));
	memcpy(ram, a, CARD);
	CHECK(cards_check() && file_is("game.srm", a) && size_of("game.srm.bad") == 7);

	// cut-off card, good backup: the backup is loaded and put back
	reset();
	put("game.srm", a, CARD / 2);
	put("game.srm.bak", b, CARD);
	snprintf(ctx, sizeof(ctx), "backup");
	cards_begin("game.srm");
	CHECK(!memcmp(ram, b, CARD) && file_is("game.srm", b) && strstr(last_osd, "restored"));
	CHECK(!cards_check());

	// the repair fails: loaded, told so, and NOT taken as saved: the next
	// check writes it again without any change
	reset();
	put("game.srm", a, 10);
	put("game.srm.bak", b, CARD);
	snprintf(ctx, sizeof(ctx), "failed repair");
	write_fail = rename_fail_all = 1;
	cards_begin("game.srm");
	CHECK(!memcmp(ram, b, CARD) && strstr(last_osd, "could not fix"));
	write_fail = rename_fail_all = 0;
	CHECK(cards_check() && file_is("game.srm", b) && file_is("game.srm.bak", b));
	CHECK(size_of("game.srm.bad") == 10);

	// a failing write: reported, retried after the delay, at exit at once
	reset();
	snprintf(ctx, sizeof(ctx), "write fails");
	cards_begin("game.srm");
	memcpy(ram, a, CARD);
	write_fail = 1;
	CHECK(!cards_check() && strstr(last_osd, "Could not save") && size_of("game.srm") < 0);
	write_fail = 0;
	int shown = osd_count;
	CHECK(!cards_check() && osd_count == shown);   // waits CARD_RETRY_MS
	now_ms += 31 * 1000;
	CHECK(cards_check() && file_is("game.srm", a));
	memcpy(ram, b, CARD);
	write_fail = 1;
	CHECK(!cards_check());
	write_fail = 0;
	cards_save_now();               // leaving the game: tried at once
	CHECK(file_is("game.srm", b));

	// input-replay sessions never write the player's card
	reset();
	snprintf(ctx, sizeof(ctx), "replay");
	clip_replay_session = true;
	cards_begin("game.srm");
	memcpy(ram, a, CARD);
	CHECK(!cards_check());
	cards_save_now();
	CHECK(size_of("game.srm") < 0);
}

static void card2(void)
{
	unsigned char a[CARD], b[CARD], empty[CARD];
	pattern(a, 7);
	pattern(b, 9);
	blank(empty);
	char path[600];

	// no file yet: the core's empty card stays, its file name is taken away
	// (memory only); PSX3D writes the card when the game changes it
	reset();
	snprintf(ctx, sizeof(ctx), "card 2 new");
	cards_begin("game.srm");
	snprintf(path, sizeof(path), "card2.mcd");
	__wrap_LoadMcd(2, path);
	CHECK(path[0] == 0 && real_loads == 1 && real_path[0] == 0);
	CHECK(!memcmp(Mcd2Data, empty, CARD) && size_of("card2.mcd") < 0);
	CHECK(!cards_check());
	memcpy(Mcd2Data, a, CARD);
	CHECK(cards_check() && file_is("card2.mcd", a));
	memcpy(Mcd2Data, b, CARD);
	cards_save_now();
	CHECK(file_is("card2.mcd", b) && file_is("card2.mcd.bak", a));

	// existing card loads; damaged one with a backup: the backup
	reset();
	put("card2.mcd", a, CARD);
	snprintf(ctx, sizeof(ctx), "card 2 load");
	snprintf(path, sizeof(path), "card2.mcd");
	__wrap_LoadMcd(2, path);
	CHECK(!memcmp(Mcd2Data, a, CARD));
	reset();
	put("card2.mcd", a, 100);
	put("card2.mcd.bak", b, CARD);
	snprintf(path, sizeof(path), "card2.mcd");
	__wrap_LoadMcd(2, path);
	CHECK(!memcmp(Mcd2Data, b, CARD) && file_is("card2.mcd", b) && size_of("card2.mcd.bad") == 100);

	// a damaged card without a backup is not read (empty card), and kept
	reset();
	put("card2.mcd", a, 5000);
	snprintf(ctx, sizeof(ctx), "card 2 damaged");
	snprintf(path, sizeof(path), "card2.mcd");
	__wrap_LoadMcd(2, path);
	CHECK(!memcmp(Mcd2Data, empty, CARD) && size_of("card2.mcd") == 5000);
	memcpy(Mcd2Data, a, CARD);
	CHECK(cards_check() && file_is("card2.mcd", a) && size_of("card2.mcd.bad") == 5000);

	// DexDrive/VGS files (with a header), "none" and "": the core as before
	reset();
	unsigned char *gme = calloc(1, CARD + 3904);
	put("card2.mcd", gme, CARD + 3904);
	free(gme);
	snprintf(ctx, sizeof(ctx), "card 2 header file");
	snprintf(path, sizeof(path), "card2.mcd");
	__wrap_LoadMcd(2, path);
	CHECK(!strcmp(path, "card2.mcd") && !strcmp(real_path, "card2.mcd"));
	memcpy(Mcd2Data, a, CARD);
	CHECK(!cards_check() && size_of("card2.mcd") == CARD + 3904);
	snprintf(path, sizeof(path), "none");
	__wrap_LoadMcd(2, path);
	CHECK(!strcmp(real_path, "none"));
}

int main(void)
{
	char dir[] = "/tmp/psx3d-cards-XXXXXX";
	if (!mkdtemp(dir) || chdir(dir)) return 2;
	card1();
	card2();
	reset();
	remove("game.srm.bad");
	rmdir(dir);
	printf("memory cards: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
