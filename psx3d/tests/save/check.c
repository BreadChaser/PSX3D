// Host test for safefile.c (run.sh: ASan/UBSan). FAT renames never replace an
// existing file, as on the 3DS SD card. Faults are injected into the real
// code: a power cut after every step, short writes at every chunk, a full
// card, failing closes, failing renames and read errors. Whatever happens, a
// good copy of the old or the new data must stay loadable, a rejected read
// must hand out nothing, and repair must never destroy its source.
#define _GNU_SOURCE
#include "safefile.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SIZE (128 * 1024 + 7)   // not a multiple of the write chunk
static unsigned char A[SIZE], B[SIZE], C[SIZE];
static int failures, checks;
static char ctx[160];
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #c, ctx); } } while (0)

static bool head_ok(const void *head) { const unsigned char *h = head; return h[0] == 'M' && h[1] == 'C'; }
static const safe_format_t FMT = { .size = SIZE, .head = 2, .valid = head_ok };

// ---------------------------------------------------------------- faults
static struct {
	long write_budget;   // >= 0: bytes fwrite may still write (full card)
	int fwrite_zero;     // the n-th fwrite (1-based) writes nothing
	int fclose_fail;     // the n-th fclose (1-based) reports an error
	bool fclose_all;     // every fclose reports an error
	int rename_fail;     // the n-th rename (1-based) fails
	int fread_fail;      // the n-th fread (1-based) comes back short
	int writes, closes, renames, reads;
} fault;
static void faults_off(void) { memset(&fault, 0, sizeof(fault)); fault.write_budget = -1; }

int safe_test_rename(const char *from, const char *to)
{
	if (++fault.renames == fault.rename_fail) return -1;
	if (access(to, F_OK) == 0) return -1;   // FAT: never over a file
	return rename(from, to);
}
size_t safe_test_fwrite(const void *p, size_t size, size_t n, FILE *f)
{
	if (++fault.writes == fault.fwrite_zero) return 0;
	size_t bytes = size * n;
	if (fault.write_budget >= 0 && (long)bytes > fault.write_budget) {
		size_t part = fault.write_budget;   // what fits, then the card is full
		fault.write_budget = 0;
		fwrite(p, 1, part, f);
		errno = ENOSPC;
		return part / size;
	}
	if (fault.write_budget >= 0) fault.write_budget -= bytes;
	return fwrite(p, size, n, f);
}
size_t safe_test_fread(void *p, size_t size, size_t n, FILE *f)
{
	if (++fault.reads == fault.fread_fail) return n ? n - 1 : 0;
	return fread(p, size, n, f);
}
int safe_test_fclose(FILE *f)
{
	int r = fclose(f);
	return ++fault.closes == fault.fclose_fail || fault.fclose_all ? EOF : r;
}

// ---------------------------------------------------------------- helpers
static void put(const char *path, const void *d, size_t n)
{
	FILE *f = fopen(path, "wb");
	fwrite(d, 1, n, f);
	fclose(f);
}
static void put_ext(const char *ext, const void *d, size_t n)
{
	char p[64];
	snprintf(p, sizeof(p), "card%s", ext);
	put(p, d, n);
}
static long size_of(const char *path)
{
	struct stat st;
	return stat(path, &st) ? -1 : st.st_size;
}
static int content(const char *path)   // which payload a file holds exactly
{
	static unsigned char buf[SIZE + 1];
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	if (n != SIZE) return '?';
	return !memcmp(buf, A, SIZE) ? 'A' : !memcmp(buf, B, SIZE) ? 'B' : !memcmp(buf, C, SIZE) ? 'C' : '?';
}
static void clean(void)
{
	safe_remove("card");
	safe_remove("conf");
	faults_off();
	safe_test_crash_after = -1;
}
// which payload safe_load hands out, 0 for none
static int load(unsigned skip, safe_info_t *info)
{
	safe_info_t dummy;
	unsigned char *d = safe_load("card", &FMT, skip, info ? info : &dummy);
	if (!d) return 0;
	int w = !memcmp(d, A, SIZE) ? 'A' : !memcmp(d, B, SIZE) ? 'B' : !memcmp(d, C, SIZE) ? 'C' : '?';
	free(d);
	return w;
}
// any good copy among <name>, .bak, .tmp holding w
static bool somewhere(int w)
{
	return content("card") == w || content("card.bak") == w || content("card.tmp") == w;
}

