// Preset updates without an app release. The player does nothing: while the
// library is idle, a cover worker (covers.c, same network lock and pauses)
// asks presets_update_due() and runs presets_update_work(). That fetches a
// small manifest at most once a day; a newer pack made for this app's preset
// schema and defaults version is downloaded to the SD card, its size and
// SHA256 checked and its files unpacked into presets.new. The main thread then
// loads that folder with the normal strict preset parser, and only if every
// file is accepted swaps it in: presets -> presets.old, presets.new -> presets.
// New values apply from the next game start; a running game never changes.
// Any failure keeps the current presets and is only logged. Format and the
// publish procedure: docs/presets.md "Automatic updates".
#include "presetupdate.h"
#include "safefile.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <mbedtls/sha256.h>
#ifndef PRESETUPDATE_TEST
#include <3ds.h>
#include <reent.h>
// libctru's devoptab already links _rmdir_r; calling it directly keeps
// newlib's rmdir() wrapper out, so no library code after it moves.
int _rmdir_r(struct _reent *r, const char *path);
#define rmdir(path) _rmdir_r(_REENT, path)
#endif

void log_printf(const char *fmt, ...);
void game_presets_init(const char *builtin_dir, const char *sd_dir);
bool game_presets_validate(const char *builtin_dir, const char *dir, int version, unsigned files);
void game_presets_moved(const char *sd_dir);
int game_presets_pack(void);
int defaults_version(void);

#ifndef PRESETS_URL
#define PRESETS_URL "https://github.com/BreadChaser/PSX3D/releases/download/presets/"
#endif
#define DAY (24 * 60 * 60)
#define OFFLINE_RETRY_MS (30 * 60 * 1000LL)   // no Wi-Fi: look again in half an hour

#ifdef PRESETUPDATE_TEST
long long pu_test_now, pu_test_ms;
int pu_test_crash_after = -1;
static long long now_s(void) { return pu_test_now; }
static long long now_ms(void) { return pu_test_ms; }
#else
static long long now_s(void) { return time(NULL); }
static long long now_ms(void) { return osGetTime(); }
#endif

static char builtin_dir[128], live[160], staging[160], previous[160], stamp[160], tmp[160];
static char url_base[128] = PRESETS_URL;
static long long last_check;      // time() of the last completed check (SD stamp)
static long long retry_at_ms;     // offline: not before this session time
static int force, force_seen;     // developer "now" requests; the one a check started with
static int staged;                // 1: presets.new is ready for the main thread (atomic)
static pu_manifest_t staged_m;
static int staged_files;
static char status[96] = "idle";

