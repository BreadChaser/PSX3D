// Crash-safe file replacement for player data (see safefile.h).
//
// Replace steps; a power cut between any two leaves a good copy:
//   1. write <name>.tmp completely and close it (the 3DS FS flushes on close)
//   2. remove <name>.bak                    (<name> still the old good file)
//   3. rename <name> -> <name>.bak          (old good file now the backup)
//      or, if <name> is damaged: rename it -> <name>.bad (the backup stays)
//   4. rename <name>.tmp -> <name>          (new file in place)
//   5. text files only: remove <name>.bak
// FAT on the 3DS SD card cannot rename over an existing file, hence 2 and 3.
#include "safefile.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHUNK (64 * 1024)   // bounded FS calls, so a writer thread yields often
#define HEAD_MAX 256        // largest safe_format_t.head

#ifdef SAFEFILE_TEST
int safe_test_crash_after = -1;   // -2: the power went off, nothing more runs
static bool crash(void)
{
	if (safe_test_crash_after >= 0 && safe_test_crash_after-- == 0)
		safe_test_crash_after = -2;
	return safe_test_crash_after == -2;
}
#define STEP() do { if (crash()) return false; } while (0)
// host tests: FAT renames (never over an existing file) and injected faults
int safe_test_rename(const char *from, const char *to);
size_t safe_test_fwrite(const void *p, size_t size, size_t n, FILE *f);
size_t safe_test_fread(void *p, size_t size, size_t n, FILE *f);
int safe_test_fclose(FILE *f);
#define rename safe_test_rename
#define fwrite safe_test_fwrite
#define fread safe_test_fread
#define fclose safe_test_fclose
#else
#define STEP() do { } while (0)
#endif

static void name_with(char *out, size_t len, const char *path, const char *ext)
{
	snprintf(out, len, "%s%s", path, ext);
}

static long file_size(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

// 1: path is a good copy (read into data if given), 0: no such file,
// -1: there but damaged or unreadable
static int read_good(const char *path, const safe_format_t *fmt, void *data)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return file_size(path) < 0 ? 0 : -1;
	unsigned char head[HEAD_MAX];
	size_t want = data ? fmt->size : (fmt->head < fmt->size ? fmt->head : fmt->size);
	bool ok = fmt->head <= HEAD_MAX && file_size(path) == (long)fmt->size;
	if (ok && data)
		ok = fread(data, 1, want, f) == want && fgetc(f) == EOF;
	else if (ok)
		ok = fread(head, 1, want, f) == want;
	fclose(f);
	if (ok && fmt->valid && fmt->head)
		ok = fmt->valid(data ? data : head);
	return ok ? 1 : -1;
}

static bool write_tmp(const char *tmp, const void *data, size_t size)
{
	FILE *f = fopen(tmp, "wb");
	if (!f)
		return false;
	bool ok = true;
	for (size_t at = 0; ok && at < size; at += CHUNK) {
		size_t n = size - at < CHUNK ? size - at : CHUNK;
		ok = fwrite((const char *)data + at, 1, n, f) == n;
	}
	if (fclose(f))
		ok = false;
	return ok && file_size(tmp) == (long)size;
}

// a damaged <name> makes room: it is kept as <name>.bad, never deleted
static bool keep_damaged(const char *path)
{
	char bad[528];
	if (file_size(path) < 0)
		return true;
	name_with(bad, sizeof(bad), path, ".bad");
	remove(bad);
	STEP();
	return rename(path, bad) == 0;
}

// steps 2-5: <name>.tmp is complete. fmt NULL: a text file (no backup kept).
static bool put_in_place(const char *path, const char *tmp, const char *bak, const safe_format_t *fmt)
{
	bool moved = false;
	if (file_size(path) >= 0) {
		if (!fmt || read_good(path, fmt, NULL) > 0) {
			remove(bak);
			STEP();
			if (rename(path, bak))
				return false;
			moved = true;
		} else if (!keep_damaged(path))   // must not replace a good backup
			return false;
		STEP();
	}
	if (rename(tmp, path)) {
		if (moved)
			rename(bak, path);
		return false;
	}
	if (!fmt) {
		STEP();
		remove(bak);
	}
	return true;
}