// ---------------------------------------------------------------- tests
static void power_cuts(void)
{
	// first save ever, cut after every step: nothing or the new data
	for (int k = 0; k < 10; k++) {
		clean();
		snprintf(ctx, sizeof(ctx), "first write, cut after %d", k);
		safe_test_crash_after = k;
		bool done = safe_write("card", &FMT, A, true);
		safe_test_crash_after = -1;
		CHECK(!done || size_of("card.tmp") < 0);
		int w = load(0, NULL);
		CHECK(w == 0 || w == 'A');
		CHECK(!done || w == 'A');
		// a complete .tmp left behind is still found (Continue / slot list)
		CHECK(safe_stat("card", &FMT, NULL) == (w == 'A'));
	}
	// replace A (backup C) by B: always A or B, a second good copy remains
	for (int k = 0; k < 10; k++) {
		clean();
		put_ext("", A, SIZE);
		put_ext(".bak", C, SIZE);
		snprintf(ctx, sizeof(ctx), "replace, cut after %d", k);
		safe_test_crash_after = k;
		bool done = safe_write("card", &FMT, B, true);
		safe_test_crash_after = -1;
		int w = load(0, NULL);
		CHECK(w == 'A' || w == 'B');
		CHECK(!done || (w == 'B' && content("card.bak") == 'A'));
		CHECK(somewhere('A'));   // the replaced version is never lost
		CHECK(safe_write("card", &FMT, B, true));   // the next save completes
		CHECK(load(0, NULL) == 'B' && size_of("card.tmp") < 0);
	}
	// replace over a damaged file: it is kept as .bad, the backup survives
	for (int k = 0; k < 10; k++) {
		clean();
		put_ext("", B, 100);
		put_ext(".bak", A, SIZE);
		snprintf(ctx, sizeof(ctx), "write over damaged, cut after %d", k);
		safe_test_crash_after = k;
		bool done = safe_write("card", &FMT, C, true);
		safe_test_crash_after = -1;
		CHECK(content("card.bak") == 'A');
		int w = load(0, NULL);
		CHECK(w == 'A' || w == 'C');
		CHECK(!done || (w == 'C' && size_of("card.bad") == 100));
		CHECK(size_of("card") == 100 || size_of("card.bad") == 100);   // never deleted
	}
	// repair from the backup, cut after every step: the backup is untouched
	for (int k = 0; k < 6; k++) {
		clean();
		put_ext("", B, 50);
		put_ext(".bak", A, SIZE);
		snprintf(ctx, sizeof(ctx), "repair from backup, cut after %d", k);
		safe_test_crash_after = k;
		safe_repair("card", &FMT, A, SAFE_BACKUP);
		safe_test_crash_after = -1;
		CHECK(content("card.bak") == 'A' && load(0, NULL) == 'A');
	}
	// repair from a sole .tmp, cut after every step: the .tmp or <name> holds it
	for (int k = 0; k < 6; k++) {
		clean();
		put_ext("", B, 50);
		put_ext(".tmp", A, SIZE);
		snprintf(ctx, sizeof(ctx), "repair from tmp, cut after %d", k);
		safe_test_crash_after = k;
		safe_repair("card", &FMT, A, SAFE_TEMP);
		safe_test_crash_after = -1;
		CHECK(load(0, NULL) == 'A');
	}
}

static void write_faults(void)
{
	// a short write at every chunk, a full card at many sizes, a failing
	// close: false, the old version and its backup exactly as they were
	for (int mode = 0; mode < 3; mode++)
		for (int n = 1; n <= 4; n++) {
			clean();
			put_ext("", A, SIZE);
			put_ext(".bak", C, SIZE);
			snprintf(ctx, sizeof(ctx), "%s %d", mode == 0 ? "short write" : mode == 1 ? "full card" : "close fails", n);
			if (mode == 0) fault.fwrite_zero = n;
			if (mode == 1) fault.write_budget = (long)(n - 1) * 40000;
			if (mode == 2) fault.fclose_all = n <= 2;   // reads ignore it, the write must not
			bool done = safe_write("card", &FMT, B, true);
			bool fails = mode == 0 ? n <= 3 : mode == 1 ? true : n <= 2;
			if (fails) {
				CHECK(!done);
				CHECK(content("card") == 'A' && content("card.bak") == 'C');
				CHECK(size_of("card.tmp") < 0);
			} else
				CHECK(done && content("card") == 'B');
			faults_off();
			CHECK(safe_write("card", &FMT, B, true) && load(0, NULL) == 'B');
		}
	// a rename fails at every one of the replace's renames: false or done,
	// never lost, and the next save puts things right
	for (int n = 1; n <= 4; n++) {
		clean();
		put_ext("", A, SIZE);
		put_ext(".bak", C, SIZE);
		snprintf(ctx, sizeof(ctx), "rename %d fails", n);
		fault.rename_fail = n;
		bool done = safe_write("card", &FMT, B, true);
		faults_off();
		int w = load(0, NULL);
		CHECK(done ? w == 'B' : (w == 'A' || w == 'B'));
		CHECK(somewhere('A'));
		CHECK(safe_write("card", &FMT, B, true) && load(0, NULL) == 'B');
	}
	// text files: write error or failing close keeps the old file
	for (int mode = 0; mode < 2; mode++) {
		clean();
		put("conf", "old\n", 4);
		snprintf(ctx, sizeof(ctx), "text %s", mode ? "close fails" : "ok=false");
		FILE *f = safe_begin("conf");
		fputs("half", f);
		if (mode) fault.fclose_fail = 1;
		CHECK(!safe_finish(f, "conf", mode == 1) && size_of("conf.tmp") < 0);
		faults_off();
		safe_recover("conf");
		CHECK(size_of("conf") == 4);
	}
}

