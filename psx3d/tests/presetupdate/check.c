// Host test of the automatic preset update (presetupdate.c) with the real
// preset parser (presets.c) and safe files (safefile.c). A fake fetch serves
// files from a local "server" folder. Run: bash psx3d/tests/presetupdate/run.sh
#include <assert.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "psx3d.h"
#include "presetupdate.h"

bool g_new3ds;
psx3d_config g_cfg;
char sw_opt[8][48];
bool config_in_game(void) { return false; }
void cheats_disc_changed(void) {}
void options_mark_updated(void) {}
bool config_rewind_on(void) { return false; }
static bool verbose;
void log_printf(const char *fmt, ...)
{
	if (!verbose) return;
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

static const char *builtin, *server, *base;
static char live[512], old_dir[512], new_dir[512];
static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

// ---------------------------------------------------------------- fake network
enum { NET_OK, NET_DOWN, NET_ABORT_MANIFEST, NET_ABORT_PACK, NET_SHORT };
static int net_mode, fetches;
static int fake_fetch(const char *url, const char *path, size_t limit, long *http)
{
	fetches++;
	const char *prefix = "http://test/";
	if (net_mode == NET_DOWN) return PU_FETCH_NET;
	bool pack = strstr(url, ".pack") != NULL;
	if ((net_mode == NET_ABORT_MANIFEST && !pack) || (net_mode == NET_ABORT_PACK && pack)) return PU_FETCH_ABORT;
	assert(!strncmp(url, prefix, strlen(prefix)));
	char src[600];
	snprintf(src, sizeof(src), "%s/%s", server, url + strlen(prefix));
	FILE *in = fopen(src, "rb"), *out = fopen(path, "wb");
	assert(out);
	*http = in ? 200 : 404;
	size_t total = 0, n;
	char buf[4096];
	int r = PU_FETCH_OK;
	while (in && (n = fread(buf, 1, sizeof(buf), in)) > 0) {
		if (net_mode == NET_SHORT && pack && total + n > 100) n = 100 - total;   // connection dropped
		total += n;
		if (total > limit) { r = PU_FETCH_FAIL; break; }    // curl: write callback refused
		fwrite(buf, 1, n, out);
		if (net_mode == NET_SHORT && pack && total >= 100) break;
	}
	if (in) fclose(in);
	fclose(out);
	return r;
}
static bool never(void) { return false; }

// ---------------------------------------------------------------- helpers
static void sh(const char *fmt, ...)
{
	char cmd[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(cmd, sizeof(cmd), fmt, ap);
	va_end(ap);
	if (system(cmd)) { printf("command failed: %s\n", cmd); exit(1); }
}
static bool exists(const char *p) { struct stat st; return !stat(p, &st); }
static int file_version(const char *dir)
{
	char p[600];
	snprintf(p, sizeof(p), "%s/VERSION", dir);
	FILE *f = fopen(p, "r");
	int v = 0;
	if (f) { if (fscanf(f, "%d", &v) != 1) v = 0; fclose(f); }
	return v;
}
// A fresh start of the app: recovery, then the startup preset load.
static void start(void)
{
	presets_update_init(builtin, base);
	presets_update_dev("http://test/", false);
	game_presets_init(builtin, live);
}
// One idle-library check: the worker part, then the main loop part.
static bool check(void)
{
	presets_update_work(fake_fetch, never);
	return presets_update_poll();
}
static void reset_sd(void) { sh("rm -rf '%s' && mkdir -p '%s'", base, base); }
// Publish a pack: copy of the built-in presets with VERSION v (and a change).
static void publish(int v, const char *edit)
{
	sh("rm -rf '%s/src' '%s/out' && cp -r '%s' '%s/src' && printf '%d\\n' > '%s/src/VERSION'",
	   server, server, builtin, server, v, server);
	if (edit) sh("cd '%s/src' && %s", server, edit);
	sh("python3 '%s/tools/presets/pack.py' --no-check --presets '%s/src' --out '%s/out' > /dev/null && cp '%s'/out/* '%s/'",
	   getenv("REPO"), server, server, server, server);
}
static const char *status(void) { return presets_update_status(); }

// ---------------------------------------------------------------- manifest
static void test_manifest(void)
{
	pu_manifest_t m;
	const char *sha = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
	char good[512], j[1024];
	snprintf(good, sizeof(good), "{\"format\":1,\"pack_version\":4,\"schema_min\":1,\"schema_max\":2,"
	         "\"defaults_version\":1,\"file\":\"presets-d1-s1-v4.pack\",\"size\":2000,\"sha256\":\"%s\"}", sha);
	CHECK(pu_parse_manifest(good, strlen(good), &m) == PU_OK && m.pack_version == 4 && m.size == 2000 &&
	      m.schema_max == 2 && !strcmp(m.sha256, sha) && !strcmp(m.file, "presets-d1-s1-v4.pack"));
	// unknown keys are ignored; whitespace anywhere
	snprintf(j, sizeof(j), " {\n \"note\" : \"hi\", %s\n", good + 1);
	CHECK(pu_parse_manifest(j, strlen(j), &m) == PU_OK);
	static const char *const bad[][2] = {
		{"\"format\":1", "\"format\":2"},
		{"\"pack_version\":4", "\"pack_version\":0"},
		{"\"pack_version\":4", "\"pack_version\":\"4\""},
		{"\"pack_version\":4", "\"pack_version\":-4"},
		{"\"pack_version\":4", "\"pack_version\":4,\"pack_version\":5"},
		{"\"pack_version\":4,", ""},
		{"\"schema_max\":2", "\"schema_max\":0"},
		{"\"size\":2000", "\"size\":0"},
		{"\"size\":2000", "\"size\":524289"},
		{"\"size\":2000", "\"size\":99999999999"},
		{"presets-d1-s1-v4.pack", "../presets.pack"},
		{"presets-d1-s1-v4.pack", "presets/x.pack"},
		{"presets-d1-s1-v4.pack", "Presets.pack"},
		{"presets-d1-s1-v4.pack", "presets.zip"},
		{"presets-d1-s1-v4.pack", ".pack"},
		{"0123456789abcdef0123", "0123456789ABCDEF0123"},
		{"0123456789abcdef0123", "0123456789abcdef012"},
		{"\"file\"", "\"fi\\le\""},
		{"}", "},"},
		{"}", "}x"},
		{"}", ""},
		{"\"defaults_version\":1", "\"defaults_version\":{}"},
	};
	for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
		const char *at = strstr(good, bad[i][0]);
		assert(at);
		snprintf(j, sizeof(j), "%.*s%s%s", (int)(at - good), good, bad[i][1], at + strlen(bad[i][0]));
		if (pu_parse_manifest(j, strlen(j), &m) != PU_BAD_MANIFEST) { printf("accepted: %s\n", j); failures++; }
	}
	char *big = malloc(PU_MANIFEST_MAX + 2);
	memset(big, ' ', PU_MANIFEST_MAX + 1);
	memcpy(big, good, strlen(good));
	CHECK(pu_parse_manifest(big, PU_MANIFEST_MAX, &m) == PU_OK);
	CHECK(pu_parse_manifest(big, PU_MANIFEST_MAX + 1, &m) == PU_BAD_MANIFEST);
	free(big);
	CHECK(pu_parse_manifest("", 0, &m) == PU_BAD_MANIFEST);
	CHECK(pu_parse_manifest(good, strlen(good) - 1, &m) == PU_BAD_MANIFEST);

	pu_parse_manifest(good, strlen(good), &m);
	CHECK(pu_decide(&m, 1, 1, 3) == PU_OK);
	CHECK(pu_decide(&m, 2, 1, 3) == PU_OK);
	CHECK(pu_decide(&m, 3, 1, 3) == PU_SCHEMA);
	m.schema_min = 2;
	CHECK(pu_decide(&m, 1, 1, 3) == PU_SCHEMA);
	m.schema_min = 1;
	CHECK(pu_decide(&m, 1, 2, 3) == PU_DEFAULTS);
	CHECK(pu_decide(&m, 1, 1, 4) == PU_NOT_NEWER);
	CHECK(pu_decide(&m, 1, 1, 5) == PU_NOT_NEWER);
}

// ---------------------------------------------------------------- pack format
static void raw_pack(const char *path, const char *body, size_t n)
{
	FILE *f = fopen(path, "wb");
	fwrite(body, 1, n, f);
	fclose(f);
}
static void test_unpack(void)
{
	char pack[600], dir[600];
	snprintf(pack, sizeof(pack), "%s/t.pack", server);
	snprintf(dir, sizeof(dir), "%s/unpacked", server);
	sh("rm -rf '%s'", dir);
	const char ini[] = "title = A\nversion = 1\nserials = SLUS00001\n";
	char good[512];
	int n = snprintf(good, sizeof(good), "PSX3D-PRESETS 1\nVERSION 2\n4\na-b.ini %zu\n%sEND\n", strlen(ini), ini);
	raw_pack(pack, good, n);
	CHECK(pu_unpack(pack, dir, 4, never) == 1 && exists(dir));
	CHECK(pu_unpack(pack, dir, 4, never) == -1);   // the folder must not exist yet
	sh("rm -rf '%s'", dir);
	CHECK(pu_unpack(pack, dir, 5, never) == -1 && !exists(dir));   // VERSION differs from the manifest
	static const char *const bad[] = {
		"PSX3D-PRESETS 2\nVERSION 2\n4\nEND\n",
		"PSX3D-PRESETS 1\nVERSION 2\n4\n",                         // no END
		"PSX3D-PRESETS 1\nVERSION 2\n4\nEND\nx",                   // bytes after END
		"PSX3D-PRESETS 1\na.ini 3\nabcEND\n",                      // no VERSION
		"PSX3D-PRESETS 1\nVERSION 2\n4\nVERSION 2\n4\nEND\n",      // twice
		"PSX3D-PRESETS 1\nVERSION 2\n4\na.ini 3\nabca.ini 3\nabcEND\n",
		"PSX3D-PRESETS 1\nVERSION 2\n4\ndefaults.ini 3\nabcEND\n",
		"PSX3D-PRESETS 1\nVERSION 2\n4\n../a.ini 3\nabcEND\n",
		"PSX3D-PRESETS 1\nVERSION 2\n4\nA.ini 3\nabcEND\n",
		"PSX3D-PRESETS 1\nVERSION 2\n4\na.txt 3\nabcEND\n",
		"PSX3D-PRESETS 1\nVERSION 2\n4\na.ini 0\nEND\n",
		"PSX3D-PRESETS 1\nVERSION 2\n4\na.ini +3\nabcEND\n",
		"PSX3D-PRESETS 1\nVERSION 2\n4\na.ini 3x\nabcEND\n",
		"PSX3D-PRESETS 1\nVERSION 2\n4\na.ini 99\nabcEND\n",       // runs past the end
		"PSX3D-PRESETS 1\nVERSION 2\n4\na.ini 130561\nEND\n",      // over the file bound
		"PSX3D-PRESETS 1\nVERSION 1\n4END\n",                       // VERSION without newline
		"PSX3D-PRESETS 1\nVERSION 3\n04\nEND\n",
	};
	for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
		raw_pack(pack, bad[i], strlen(bad[i]));
		if (pu_unpack(pack, dir, 4, never) != -1 || exists(dir)) { printf("unpacked bad pack %u\n", i); failures++; }
		sh("rm -rf '%s'", dir);
	}
	// too many files
	FILE *f = fopen(pack, "wb");
	fputs("PSX3D-PRESETS 1\nVERSION 2\n4\n", f);
	for (int i = 0; i < PU_FILES_MAX; i++) fprintf(f, "f%d.ini 1\nx", i);
	fputs("END\n", f);
	fclose(f);
	CHECK(pu_unpack(pack, dir, 4, never) == -1 && !exists(dir));
	remove(pack);
}

