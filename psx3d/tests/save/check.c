// Host test for safefile.c: a power cut after every step of a replace must
// leave a complete old or new copy, and a damaged file must never become the
// backup. Run with run.sh (ASan/UBSan).
#define _GNU_SOURCE
#include "safefile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SIZE (128 * 1024 + 7)   // not a multiple of the write chunk
static unsigned char A[SIZE], B[SIZE], C[SIZE], got[SIZE];
static int failures, checks;

#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #c, ctx); } } while (0)
static char ctx[128];

int safe_test_rename(const char *from, const char *to)
{
	if (access(to, F_OK) == 0) return -1;
	return rename(from, to);
}

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
static void clean(void)
{
	safe_remove("card");
	safe_remove("conf");
}
// which of the payloads safe_read returns
static int which(bool skip_current)
{
	safe_source_t s = safe_read("card", got, SIZE, skip_current, false, NULL);
	if (s == SAFE_NONE) return 0;
	if (!memcmp(got, A, SIZE)) return 'A';
	if (!memcmp(got, B, SIZE)) return 'B';
	if (!memcmp(got, C, SIZE)) return 'C';
	return '?';
}

static void binary_crashes(void)
{
	// 1. first save ever, interrupted: nothing or the new data
	for (int k = 0; k < 8; k++) {
		clean();
		snprintf(ctx, sizeof(ctx), "first write, crash after %d", k);
		safe_test_crash_after = k;
		bool done = safe_write("card", A, SIZE, true);
		safe_test_crash_after = -1;
		int w = which(false);
		CHECK(w == 0 || w == 'A');
		CHECK(!done || w == 'A');
	}
	// 2. replace A (backup C) by B: always A or B, never cut off; afterwards
	// a second complete copy of a good version must exist
	for (int k = 0; k < 8; k++) {
		clean();
		put_ext("", A, SIZE);
		put_ext(".bak", C, SIZE);
		snprintf(ctx, sizeof(ctx), "replace, crash after %d", k);
		safe_test_crash_after = k;
		bool done = safe_write("card", B, SIZE, true);
		safe_test_crash_after = -1;
		int w = which(false);
		CHECK(w == 'A' || w == 'B');
		if (done) {
			CHECK(w == 'B');
			CHECK(which(true) == 'A');   // previous good version is the backup
		}
		CHECK(which(true) != '?');
		// the next save after the interruption completes normally
		CHECK(safe_write("card", B, SIZE, true));
		CHECK(which(false) == 'B');
		CHECK(which(true) == 'A' || which(true) == 'B' || which(true) == 'C');
		CHECK(size_of("card.tmp") < 0);
	}
	// 3. power cut while the .tmp itself was written: half a .tmp
	clean();
	put_ext("", A, SIZE);
	put_ext(".tmp", B, SIZE / 2);
	snprintf(ctx, sizeof(ctx), "half tmp");
	CHECK(which(false) == 'A');
	CHECK(size_of("card.tmp") < 0);   // dropped once the card loaded
	// ... and no card at all next to it: nothing usable, half data refused
	clean();
	put_ext(".tmp", B, SIZE / 2);
	CHECK(which(false) == 0);
	// a complete .tmp alone (first save cut before its rename) is used
	clean();
	put_ext(".tmp", B, SIZE);
	CHECK(safe_read("card", got, SIZE, false, false, NULL) == SAFE_TEMP && !memcmp(got, B, SIZE));
	// prefer a complete .bak over a .tmp (cut between steps 3 and 4)
	clean();
	put_ext(".bak", A, SIZE);
	put_ext(".tmp", B, SIZE);
	CHECK(safe_read("card", got, SIZE, false, false, NULL) == SAFE_BACKUP && !memcmp(got, A, SIZE));
}