static void reads(void)
{
	safe_info_t info;
	// the reviewer's probe: a three-byte file, then the same as the backup;
	// nothing comes back, nothing on the card changes
	clean();
	put_ext("", "BAD", 3);
	snprintf(ctx, sizeof(ctx), "three bytes");
	CHECK(load(0, &info) == 0 && info.rejected && !info.failed && size_of("card") == 3);
	// exact size only: other sizes are rejected, never read in part
	long sizes[] = { 0, 1, SIZE - 1, SIZE + 1, 2 * SIZE };
	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		clean();
		unsigned char *big = calloc(1, 2 * SIZE);
		memcpy(big, A, SIZE);
		put_ext("", big, sizes[i]);
		free(big);
		snprintf(ctx, sizeof(ctx), "size %ld", sizes[i]);
		CHECK(load(0, &info) == 0 && info.rejected);
		put_ext(".bak", C, SIZE);   // ... with a good backup: that one
		CHECK(load(0, &info) == 'C' && info.from == SAFE_BACKUP && info.rejected);
	}
	// right size, wrong header (zero-filled): rejected
	clean();
	unsigned char *zero = calloc(1, SIZE);
	put_ext("", zero, SIZE);
	free(zero);
	put_ext(".tmp", B, SIZE);
	snprintf(ctx, sizeof(ctx), "zero filled");
	CHECK(load(0, &info) == 'B' && info.from == SAFE_TEMP);
	// a read error after a good stat: rejected, the next copy is used
	clean();
	put_ext("", A, SIZE);
	put_ext(".bak", C, SIZE);
	snprintf(ctx, sizeof(ctx), "read error");
	fault.fread_fail = 1;
	CHECK(load(0, &info) == 'C' && info.rejected);
	// order: <name>, .bak, .tmp; skip bits leave sources out
	clean();
	put_ext("", A, SIZE);
	put_ext(".bak", B, SIZE);
	put_ext(".tmp", C, SIZE);
	snprintf(ctx, sizeof(ctx), "order");
	CHECK(load(0, NULL) == 'A');
	CHECK(load(1u << SAFE_CURRENT, NULL) == 'B');
	CHECK(load(3u << SAFE_CURRENT, NULL) == 'C');
	CHECK(load(7u << SAFE_CURRENT, NULL) == 0);
	// reading never changes the card: the .tmp next to a good file stays
	CHECK(content("card.tmp") == 'C');
	// half .tmp alone: nothing, and the slot is not "used"
	clean();
	put_ext(".tmp", B, SIZE / 2);
	snprintf(ctx, sizeof(ctx), "half tmp");
	CHECK(load(0, NULL) == 0 && !safe_stat("card", &FMT, NULL));
	// a complete sole .tmp is a save (the reviewer: invisible before)
	clean();
	put_ext(".tmp", B, SIZE);
	snprintf(ctx, sizeof(ctx), "sole tmp");
	CHECK(safe_stat("card", &FMT, NULL) && load(0, &info) == 'B' && info.from == SAFE_TEMP);
}

