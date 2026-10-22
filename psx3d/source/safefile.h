// Crash-safe saving: a file is written to <name>.tmp first and only then put
// in place, so a power cut, flat battery or crash during the write never
// leaves a cut-off memory card or state. FAT (the 3DS SD card) cannot rename
// over an existing file, so the old version is renamed away first.
//
// Memory cards, states and their thumbnails keep the previous good version as
// <name>.bak; loading falls back to it (then to a complete <name>.tmp) when
// <name> is missing or damaged. A damaged <name> is never deleted: it is kept
// as <name>.bad when it has to make room.
#ifndef SAFEFILE_H
#define SAFEFILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

typedef enum { SAFE_NONE, SAFE_CURRENT, SAFE_BACKUP, SAFE_TEMP } safe_source_t;

// What a good copy of a binary file is: exactly size bytes whose first head
// bytes pass valid() (NULL: any content) and, if given, whose whole content
// passes deep() (memory card checksums). A copy that passes valid() but not
// deep() is "weak": it is loaded only when no good copy exists, it never
// takes the place of a good backup, and it is kept as <name>.bad instead.
typedef struct {
	size_t size;
	size_t head;
	bool (*valid)(const void *head);
	bool (*deep)(const void *data, size_t size);
	// the backup about to be replaced (a version two saves old): keep it
	// somewhere else (history.c renames it away). NULL, or if it is still
	// there afterwards: it is deleted.
	void (*retire)(const char *bak, void *ctx);
	void *ctx;
} safe_format_t;

typedef struct {
	safe_source_t from;   // where the returned copy came from
	bool rejected;        // a copy was there but damaged (or could not be read)
	bool failed;          // no memory to read it: nothing is known
	bool weak;            // the returned copy fails deep(): there is no better one
} safe_info_t;

// Write size = fmt->size bytes. keep_backup: a good current version stays as
// <name>.bak; a damaged one is kept as <name>.bad instead.
bool safe_write(const char *path, const safe_format_t *fmt, const void *data, bool keep_backup);

// Text files: fprintf into the returned <name>.tmp, then safe_finish() puts it
// in place (ok = false or a write error throws it away). No .bak is kept.
FILE *safe_begin(const char *path);
bool safe_finish(FILE *f, const char *path, bool ok);
// Before reading a text file: bring back <name> if a replace was interrupted.
void safe_recover(const char *path);

// The best good copy, in a new buffer of fmt->size bytes (NULL: none). Tries
// <name>, then <name>.bak, then <name>.tmp (the first weak copy if none is
// good), minus the sources in skip (bits
// 1 << SAFE_CURRENT etc., for copies the caller could not use). Nothing on the
// SD card changes.
void *safe_load(const char *path, const safe_format_t *fmt, unsigned skip, safe_info_t *info);
// After loading from <name>.bak or <name>.tmp (from): make <name> a good copy
// again. The source copy is never rewritten or removed first: a .tmp is
// renamed into place, a .bak is copied. A damaged <name> becomes <name>.bad.
bool safe_repair(const char *path, const safe_format_t *fmt, const void *data, safe_source_t from);
// A save is there: <name> or its backup, or a good <name>.tmp (a first save
// cut off before it was put in place). *mtime: its time.
bool safe_stat(const char *path, const safe_format_t *fmt, time_t *mtime);
// Delete <name>, its backup and any leftover temp file
void safe_remove(const char *path);

#ifdef SAFEFILE_TEST
// host tests: stop a replace after this many steps, as if the power went off
extern int safe_test_crash_after;
#endif
#endif