// ---------------------------------------------------------------- manifest
// One flat JSON object of string and integer values, no escapes, unknown keys
// ignored, known keys once each.
int pu_parse_manifest(const char *s, size_t n, pu_manifest_t *m)
{
	static const char *const keys[] = {"format", "pack_version", "schema_min", "schema_max",
		"defaults_version", "size", "sha256", "file"};
	enum { NK = sizeof(keys) / sizeof(*keys) };
	long num[NK] = {0};
	unsigned seen = 0;
	size_t i = 0;
	memset(m, 0, sizeof(*m));
#define WS() while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++
	if (n > PU_MANIFEST_MAX || memchr(s, 0, n)) return PU_BAD_MANIFEST;
	WS();
	if (i == n || s[i++] != '{') return PU_BAD_MANIFEST;
	for (;;) {
		char key[32], str[80];
		size_t k = 0;
		WS();
		if (i == n || s[i++] != '"') return PU_BAD_MANIFEST;
		while (i < n && s[i] != '"') {
			if (k == sizeof(key) - 1 || s[i] == '\\' || (unsigned char)s[i] < 32 || (unsigned char)s[i] > 126) return PU_BAD_MANIFEST;
			key[k++] = s[i++];
		}
		key[k] = 0;
		if (i++ == n) return PU_BAD_MANIFEST;
		WS();
		if (i == n || s[i++] != ':') return PU_BAD_MANIFEST;
		WS();
		bool is_string = i < n && s[i] == '"';
		long value = 0;
		k = 0;
		if (is_string) {
			i++;
			while (i < n && s[i] != '"') {
				if (k == sizeof(str) - 1 || s[i] == '\\' || (unsigned char)s[i] < 32 || (unsigned char)s[i] > 126) return PU_BAD_MANIFEST;
				str[k++] = s[i++];
			}
			str[k] = 0;
			if (i++ == n) return PU_BAD_MANIFEST;
		} else {
			if (i == n || !isdigit((unsigned char)s[i])) return PU_BAD_MANIFEST;
			while (i < n && isdigit((unsigned char)s[i])) {
				if (++k > 9) return PU_BAD_MANIFEST;
				value = value * 10 + (s[i++] - '0');
			}
		}
		for (int j = 0; j < NK; j++) {
			if (strcmp(key, keys[j])) continue;
			if ((seen & 1u << j) || is_string != (j >= 6)) return PU_BAD_MANIFEST;
			seen |= 1u << j;
			num[j] = value;
			if (j >= 6 && strlen(str) >= (j == 6 ? sizeof(m->sha256) : sizeof(m->file))) return PU_BAD_MANIFEST;
			if (j >= 6) strcpy(j == 6 ? m->sha256 : m->file, str);
		}
		WS();
		if (i == n) return PU_BAD_MANIFEST;
		if (s[i] == ',') { i++; continue; }
		if (s[i++] != '}') return PU_BAD_MANIFEST;
		break;
	}
	WS();
#undef WS
	if (i != n || seen != (1u << NK) - 1 || num[0] != 1) return PU_BAD_MANIFEST;
	m->pack_version = num[1]; m->schema_min = num[2]; m->schema_max = num[3];
	m->defaults_version = num[4]; m->size = num[5];
	if (m->pack_version < 1 || m->pack_version > 999999 || m->schema_min < 1 || m->schema_max < m->schema_min ||
	    m->defaults_version < 1 || m->size < 1 || m->size > PU_PACK_MAX || strlen(m->sha256) != 64)
		return PU_BAD_MANIFEST;
	for (int j = 0; j < 64; j++)
		if (!strchr("0123456789abcdef", m->sha256[j])) return PU_BAD_MANIFEST;
	// A file name next to the manifest: no path, no hidden name, ends in .pack.
	size_t len = strlen(m->file);
	if (len < 6 || strcmp(m->file + len - 5, ".pack") || m->file[0] == '.') return PU_BAD_MANIFEST;
	for (size_t j = 0; j < len; j++)
		if (!islower((unsigned char)m->file[j]) && !isdigit((unsigned char)m->file[j]) && !strchr("._-", m->file[j]))
			return PU_BAD_MANIFEST;
	return PU_OK;
}

int pu_decide(const pu_manifest_t *m, int app_schema, int app_defaults, int installed_pack)
{
	if (app_schema < m->schema_min || app_schema > m->schema_max) return PU_SCHEMA;
	if (app_defaults != m->defaults_version) return PU_DEFAULTS;
	if (m->pack_version <= installed_pack) return PU_NOT_NEWER;
	return PU_OK;
}

// ---------------------------------------------------------------- files
bool pu_sha256_file(const char *path, long size, char hex[65], bool (*stop)(void))
{
	FILE *f = fopen(path, "rb");
	unsigned char *buf = f ? malloc(4096) : NULL, digest[32];
	if (!buf) { if (f) fclose(f); return false; }
	mbedtls_sha256_context c;
	mbedtls_sha256_init(&c);
	mbedtls_sha256_starts_ret(&c, 0);
	long total = 0;
	size_t got;
	bool ok = true;
	while (ok && (got = fread(buf, 1, 4096, f)) > 0) {
		total += got;
		ok = total <= size && !(stop && stop());
		if (ok) mbedtls_sha256_update_ret(&c, buf, got);
	}
	ok = ok && !ferror(f) && total == size;
	fclose(f);
	free(buf);
	mbedtls_sha256_finish_ret(&c, digest);
	mbedtls_sha256_free(&c);
	for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", digest[i]);
	return ok;
}