// <name>.tmp is the only good copy left (a cut-off first save, a failed
// repair): put it in place before anything writes a new .tmp over it
static bool rescue_tmp(const char *path, const char *tmp, const char *bak, const safe_format_t *fmt)
{
	if (read_good(path, fmt, NULL) > 0 || read_good(bak, fmt, NULL) > 0 || read_good(tmp, fmt, NULL) <= 0)
		return true;
	return keep_damaged(path) && rename(tmp, path) == 0;
}

bool safe_write(const char *path, const safe_format_t *fmt, const void *data, bool keep_backup)
{
	char tmp[528], bak[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	name_with(bak, sizeof(bak), path, ".bak");
	if (keep_backup && !rescue_tmp(path, tmp, bak, fmt))
		return false;
	if (!write_tmp(tmp, data, fmt->size)) {
		remove(tmp);
		return false;
	}
	STEP();
	if (!put_in_place(path, tmp, bak, keep_backup ? fmt : NULL)) {
#ifdef SAFEFILE_TEST
		if (safe_test_crash_after != -2)
#endif
			remove(tmp);
		return false;
	}
	return true;
}

FILE *safe_begin(const char *path)
{
	char tmp[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	return fopen(tmp, "w");
}

bool safe_finish(FILE *f, const char *path, bool ok)
{
	char tmp[528], bak[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	name_with(bak, sizeof(bak), path, ".bak");
	if (ferror(f))
		ok = false;
	if (fclose(f))
		ok = false;
	STEP();
	if (!ok || !put_in_place(path, tmp, bak, NULL)) {
		remove(tmp);
		return false;
	}
	return true;
}

void safe_recover(const char *path)
{
	char tmp[528], bak[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	name_with(bak, sizeof(bak), path, ".bak");
	// A text .tmp may be cut off and its full size is unknown: never use it.
	if (file_size(path) < 0 && file_size(bak) >= 0)
		rename(bak, path);
	remove(tmp);
}

void *safe_load(const char *path, const safe_format_t *fmt, unsigned skip, safe_info_t *info)
{
	static const char *const ext[] = { [SAFE_CURRENT] = "", [SAFE_BACKUP] = ".bak", [SAFE_TEMP] = ".tmp" };
	safe_info_t dummy;
	if (!info)
		info = &dummy;
	info->from = SAFE_NONE;
	info->rejected = info->failed = false;
	void *buf = fmt->size ? malloc(fmt->size) : NULL;
	if (!buf) {
		info->failed = true;
		return NULL;
	}
	for (int s = SAFE_CURRENT; s <= SAFE_TEMP; s++) {
		char name[528];
		if (skip & (1u << s))
			continue;
		name_with(name, sizeof(name), path, ext[s]);
		int r = read_good(name, fmt, buf);   // only a good copy is returned
		if (r > 0) {
			info->from = (safe_source_t)s;
			return buf;
		}
		if (r < 0)
			info->rejected = true;
	}
	free(buf);
	return NULL;
}

bool safe_repair(const char *path, const safe_format_t *fmt, const void *data, safe_source_t from)
{
	char src[528];
	if (from == SAFE_TEMP) {
		name_with(src, sizeof(src), path, ".tmp");
		if (!keep_damaged(path))
			return false;
		STEP();
		return rename(src, path) == 0;
	}
	if (from != SAFE_BACKUP)
		return false;
	// the .bak stays as it is; the copy is staged under its own name
	name_with(src, sizeof(src), path, ".rep");
	if (!write_tmp(src, data, fmt->size) || !keep_damaged(path)) {
		remove(src);
		return false;
	}
	STEP();
	if (rename(src, path)) {
		remove(src);
		return false;
	}
	return true;
}

bool safe_stat(const char *path, const safe_format_t *fmt, time_t *mtime)
{
	char other[528];
	struct stat st;
	bool found = stat(path, &st) == 0;
	if (!found) {
		name_with(other, sizeof(other), path, ".bak");
		found = stat(other, &st) == 0;
	}
	if (!found) {
		name_with(other, sizeof(other), path, ".tmp");
		found = read_good(other, fmt, NULL) > 0 && stat(other, &st) == 0;
	}
	if (found && mtime)
		*mtime = st.st_mtime;
	return found;
}

void safe_remove(const char *path)
{
	static const char *const ext[] = { "", ".bak", ".tmp", ".bad", ".rep" };
	char other[528];
	for (size_t i = 0; i < sizeof(ext) / sizeof(ext[0]); i++) {
		name_with(other, sizeof(other), path, ext[i]);
		remove(other);
	}
}
