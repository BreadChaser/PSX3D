// Preset updates without an app release (docs/presets.md "Automatic updates").
// A cover worker thread fetches a small manifest at most once a day while the
// library is idle; a newer, matching pack is downloaded, checked (size,
// SHA256, every file through the preset parser) and swapped in atomically.
#ifndef PRESETUPDATE_H
#define PRESETUPDATE_H
#include <stdbool.h>
#include <stddef.h>

// Bump when preset_schema.def gains keys or values an older parser rejects.
// tools/presets/pack.py reads this line.
#define PRESET_SCHEMA_VERSION 1

#define PU_MANIFEST_MAX 4096
#define PU_PACK_MAX (512 * 1024)
#define PU_FILES_MAX 256          // = presets.c MAX_FILES (.ini) + VERSION
#define PU_FILE_MAX (512 * 255)   // presets.c MAX_LINES * MAX_LINE

// fetch() results
enum { PU_FETCH_OK, PU_FETCH_NET, PU_FETCH_ABORT, PU_FETCH_FAIL };
// Download url into path, at most limit bytes. *http: the status code.
// PU_FETCH_NET: no network or server unreachable (try again later today).
// PU_FETCH_ABORT: stop() became true. PU_FETCH_FAIL: anything else.
typedef int (*pu_fetch_fn)(const char *url, const char *path, size_t limit, long *http);

typedef struct {
	int pack_version, schema_min, schema_max, defaults_version;
	long size;
	char sha256[65], file[64];
} pu_manifest_t;

enum {
	PU_OK, PU_BAD_MANIFEST, PU_NOT_NEWER, PU_SCHEMA, PU_DEFAULTS,
};
int pu_parse_manifest(const char *json, size_t n, pu_manifest_t *m);
int pu_decide(const pu_manifest_t *m, int app_schema, int app_defaults, int installed_pack);
// Unpack a pack file into dir (created; must not exist). Returns the number of
// .ini files, or -1. The pack's VERSION must equal version.
int pu_unpack(const char *pack, const char *dir, int version, bool (*stop)(void));
bool pu_sha256_file(const char *path, long size, char hex[65], bool (*stop)(void));

// base = sdmc:/3ds/PSX3D; builtin = romfs:/presets. Recovers an interrupted
// install. Call before game_presets_init.
void presets_update_init(const char *builtin, const char *base);
// Developer override (numeric LAN HTTP test server); also makes a check due now.
void presets_update_dev(const char *url, bool now);
bool presets_update_dev_reset(void);   // main thread, library, covers stopped
// Worker thread, with the network lock held. Cheap when nothing is due.
bool presets_update_due(void);
void presets_update_work(pu_fetch_fn fetch, bool (*stop)(void));
// Main thread, library idle, every frame: installs a staged pack. true once
// when the game settings changed.
bool presets_update_poll(void);
const char *presets_update_status(void);
#ifdef PRESETUPDATE_TEST
extern long long pu_test_now;     // seconds; replaces time()
extern long long pu_test_ms;      // milliseconds; replaces the session clock
extern int pu_test_crash_after;   // install: stop after this many renames
#endif
#endif