static void damaged(void)
{
	// cut-off card: loads the backup; repair puts it back; the next save
	// keeps that good backup instead of the damaged file
	clean();
	put_ext("", B, SIZE / 3);
	put_ext(".bak", A, SIZE);
	snprintf(ctx, sizeof(ctx), "short card");
	CHECK(safe_read("card", got, SIZE, false, true, NULL) == SAFE_BACKUP && !memcmp(got, A, SIZE));
	CHECK(safe_repair("card", got, SIZE));
	CHECK(which(false) == 'A' && which(true) == 'A');
	CHECK(safe_write("card", B, SIZE, true));
	CHECK(which(false) == 'B' && which(true) == 'A');

	// a damaged current file of the wrong size is never rotated into .bak
	clean();
	put_ext("", B, 100);
	put_ext(".bak", A, SIZE);
	snprintf(ctx, sizeof(ctx), "write over damaged");
	CHECK(safe_write("card", C, SIZE, true));
	CHECK(which(false) == 'C' && which(true) == 'A');
	for (int k = 0; k < 8; k++) {
		clean();
		put_ext("", B, 100);
		put_ext(".bak", A, SIZE);
		snprintf(ctx, sizeof(ctx), "write over damaged, crash after %d", k);
		safe_test_crash_after = k;
		safe_write("card", C, SIZE, true);
		safe_test_crash_after = -1;
		CHECK(which(true) == 'A' || which(true) == 'C');
		int w = which(false);
		CHECK(w == 'A' || w == 'C');
	}

	// right size but bad content (state that fails to load): the caller asks
	// for the backup, repairs, and the backup survives the next save
	clean();
	unsigned char bad[SIZE];
	memset(bad, 0, SIZE);
	put_ext("", bad, SIZE);
	put_ext(".bak", A, SIZE);
	snprintf(ctx, sizeof(ctx), "bad content");
	CHECK(safe_read("card", got, SIZE, false, true, NULL) == SAFE_CURRENT);
	CHECK(safe_read("card", got, SIZE, true, false, NULL) == SAFE_BACKUP && !memcmp(got, A, SIZE));
	CHECK(safe_repair("card", got, SIZE));
	CHECK(safe_write("card", B, SIZE, true));
	CHECK(which(false) == 'B' && which(true) == 'A');

	// a card from another tool (other size), no backup: still read (tolerant)
	clean();
	put_ext("", B, 1000);
	size_t n = 0;
	snprintf(ctx, sizeof(ctx), "tolerant");
	CHECK(safe_read("card", got, SIZE, false, true, &n) == SAFE_CURRENT && n == 1000);
	CHECK(safe_read("card", got, SIZE, false, false, NULL) == SAFE_NONE);

	// unchanged card: two identical saves leave backup == current
	clean();
	CHECK(safe_write("card", A, SIZE, true) && safe_write("card", A, SIZE, true));
	CHECK(which(false) == 'A' && which(true) == 'A');
	// delete removes every copy
	safe_remove("card");
	CHECK(!safe_exists("card") && size_of("card.bak") < 0 && size_of("card.tmp") < 0);
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
		snprintf(ctx, sizeof(ctx), "text, crash after %d", k);
		FILE *f = safe_begin("conf");
		fputs("new\n", f);
		safe_test_crash_after = k;
		bool done = safe_finish(f, "conf", true);
		safe_test_crash_after = -1;
		CHECK(text_is("old\n") || text_is("new\n"));
		CHECK(!done || text_is("new\n"));
		CHECK(size_of("conf.tmp") < 0);
		// next save finishes and leaves no backup behind
		f = safe_begin("conf");
		fputs("next\n", f);
		CHECK(safe_finish(f, "conf", true) && text_is("next\n") && size_of("conf.bak") < 0);
	}
	// write error: old file kept, temp removed
	clean();
	put("conf", "old\n", 4);
	FILE *f = safe_begin("conf");
	fputs("half", f);
	snprintf(ctx, sizeof(ctx), "text failed write");
	CHECK(!safe_finish(f, "conf", false) && text_is("old\n") && size_of("conf.tmp") < 0);
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
	binary_crashes();
	damaged();
	text_crashes();
	clean();
	rmdir(dir);
	printf("save safety: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