// Remove a flat folder of files (our own staging/previous packs).
static bool rmtree(const char *dir)
{
	DIR *d = opendir(dir);
	if (!d) return true;
	struct dirent *de;
	char path[256];
	bool ok = true;
	while ((de = readdir(d))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
		if (snprintf(path, sizeof(path), "%s/%s", dir, de->d_name) >= (int)sizeof(path) || remove(path)) ok = false;
	}
	closedir(d);
	return !rmdir(dir) && ok;
}
static bool exists(const char *path) { struct stat st; return !stat(path, &st); }

static bool pack_line(FILE *f, char *line, size_t len)
{
	size_t n = 0;
	int c;
	while ((c = fgetc(f)) != EOF && c != '\n') {
		if (n == len - 1 || c < 32 || c > 126) return false;
		line[n++] = c;
	}
	line[n] = 0;
	return c == '\n';
}
static bool pack_name(const char *s)
{
	if (!strcmp(s, "VERSION")) return true;
	size_t len = strlen(s);
	if (len < 5 || len > 63 || strcmp(s + len - 4, ".ini") || !strcmp(s, "defaults.ini")) return false;
	for (size_t i = 0; i < len - 4; i++)
		if (!islower((unsigned char)s[i]) && !isdigit((unsigned char)s[i]) && s[i] != '-') return false;
	return true;
}

// "PSX3D-PRESETS 1\n", then "<name> <size>\n<size bytes>" per file, "END\n".
int pu_unpack(const char *pack, const char *dir, int version, bool (*stop)(void))
{
	FILE *f = fopen(pack, "rb");
	if (!f) return -1;
	char line[96], (*names)[64] = calloc(PU_FILES_MAX, 64), *buf = malloc(4096);
	int count = 0, inis = -1;
	bool ok = names && buf && pack_line(f, line, sizeof(line)) && !strcmp(line, "PSX3D-PRESETS 1") && !mkdir(dir, 0777);
	bool made = ok, have_version = false;
	while (ok) {
		if (!pack_line(f, line, sizeof(line))) { ok = false; break; }
		if (!strcmp(line, "END")) break;
		char *sp = strchr(line, ' '), *end;
		if (!sp) { ok = false; break; }
		*sp = 0;
		long size = strtol(sp + 1, &end, 10);
		if (!pack_name(line) || !isdigit((unsigned char)sp[1]) || *end || size < 1 || size > PU_FILE_MAX ||
		    count == PU_FILES_MAX || (stop && stop())) { ok = false; break; }
		for (int i = 0; i < count; i++) if (!strcmp(names[i], line)) ok = false;
		if (!ok) break;
		strcpy(names[count++], line);
		char path[256];
		snprintf(path, sizeof(path), "%s/%s", dir, line);
		FILE *out = fopen(path, "wb");
		if (!out) { ok = false; break; }
		bool is_version = !strcmp(line, "VERSION");
		char vtext[16] = "";
		for (long left = size; ok && left > 0;) {
			size_t want = left > 4096 ? 4096 : left, got = fread(buf, 1, want, f);
			if (got != want || fwrite(buf, 1, got, out) != got) ok = false;
			if (ok && is_version && size < (long)sizeof(vtext)) memcpy(vtext + (size - left), buf, got);
			left -= got;
		}
		if (fclose(out)) ok = false;
		if (ok && is_version) {
			char want[16];
			snprintf(want, sizeof(want), "%d\n", version);
			ok = !strcmp(vtext, want);
			have_version = true;
		}
	}
	ok = ok && have_version && fgetc(f) == EOF && !ferror(f);
	fclose(f);
	if (ok) inis = count - 1;
	free(names); free(buf);
	if (!ok && made) rmtree(dir);
	return inis;
}

