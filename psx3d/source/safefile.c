// Crash-safe file replacement for player data (see safefile.h).
//
// Replace steps; a power cut between any two leaves a loadable copy:
//   1. write <name>.tmp completely and close it (the 3DS FS flushes on close)
//   2. remove <name>.bak                    (<name> still the old good file)
//   3. rename <name> -> <name>.bak          (old good file now the backup)
//   4. rename <name>.tmp -> <name>          (new file in place)
//   5. text files only: remove <name>.bak
// FAT on the 3DS SD card cannot rename over an existing file, hence 2 and 3.
#include "safefile.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHUNK (64 * 1024)   // bounded FS calls, so a writer thread yields often

#ifdef SAFEFILE_TEST
int safe_test_crash_after = -1;
static bool crash(void)
{
	return safe_test_crash_after >= 0 && safe_test_crash_after-- == 0;
}
#define STEP() do { if (crash()) return false; } while (0)
// FAT behaviour on the host: rename never replaces an existing file
int safe_test_rename(const char *from, const char *to);
#define rename safe_test_rename
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

// steps 2-5: <name>.tmp is complete
static bool put_in_place(const char *path, const char *tmp, const char *bak, long keep_size)
{
	bool moved = false;
	long cur = file_size(path);
	if (cur >= 0) {
		if (keep_size < 0 || cur == keep_size) {
			remove(bak);
			STEP();
			if (rename(path, bak))
				return false;
			moved = true;
		} else if (remove(path))   // damaged: must not replace a good backup
			return false;
		STEP();
	}
	if (rename(tmp, path)) {
		if (moved)
			rename(bak, path);
		return false;
	}
	if (keep_size < 0) {
		STEP();
		remove(bak);
	}
	return true;
}

bool safe_write(const char *path, const void *data, size_t size, bool keep_backup)
{
	char tmp[528], bak[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	name_with(bak, sizeof(bak), path, ".bak");
	if (!write_tmp(tmp, data, size)) {
		remove(tmp);
		return false;
	}
	STEP();
	if (!put_in_place(path, tmp, bak, keep_backup ? (long)size : -1)) {
#ifdef SAFEFILE_TEST
		if (safe_test_crash_after == -1)
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
	if (!ok || !put_in_place(path, tmp, bak, -1)) {
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

static bool read_exact(const char *path, void *data, size_t size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	bool ok = fread(data, 1, size, f) == size && fgetc(f) == EOF;
	fclose(f);
	return ok;
}

safe_source_t safe_read(const char *path, void *data, size_t size, bool skip_current,
                        bool tolerant, size_t *got)
{
	char tmp[528], bak[528];
	name_with(tmp, sizeof(tmp), path, ".tmp");
	name_with(bak, sizeof(bak), path, ".bak");
	if (got)
		*got = 0;
	if (!skip_current && read_exact(path, data, size)) {
		remove(tmp);   // an unfinished newer write; <name> is complete
		if (got)
			*got = size;
		return SAFE_CURRENT;
	}
	safe_source_t from = SAFE_NONE;
	if (read_exact(bak, data, size))
		from = SAFE_BACKUP;
	else if (read_exact(tmp, data, size))
		from = SAFE_TEMP;
	if (from != SAFE_NONE) {
		if (got)
			*got = size;
		return from;
	}
	if (!skip_current && tolerant) {   // e.g. a memory card from another tool
		FILE *f = fopen(path, "rb");
		if (f) {
			size_t n = fread(data, 1, size, f);
			fclose(f);
			if (got)
				*got = n;
			if (n)
				return SAFE_CURRENT;
		}
	}
	return SAFE_NONE;
}

bool safe_repair(const char *path, const void *data, size_t size)
{
	remove(path);   // damaged or missing; the backup stays as it is
	return safe_write(path, data, size, true);
}

bool safe_exists(const char *path)
{
	char bak[528];
	name_with(bak, sizeof(bak), path, ".bak");
	return file_size(path) >= 0 || file_size(bak) >= 0;
}

void safe_remove(const char *path)
{
	char other[528];
	remove(path);
	name_with(other, sizeof(other), path, ".bak");
	remove(other);
	name_with(other, sizeof(other), path, ".tmp");
	remove(other);
}