static void repairs(void)
{
	safe_info_t info;
	// the reviewer's probe: sole .tmp, repair while writes fail: the .tmp
	// is renamed, never rewritten, so a write fault can't touch it
	clean();
	put_ext(".tmp", A, SIZE);
	snprintf(ctx, sizeof(ctx), "repair sole tmp, writes fail");
	fault.fwrite_zero = 1;
	fault.write_budget = 0;
	CHECK(safe_repair("card", &FMT, A, SAFE_TEMP));
	faults_off();
	CHECK(content("card") == 'A' && load(0, NULL) == 'A');
	// ... and with the rename failing: false, the .tmp still there
	clean();
	put_ext("", B, 7);
	put_ext(".tmp", A, SIZE);
	snprintf(ctx, sizeof(ctx), "repair sole tmp, rename fails");
	fault.rename_fail = 2;   // the damaged file moves, the .tmp does not
	CHECK(!safe_repair("card", &FMT, A, SAFE_TEMP));
	faults_off();
	CHECK(content("card.tmp") == 'A' && load(0, NULL) == 'A' && size_of("card.bad") == 7);
	// repair from the backup with a full card: false, backup and .tmp intact
	clean();
	put_ext("", B, 7);
	put_ext(".bak", A, SIZE);
	put_ext(".tmp", C, SIZE);
	snprintf(ctx, sizeof(ctx), "repair from bak, card full");
	fault.write_budget = 1000;
	CHECK(!safe_repair("card", &FMT, A, SAFE_BACKUP));
	faults_off();
	CHECK(content("card.bak") == 'A' && content("card.tmp") == 'C' && size_of("card") == 7);
	CHECK(size_of("card.rep") < 0);
	// repair from the backup: <name> good again, backup unchanged, the
	// damaged file kept; the next save keeps the good backup generation
	clean();
	put_ext("", B, SIZE / 3);
	put_ext(".bak", A, SIZE);
	snprintf(ctx, sizeof(ctx), "repair from bak");
	CHECK(load(0, &info) == 'A' && info.from == SAFE_BACKUP);
	CHECK(safe_repair("card", &FMT, A, SAFE_BACKUP));
	CHECK(content("card") == 'A' && content("card.bak") == 'A' && size_of("card.bad") == SIZE / 3);
	CHECK(safe_write("card", &FMT, B, true) && content("card") == 'B' && content("card.bak") == 'A');
	// a save while the only good copy is a .tmp (failed repair earlier):
	// that .tmp is put in place first, then the write; a failed write
	// leaves it as the current file
	clean();
	put_ext(".tmp", A, SIZE);
	snprintf(ctx, sizeof(ctx), "write with sole tmp, write fails");
	fault.fwrite_zero = 1;
	CHECK(!safe_write("card", &FMT, B, true));
	faults_off();
	CHECK(load(0, NULL) == 'A');
	CHECK(safe_write("card", &FMT, B, true) && content("card") == 'B' && content("card.bak") == 'A');
	// right size, bad header current: never rotated into the backup
	clean();
	unsigned char *zero = calloc(1, SIZE);
	put_ext("", zero, SIZE);
	free(zero);
	put_ext(".bak", A, SIZE);
	snprintf(ctx, sizeof(ctx), "zero current, save");
	CHECK(safe_write("card", &FMT, B, true) && content("card") == 'B' && content("card.bak") == 'A');
	CHECK(size_of("card.bad") == SIZE);
	// unchanged card twice: backup == current; remove takes every copy
	clean();
	snprintf(ctx, sizeof(ctx), "remove");
	CHECK(safe_write("card", &FMT, A, true) && safe_write("card", &FMT, A, true));
	put_ext(".bad", B, 9);
	put_ext(".rep", B, 9);
	safe_remove("card");
	CHECK(!safe_stat("card", &FMT, NULL) && size_of("card.bak") < 0 && size_of("card.bad") < 0 && size_of("card.rep") < 0);
}

static int text_is(const char *want)
{
	char buf[64] = "";
	safe_recover("conf");
	FILE *f = fopen("conf", "r");
	if (!f) return want == NULL;
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = 0;
	fclose(f);
	return want && !strcmp(buf, want);
}

static void text_crashes(void)
{
	for (int k = 0; k < 8; k++) {
		clean();
		put("conf", "old\n", 4);
		snprintf(ctx, sizeof(ctx), "text, cut after %d", k);
		FILE *f = safe_begin("conf");
		fputs("new\n", f);
		safe_test_crash_after = k;
		bool done = safe_finish(f, "conf", true);
		safe_test_crash_after = -1;
		CHECK(text_is("old\n") || text_is("new\n"));
		CHECK(!done || text_is("new\n"));
		CHECK(size_of("conf.tmp") < 0);
		f = safe_begin("conf");
		fputs("next\n", f);
		CHECK(safe_finish(f, "conf", true) && text_is("next\n") && size_of("conf.bak") < 0);
	}
}

int main(void)
{
	char dir[] = "/tmp/psx3d-save-XXXXXX";
	if (!mkdtemp(dir) || chdir(dir)) return 2;
	for (int i = 0; i < SIZE; i++) {
		A[i] = (unsigned char)(i * 7 + 1);
		B[i] = (unsigned char)(i * 13 + 5);
		C[i] = (unsigned char)(i * 3 + 9);
	}
	A[0] = B[0] = C[0] = 'M';
	A[1] = B[1] = C[1] = 'C';
	faults_off();
	power_cuts();
	write_faults();
	reads();
	repairs();
	text_crashes();
	clean();
	rmdir(dir);
	printf("save safety: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