// ---------------------------------------------------------------- end to end
static void test_flow(void)
{
	char stampf[600];
	snprintf(stampf, sizeof(stampf), "%s/presets-update.txt", base);
	reset_sd();
	pu_test_now = 1800000000; pu_test_ms = 1000;
	start();
	int builtin_pack = game_presets_pack();
	CHECK(builtin_pack == file_version(builtin));
	CHECK(presets_update_due());

	// nothing newer published: a check, no download, done for the day
	publish(builtin_pack, NULL);
	fetches = 0;
	CHECK(!check() && fetches == 1 && !exists(live) && exists(stampf));
	CHECK(!presets_update_due());
	pu_test_now += 23 * 3600;
	CHECK(!presets_update_due());
	pu_test_now += 3600;
	CHECK(presets_update_due());
	// the stamp survives a restart
	pu_test_now -= 3600;
	start();
	CHECK(!presets_update_due());
	// a clock set back is due again
	pu_test_now -= 7200 + 24 * 3600;
	CHECK(presets_update_due());
	pu_test_now += 7200 + 24 * 3600 + 3600;

	// a newer pack, with one game changed: installed, applies after the swap
	char edit[128];
	snprintf(edit, sizeof(edit), "sed -i 's/^version = .*/version = %d/' silent-hill.ini", builtin_pack + 1);
	publish(builtin_pack + 1, edit);
	CHECK(check());
	CHECK(file_version(live) == builtin_pack + 1 && game_presets_pack() == builtin_pack + 1);
	CHECK(!exists(new_dir) && !exists(old_dir));
	CHECK(strstr(status(), "installed") != NULL);
	CHECK(!presets_update_due());
	// the changed game takes its new file at the next disc identification
	game_presets_disc_log("CD-ROM ID: SLUS_007.07\n");
	CHECK(game_preset_version() == builtin_pack + 1 && !strcmp(game_preset_source(), "sd"));
	game_presets_reset();
	// same day: nothing more is fetched
	fetches = 0;
	presets_update_work(fake_fetch, never);
	CHECK(fetches == 0);
	// next start reads it from the SD card
	start();
	CHECK(game_presets_pack() == builtin_pack + 1);

	// a second update keeps the first as presets.old
	pu_test_now += 86400;
	publish(builtin_pack + 2, NULL);
	CHECK(check() && file_version(live) == builtin_pack + 2 && file_version(old_dir) == builtin_pack + 1);
	int current = builtin_pack + 2;

	// damaged downloads: nothing changes, and no retry the same day
	struct { const char *name, *edit; int mode; } broken[] = {
		{"flipped byte", "MANGLE", NET_OK},
		{"cut off", NULL, NET_SHORT},
		{"bigger than the manifest says", "GROW", NET_OK},
		{"wrong hash in manifest", "HASH", NET_OK},
		{"pack missing", "NOPACK", NET_OK},
		{"bad manifest", "BADJSON", NET_OK},
		{"other defaults", "DEFAULTS", NET_OK},
		{"other schema", "SCHEMA", NET_OK},
		{"invalid preset inside", "INVALID", NET_OK},
		{"bad archive", "ARCHIVE", NET_OK},
	};
	for (unsigned i = 0; i < sizeof(broken) / sizeof(*broken); i++) {
		pu_test_now += 86400;
		const char *e = broken[i].edit;
		publish(current + 1, e && !strcmp(e, "INVALID") ? "echo 'bogus_key = 1' >> silent-hill.ini" : NULL);
		char pack[600], js[600];
		snprintf(pack, sizeof(pack), "%s/presets-d%d-s%d-v%d.pack", server, defaults_version(), PRESET_SCHEMA_VERSION, current + 1);
		snprintf(js, sizeof(js), "%s/presets-d%d-s%d.json", server, defaults_version(), PRESET_SCHEMA_VERSION);
		if (e && !strcmp(e, "MANGLE")) sh("printf 'X' | dd of='%s' bs=1 seek=40 conv=notrunc status=none", pack);
		if (e && !strcmp(e, "GROW")) sh("printf 'extra' >> '%s'", pack);
		if (e && !strcmp(e, "HASH"))
			sh("python3 -c \"import json,sys; p=sys.argv[1]; m=json.load(open(p)); m['sha256']=('1' if m['sha256'][0]=='0' else '0')+m['sha256'][1:]; json.dump(m,open(p,'w'))\" '%s'", js);
		if (e && !strcmp(e, "NOPACK")) remove(pack);
		if (e && !strcmp(e, "BADJSON")) sh("sed -i 's/\"format\": 1/\"format\": 1,/' '%s'", js);
		if (e && !strcmp(e, "DEFAULTS")) sh("sed -i 's/\"defaults_version\": [0-9]*/\"defaults_version\": 99/' '%s'", js);
		if (e && !strcmp(e, "SCHEMA")) sh("sed -i 's/\"schema_min\": [0-9]*/\"schema_min\": 50/;s/\"schema_max\": [0-9]*/\"schema_max\": 60/' '%s'", js);
		if (e && !strcmp(e, "ARCHIVE")) {
			// a well-hashed pack whose VERSION disagrees with the manifest
			sh("sed -i 's/\"pack_version\": [0-9]*/\"pack_version\": %d/' '%s'", current + 2, js);
		}
		net_mode = broken[i].mode;
		bool installed = check();
		net_mode = NET_OK;
		if (installed || file_version(live) != current || exists(new_dir) || game_presets_pack() != current) {
			printf("FAIL broken download '%s': installed %d, live %d, status %s\n", broken[i].name, installed, file_version(live), status());
			failures++;
		}
		CHECK(!presets_update_due());
		CHECK(file_version(old_dir) == current - 1);
	}

	// no network: not stamped, retried after half an hour of session time
	pu_test_now += 86400;
	publish(current + 1, NULL);
	net_mode = NET_DOWN;
	CHECK(!check() && strstr(status(), "offline"));
	CHECK(!presets_update_due());
	pu_test_ms += 29 * 60 * 1000;
	CHECK(!presets_update_due());
	pu_test_ms += 2 * 60 * 1000;
	CHECK(presets_update_due());
	// a game starts during the manifest or pack download: still due afterwards
	net_mode = NET_ABORT_MANIFEST;
	CHECK(!check() && presets_update_due());
	net_mode = NET_ABORT_PACK;
	CHECK(!check() && presets_update_due() && file_version(live) == current);
	net_mode = NET_OK;
	CHECK(check() && file_version(live) == current + 1);
	current++;

	// power cut in the middle of the swap, after each step
	for (int cut = 0; cut < 2; cut++) {
		pu_test_now += 86400;
		publish(current + 1, NULL);
		pu_test_crash_after = cut;
		CHECK(!check());
		pu_test_crash_after = -1;
		start();   // the next start repairs the folders
		if (file_version(live) != current || exists(new_dir) || game_presets_pack() != current) {
			printf("FAIL cut after step %d: live %d pack %d\n", cut, file_version(live), game_presets_pack());
			failures++;
		}
		// stamped before the swap: tomorrow it installs
		CHECK(!presets_update_due());
		pu_test_now += 86400;
		CHECK(check() && file_version(live) == current + 1);
		current++;
	}

	// an SD pack older than the built-in pack (app updated) is ignored, and a
	// published pack not newer than the built-in one is not fetched
	reset_sd();
	sh("mkdir -p '%s' && cp '%s'/*.ini '%s/' && printf '1\\n' > '%s/VERSION'", live, builtin, live, live);
	start();
	CHECK(game_presets_pack() == builtin_pack);
	publish(builtin_pack, NULL);
	fetches = 0;
	CHECK(!check() && fetches == 1 && file_version(live) == 1);

	// developer reset: back to the built-in presets, due again
	pu_test_now += 86400;
	publish(builtin_pack + 1, NULL);
	CHECK(check() && game_presets_pack() == builtin_pack + 1);
	CHECK(presets_update_dev_reset());
	CHECK(!exists(live) && !exists(old_dir) && !exists(stampf) && game_presets_pack() == builtin_pack && !presets_update_due());
	// a "now" request still works after it
	presets_update_dev(NULL, true);
	CHECK(presets_update_due());
	presets_update_work(fake_fetch, never);
	CHECK(!presets_update_due());
}

int main(int argc, char **argv)
{
	if (argc != 4) { fprintf(stderr, "usage: check BUILTIN_PRESETS SERVER_DIR SD_BASE\n"); return 2; }
	builtin = argv[1]; server = argv[2]; base = argv[3];
	verbose = getenv("VERBOSE") != NULL;
	snprintf(live, sizeof(live), "%s/presets", base);
	snprintf(old_dir, sizeof(old_dir), "%s/presets.old", base);
	snprintf(new_dir, sizeof(new_dir), "%s/presets.new", base);
	sh("mkdir -p '%s' '%s'", server, base);
	test_manifest();
	test_unpack();
	test_flow();
	if (failures) { printf("presetupdate: %d failures\n", failures); return 1; }
	printf("presetupdate: all checks passed\n");
	return 0;
}