// ---------------------------------------------------------------- state
static void write_stamp(void)
{
	last_check = now_s();
	FILE *f = safe_begin(stamp);
	if (f) {
		fprintf(f, "last=%lld\n", last_check);
		safe_finish(f, stamp, true);
	}
}

void presets_update_init(const char *builtin, const char *base)
{
	snprintf(builtin_dir, sizeof(builtin_dir), "%s", builtin);
	snprintf(live, sizeof(live), "%s/presets", base);
	snprintf(staging, sizeof(staging), "%s/presets.new", base);
	snprintf(previous, sizeof(previous), "%s/presets.old", base);
	snprintf(stamp, sizeof(stamp), "%s/presets-update.txt", base);
	snprintf(tmp, sizeof(tmp), "%s/presets-update.tmp", base);
	last_check = retry_at_ms = 0;
	force = force_seen = 0;
	__atomic_store_n(&staged, 0, __ATOMIC_RELEASE);
	snprintf(status, sizeof(status), "idle");
	// An install cut off between its two renames left no live folder: put the
	// previous pack back (the new one is fetched again on a later day).
	if (!exists(live) && exists(previous) && !rename(previous, live))
		log_printf("presets update: restored %s after an interrupted install\n", live);
	if (exists(staging)) rmtree(staging);
	remove(tmp);
	safe_recover(stamp);
	FILE *f = fopen(stamp, "r");
	if (f) {
		if (fscanf(f, "last=%lld", &last_check) != 1) last_check = 0;
		fclose(f);
	}
}

void presets_update_dev(const char *url, bool now)
{
	if (url && *url) snprintf(url_base, sizeof(url_base), "%s%s", url, url[strlen(url) - 1] == '/' ? "" : "/");
	if (now) { force++; retry_at_ms = 0; }
}

bool presets_update_due(void)
{
	if (!live[0] || __atomic_load_n(&staged, __ATOMIC_ACQUIRE) || now_ms() < retry_at_ms) return false;
	long long t = now_s();
	// A clock set back (last check in the future) counts as due.
	return force != force_seen || t - last_check >= DAY || t < last_check;
}

static int check_request;   // the "now" request the running check answers
static void finish(const char *what, bool done_for_today)
{
	if (done_for_today) force_seen = check_request;
	snprintf(status, sizeof(status), "%s", what);
	log_printf("presets update: %s\n", what);
	if (done_for_today) write_stamp();
	remove(tmp);
}

void presets_update_work(pu_fetch_fn fetch, bool (*stop)(void))
{
	if (!presets_update_due()) return;
	check_request = force;
	char url[256], what[96];
	long http = 0;
	pu_manifest_t m;
	snprintf(url, sizeof(url), "%spresets-d%d-s%d.json", url_base, defaults_version(), PRESET_SCHEMA_VERSION);
	log_printf("presets update: checking %s\n", url);
	int r = fetch(url, tmp, PU_MANIFEST_MAX, &http);
	if (r == PU_FETCH_ABORT) { remove(tmp); return; }   // paused: due again on resume
	if (r == PU_FETCH_NET) {
		retry_at_ms = now_ms() + OFFLINE_RETRY_MS;
		finish("offline", false);
		return;
	}
	if (r != PU_FETCH_OK || http != 200) {
		snprintf(what, sizeof(what), "no manifest (http %ld)", http);
		finish(what, true);
		return;
	}
	char *json = malloc(PU_MANIFEST_MAX + 1);
	FILE *f = json ? fopen(tmp, "rb") : NULL;
	size_t n = f ? fread(json, 1, PU_MANIFEST_MAX + 1, f) : 0;
	if (f) fclose(f);
	int d = pu_parse_manifest(json ? json : "", n, &m);
	free(json);
	if (d == PU_OK) d = pu_decide(&m, PRESET_SCHEMA_VERSION, defaults_version(), game_presets_pack());
	if (d != PU_OK) {
		static const char *const why[] = {"", "bad manifest", "current", "other schema", "other defaults"};
		snprintf(what, sizeof(what), "%s (pack %d, have %d)", why[d], m.pack_version, game_presets_pack());
		finish(what, true);
		return;
	}
	snprintf(url, sizeof(url), "%s%s", url_base, m.file);
	r = fetch(url, tmp, m.size, &http);
	if (r == PU_FETCH_ABORT) { remove(tmp); return; }
	if (r == PU_FETCH_NET) { retry_at_ms = now_ms() + OFFLINE_RETRY_MS; finish("offline (pack)", false); return; }
	char hex[65];
	if (r != PU_FETCH_OK || http != 200) {
		snprintf(what, sizeof(what), "no pack %d (http %ld)", m.pack_version, http);
		finish(what, true);
		return;
	}
	if (!pu_sha256_file(tmp, m.size, hex, stop) || strcmp(hex, m.sha256)) {
		if (stop && stop()) { remove(tmp); return; }
		snprintf(what, sizeof(what), "pack %d: wrong size or SHA256", m.pack_version);
		finish(what, true);
		return;
	}
	rmtree(staging);
	int files = pu_unpack(tmp, staging, m.pack_version, stop);
	if (files < 0) {
		if (stop && stop()) { remove(tmp); return; }
		snprintf(what, sizeof(what), "pack %d: bad archive", m.pack_version);
		finish(what, true);
		return;
	}
	staged_m = m;
	staged_files = files;
	snprintf(what, sizeof(what), "pack %d staged (%d files)", m.pack_version, files);
	finish(what, true);
	__atomic_store_n(&staged, 1, __ATOMIC_RELEASE);
}

