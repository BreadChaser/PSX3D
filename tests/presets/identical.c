// Host test: the data files reproduce v0.4.1 exactly, and the SD layer follows
// the version rules. Run from the repo root: bash tests/presets/run.sh
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "psx3d.h"
#include "widescreen.h"
#include "legacy_v041.h"

bool g_new3ds;
static unsigned char ram[0x200000];
void *ws_memory(void) { return ram; }
void ws_invalidate(uint32_t a, unsigned n) { (void)a; (void)n; }
void cheats_disc_changed(void) {}
void options_mark_updated(void) {}
static bool quiet;
void log_printf(const char *fmt, ...)
{
	if (quiet) return;
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

static void identify(const char *serial)
{
	char msg[64];
	snprintf(msg, sizeof(msg), "CD-ROM ID: %s\n", serial);
	game_presets_disc_log(msg);
}

// v0.4.1 bundle models= values, frozen in v041-ws-models.txt.
typedef struct { char serial[10], fp[9]; int models[2]; } bundle_t;
static bundle_t bundles[64];
static int nbundles;
static const bundle_t *bundle_of(const char *serial)
{
	for (int i = 0; i < nbundles; i++)
		if (!strcmp(bundles[i].serial, serial)) return &bundles[i];
	return NULL;
}

// What a player sees, for one serial on one console.
typedef struct {
	int ff, rw, tc, l2r2, l3r3, stick, binds[6], frameskip, curated, ws_ok, ws_note;
} view_t;

static view_t legacy_view(const char *serial, int new3ds)
{
	const legacy_preset_t *p = NULL;
	for (size_t i = 0; i < sizeof(legacy_presets) / sizeof(legacy_presets[0]); i++)
		if (!strcmp(legacy_presets[i].serial, serial)) p = &legacy_presets[i];
	legacy_options_t c = legacy_capabilities(p, new3ds);
	const bundle_t *b = bundle_of(serial);
	int policy = b ? b->models[new3ds] : -1;
	bool allowed = !b || policy == WS_OFFERED || policy == WS_COSTLY;
	view_t v = { c.fast_forward, c.rewind, c.touch_controls, c.needs_l2r2, c.needs_l3r3, c.needs_right_stick,
		{0}, c.frameskip, p != NULL, allowed && c.widescreen, policy };
	memcpy(v.binds, c.touch_bindings, sizeof(v.binds));
	return v;
}

static view_t new_view(const char *dir, const char *serial, int new3ds)
{
	g_new3ds = new3ds;
	identify(serial);
	game_preset_options_t c = game_capabilities();
	const bundle_t *b = bundle_of(serial);
	if (b) assert(ws_load(dir, serial, (uint32_t)strtoul(b->fp, NULL, 16), c.widescreen));
	else ws_unload();
	// config_ws_sync() and the settings note, as in config.c/settings_ui.c.
	bool ok = ws_available() && (c.widescreen == WS_OFFERED || c.widescreen == WS_COSTLY);
	view_t v = { c.fast_forward, c.rewind, c.touch_controls, c.needs_l2r2, c.needs_l3r3, c.needs_right_stick,
		{0}, c.frameskip, game_preset_curated(), ok, ws_console_policy() };
	memcpy(v.binds, c.touch_bindings, sizeof(v.binds));
	return v;
}

static void put(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	assert(f);
	fputs(text, f);
	fclose(f);
}
static int fast_forward_of(const char *serial)
{
	g_new3ds = false;
	identify(serial);
	return game_capabilities().fast_forward;
}

static void precedence_checks(const char *tmp)
{
	char b[512], s[512], p[600];
	snprintf(b, sizeof(b), "%s/builtin", tmp);
	snprintf(s, sizeof(s), "%s/sd", tmp);
	mkdir(b, 0700);
	mkdir(s, 0700);
#define AT(dir, name) (snprintf(p, sizeof(p), "%s/%s", dir, name), p)
	put(AT(b, "VERSION"), "2\n");
	put(AT(b, "a.ini"), "title = A\nversion = 2\nserials = TEST00001, TEST00002\ncurated = yes\n[old]\nfast_forward = no\n");
	put(AT(b, "c.ini"), "title = C\nversion = 1\nserials = TEST00003\n[old]\nfast_forward = no\nbogus = 1\nrewind = maybe\n");
	quiet = true;

	// No SD pack: built-in data; a bad built-in line is ignored, the rest applies.
	game_presets_init(b, s);
	assert(fast_forward_of("TEST00001") == 0 && game_preset_version() == 2 && !strcmp(game_preset_source(), "builtin"));
	assert(fast_forward_of("TEST00003") == 0 && !game_preset_curated());
	assert(fast_forward_of("TEST00009") == 1 && !strcmp(game_preset_source(), "none"));
	assert(game_presets_pack() == 2);

	// SD newer: higher file version wins for the serials it lists.
	put(AT(s, "VERSION"), "3\n");
	put(AT(s, "a.ini"), "title = A\nversion = 3\nserials = TEST00001\ncurated = yes\n");
	game_presets_init(b, s);
	assert(fast_forward_of("TEST00001") == 1 && game_preset_version() == 3 && !strcmp(game_preset_source(), "sd"));
	assert(fast_forward_of("TEST00002") == 0 && !strcmp(game_preset_source(), "builtin"));

	// SD file not newer than the built-in one: built-in wins.
	put(AT(s, "a.ini"), "title = A\nversion = 2\nserials = TEST00001\ncurated = yes\n");
	game_presets_init(b, s);
	assert(fast_forward_of("TEST00001") == 0 && !strcmp(game_preset_source(), "builtin"));

	// Malformed SD file (unknown key): rejected whole, built-in used.
	put(AT(s, "a.ini"), "title = A\nversion = 3\nserials = TEST00001\nfast_forward = yes\nturbo = on\n");
	game_presets_init(b, s);
	assert(fast_forward_of("TEST00001") == 0 && !strcmp(game_preset_source(), "builtin"));
	// SD file newer than its own pack: rejected.
	put(AT(s, "a.ini"), "title = A\nversion = 9\nserials = TEST00001\nfast_forward = yes\n");
	game_presets_init(b, s);
	assert(fast_forward_of("TEST00001") == 0 && !strcmp(game_preset_source(), "builtin"));

	// Missing built-in: an SD-only game is used from the SD pack.
	put(AT(s, "new.ini"), "title = New\nversion = 1\nserials = TEST00004\ncurated = yes\n[old]\nfast_forward = no\n");
	game_presets_init(b, s);
	assert(fast_forward_of("TEST00004") == 0 && game_preset_curated() && !strcmp(game_preset_source(), "sd"));

	// SD pack older than the built-in pack: ignored entirely, even newer files.
	put(AT(s, "VERSION"), "1\n");
	put(AT(s, "a.ini"), "title = A\nversion = 1\nserials = TEST00001\n");
	put(AT(s, "new.ini"), "title = New\nversion = 1\nserials = TEST00004\ncurated = yes\n");
	game_presets_init(b, s);
	assert(fast_forward_of("TEST00004") == 1 && !strcmp(game_preset_source(), "none"));
	assert(fast_forward_of("TEST00001") == 0 && !strcmp(game_preset_source(), "builtin"));
	assert(game_presets_pack() == 2);

	// SD folder without VERSION: ignored.
	remove(AT(s, "VERSION"));
	game_presets_init(b, s);
	assert(fast_forward_of("TEST00004") == 1);

	// A newer SD pack is reported; a serial listed twice keeps one file.
	remove(AT(s, "a.ini"));
	put(AT(s, "VERSION"), "4\n");
	put(AT(s, "x.ini"), "title = X\nversion = 3\nserials = TEST00001\n[new]\nrewind = no\n");
	put(AT(s, "y.ini"), "title = Y\nversion = 3\nserials = TEST00001\n[new]\nrewind = no\n");
	game_presets_init(b, s);
	assert(game_presets_pack() == 4);
	g_new3ds = true;
	identify("TEST00001");
	assert(!game_capabilities().rewind && game_preset_version() == 3);
	quiet = false;
#undef AT
	puts("PASS: version precedence (SD newer, SD older, older SD pack, malformed SD, SD-only, bad built-in key)");
}

int main(int argc, char **argv)
{
	if (argc != 5) {
		fprintf(stderr, "usage: identical <presets dir> <widescreen dir> <v041-ws-models.txt> <tmp>\n");
		return 2;
	}
	FILE *f = fopen(argv[3], "r");
	assert(f);
	while (nbundles < 64 && fscanf(f, "%9s %8s %d %d", bundles[nbundles].serial, bundles[nbundles].fp,
	                               &bundles[nbundles].models[0], &bundles[nbundles].models[1]) == 4)
		nbundles++;
	fclose(f);
	assert(nbundles > 20);

	quiet = true;
	game_presets_init(argv[1], "/nonexistent-sd-presets");
	quiet = false;
	const char *serials[256];
	int n = 0;
	for (size_t i = 0; i < sizeof(legacy_presets) / sizeof(legacy_presets[0]); i++) serials[n++] = legacy_presets[i].serial;
	for (int i = 0; i < nbundles; i++) serials[n++] = bundles[i].serial;
	serials[n++] = "SLUS99999"; // uncurated, no bundle
	serials[n++] = "SCUS94163"; // FFVII: in the library, never curated
	int compared = 0;
	quiet = true;
	for (int i = 0; i < n; i++)
		for (int model = 0; model < 2; model++) {
			view_t a = legacy_view(serials[i], model), b = new_view(argv[2], serials[i], model);
			if (memcmp(&a, &b, sizeof(a))) {
				quiet = false;
				fprintf(stderr, "MISMATCH %s %s\n", serials[i], model ? "new" : "old");
				return 1;
			}
			// Overclock entries are present but inert: New only, never curating.
			game_preset_options_t c = game_capabilities();
			if (c.psxclock) assert(model && !game_preset_curated());
			compared++;
		}
	quiet = false;
	printf("PASS: %d serial/console views identical to v0.4.1 (capabilities, curated, widescreen offer and note)\n", compared);
	int clocks = 0;
	g_new3ds = true;
	static const char *const oc[] = { "SCUS94426", "SCUS94455", "SLUS00707", "SLUS00826" };
	static const int want[] = { 75, 80, 100, 80 };
	quiet = true;
	for (int i = 0; i < 4; i++) {
		identify(oc[i]);
		assert(game_capabilities().psxclock == want[i]);
		g_new3ds = false;
		assert(!game_capabilities().psxclock);
		g_new3ds = true;
		clocks++;
	}
	quiet = false;
	printf("PASS: %d New 3DS psxclock entries parsed (applied only with PSX3D_PS1_OVERCLOCK_PRESETS)\n", clocks);
	precedence_checks(argv[4]);
	return 0;
}
