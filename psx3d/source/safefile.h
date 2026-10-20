// Crash-safe saving: a file is written to <name>.tmp first and only then put
// in place, so a power cut, flat battery or crash during the write never
// leaves a cut-off memory card or state. FAT (the 3DS SD card) cannot rename
// over an existing file, so the old version is renamed away first.
//
// Memory cards, states and their thumbnails keep the previous good version as
// <name>.bak; loading falls back to it when <name> is missing or damaged.
#ifndef SAFEFILE_H
#define SAFEFILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum { SAFE_NONE, SAFE_CURRENT, SAFE_BACKUP, SAFE_TEMP } safe_source_t;

// Write size bytes. keep_backup: the replaced version stays as <name>.bak, but
// only when it had this same size (a damaged file never becomes the backup).
bool safe_write(const char *path, const void *data, size_t size, bool keep_backup);

// Text files: fprintf into the returned <name>.tmp, then safe_finish() puts it
// in place (ok = false or a write error throws it away). No .bak is kept.
FILE *safe_begin(const char *path);
bool safe_finish(FILE *f, const char *path, bool ok);
// Before reading a text file: bring back <name> if a replace was interrupted.
void safe_recover(const char *path);

// Read exactly size bytes from the best complete copy: <name>, else <name>.bak,
// else a complete <name>.tmp. skip_current: <name> failed to load, try the
// others. tolerant: a lone <name> of another size is still read (up to size;
// *got tells how much). Returns where the data came from.
safe_source_t safe_read(const char *path, void *data, size_t size, bool skip_current,
                        bool tolerant, size_t *got);
// After loading from a backup or temp copy: make <name> a good copy of data
// again, leaving <name>.bak alone.
bool safe_repair(const char *path, const void *data, size_t size);
// <name> or its backup exists
bool safe_exists(const char *path);
// Delete <name>, its backup and any leftover temp file
void safe_remove(const char *path);

#ifdef SAFEFILE_TEST
// host tests: stop a replace after this many steps, as if the power went off
extern int safe_test_crash_after;
#endif
#endif