// The swap. A cut between the two renames is undone by presets_update_init.
static bool install(void)
{
	int steps = 0;
#ifdef PRESETUPDATE_TEST
#define STEP() if (pu_test_crash_after >= 0 && steps++ >= pu_test_crash_after) return false
#else
#define STEP() (void)steps
#endif
	if (exists(live)) {
		if (exists(previous) && !rmtree(previous)) return false;
		STEP();
		if (rename(live, previous)) return false;
	}
	STEP();
	if (rename(staging, live)) {
		// Put the current pack back where the next start expects it.
		if (!exists(live) && exists(previous)) rename(previous, live);
		return false;
	}
#undef STEP
	return true;
}

bool presets_update_poll(void)
{
	if (!__atomic_load_n(&staged, __ATOMIC_ACQUIRE)) return false;
	bool ok = game_presets_validate(builtin_dir, staging, staged_m.pack_version, staged_files);
	if (ok) ok = install();
#ifdef PRESETUPDATE_TEST
	if (!ok && pu_test_crash_after >= 0) { __atomic_store_n(&staged, 0, __ATOMIC_RELEASE); return false; }   // "power cut": nothing more runs
#endif
	// Success: the parsed data is the live folder's now (one parse, about
	// 0.2 s on the Old 3DS, once per new pack). Otherwise reload what is there.
	if (ok) game_presets_moved(live);
	else {
		if (exists(staging)) rmtree(staging);
		game_presets_init(builtin_dir, live);
	}
	snprintf(status, sizeof(status), ok ? "installed pack %d" : "pack %d rejected", staged_m.pack_version);
	log_printf("presets update: %s\n", status);
	__atomic_store_n(&staged, 0, __ATOMIC_RELEASE);
	return ok;
}

// Developer: remove every downloaded pack and the stamp (test consoles go back
// to the built-in presets). Main thread, library, with the cover workers stopped.
bool presets_update_dev_reset(void)
{
	bool ok = !__atomic_load_n(&staged, __ATOMIC_ACQUIRE) && rmtree(staging) && rmtree(previous) && rmtree(live);
	remove(stamp);
	remove(tmp);
	// Nothing more this session (no stamp file: the next start checks again).
	last_check = now_s();
	retry_at_ms = 0;
	force_seen = force;
	game_presets_init(builtin_dir, live);
	snprintf(status, sizeof(status), ok ? "reset" : "reset failed");
	return ok;
}

const char *presets_update_status(void) { return status; }
