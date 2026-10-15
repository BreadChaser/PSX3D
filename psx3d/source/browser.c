// Game browser. Reads one folder at a time with readdir() and never stats
// files or asks the card for its free space, so it stays fast on huge
// (1TB+) SD cards and folders with thousands of files.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "psx3d.h"

#define MAX_ENTRIES 4096
#define LINES 13

typedef struct {
	char *name;
	bool dir;
} entry_t;

static entry_t entries[MAX_ENTRIES];
static int nentries, sel, scroll;
static bool truncated;
static char cwd[PATH_LEN];

static const char *const exts[] = {
	".cue", ".chd", ".pbp", ".m3u", ".bin", ".iso", ".img", ".mdf", ".toc", ".cbn", ".exe",
};

static bool is_game(const char *name)
{
	const char *dot = strrchr(name, '.');
	if (!dot)
		return false;
	for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
		if (strcasecmp(dot, exts[i]) == 0)
			return true;
	return false;
}

static int cmp(const void *a, const void *b)
{
	const entry_t *x = a, *y = b;
	if (x->dir != y->dir)
		return x->dir ? -1 : 1;
	return strcasecmp(x->name, y->name);
}

static void free_entries(void)
{
	for (int i = 0; i < nentries; i++)
		free(entries[i].name);
	nentries = 0;
}

// .bin files next to a .cue of the same name are just clutter; hide them
static bool has_cue_sibling(const char *name)
{
	char want[PATH_LEN];
	const char *dot = strrchr(name, '.');
	if (!dot || strcasecmp(dot, ".bin") != 0)
		return false;
	snprintf(want, sizeof(want), "%.*s.cue", (int)(dot - name), name);
	for (int i = 0; i < nentries; i++)
		if (!entries[i].dir && strcasecmp(entries[i].name, want) == 0)
			return true;
	return false;
}

static void load_dir(const char *path)
{
	DIR *d;
	struct dirent *de;

	free_entries();
	truncated = false;
	sel = scroll = 0;
	snprintf(cwd, sizeof(cwd), "%s", path);

	d = opendir(cwd);
	if (!d) {
		log_printf("opendir(%s) failed\n", cwd);
		return;
	}
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		bool dir = de->d_type == DT_DIR;
		if (!dir && !is_game(de->d_name))
			continue;
		if (nentries == MAX_ENTRIES) {
			truncated = true;
			break;
		}
		entries[nentries].name = strdup(de->d_name);
		entries[nentries].dir = dir;
		nentries++;
	}
	closedir(d);

	// drop .bin tracks that belong to a .cue
	int j = 0;
	for (int i = 0; i < nentries; i++) {
		if (!entries[i].dir && has_cue_sibling(entries[i].name)) {
			free(entries[i].name);
			continue;
		}
		entries[j++] = entries[i];
	}
	nentries = j;
	qsort(entries, nentries, sizeof(entries[0]), cmp);
}

static void go_up(void)
{
	char *slash;
	size_t len = strlen(cwd);

	if (len && cwd[len - 1] == '/')
		cwd[--len] = 0;
	slash = strrchr(cwd, '/');
	if (!slash || slash[-1] == ':') {
		load_dir("sdmc:/");
		return;
	}
	*slash = 0;
	char parent[PATH_LEN];
	snprintf(parent, sizeof(parent), "%s", cwd);
	load_dir(parent);
}

static void join(char *out, size_t len, const char *dir, const char *name)
{
	size_t n = strlen(dir);
	snprintf(out, len, "%s%s%s", dir, (n && dir[n - 1] == '/') ? "" : "/", name);
}

static void draw_top(void *ud)
{
	(void)ud;
	ui_rect(0, 0, 400, 20, C2D_Color32(40, 40, 70, 255));
	ui_text(6, 2, 0.5f, C2D_Color32(255, 255, 255, 255), "PSX3D  %s", cwd);

	if (nentries == 0)
		ui_text(10, 40, 0.5f, C2D_Color32(200, 200, 200, 255),
			"No games or folders here.");

	for (int i = 0; i < LINES && scroll + i < nentries; i++) {
		int idx = scroll + i;
		float y = 24 + i * 16;
		if (idx == sel)
			ui_rect(0, y, 400, 16, C2D_Color32(70, 90, 160, 255));
		ui_text(8, y, 0.5f,
			entries[idx].dir ? C2D_Color32(255, 220, 120, 255) : C2D_Color32(255, 255, 255, 255),
			"%s%s", entries[idx].name, entries[idx].dir ? "/" : "");
	}
}

static void draw_bottom(void *ud)
{
	(void)ud;
	u32 white = C2D_Color32(255, 255, 255, 255), grey = C2D_Color32(160, 160, 170, 255);

	ui_text(10, 10, 0.7f, white, "PSX3D v" PSX3D_VERSION);
	ui_text(10, 34, 0.45f, grey, "Based on PCSX-ReARMed (GPL v2)");
	ui_text(10, 70, 0.5f, white, "A: open    B: back    L/R: page");
	ui_text(10, 88, 0.5f, white, "START: exit PSX3D");
	ui_text(10, 120, 0.45f, grey, "BIOS (optional): " PSX3D_BIOS_DIR);
	ui_text(10, 136, 0.45f, grey, __ctr_svchax ? "Dynarec: ON (fast)" :
		"Dynarec: OFF - needs Luma3DS (slow!)");
	if (truncated)
		ui_text(10, 160, 0.45f, C2D_Color32(255, 160, 120, 255),
			"Folder has over %d entries, list cut off", MAX_ENTRIES);
	ui_text(10, 200, 0.45f, grey, "%d item%s", nentries, nentries == 1 ? "" : "s");
}

bool browser_run(char *out_path, size_t out_len)
{
	DIR *test = opendir(g_cfg.last_dir);
	if (test) {
		closedir(test);
		load_dir(g_cfg.last_dir);
	} else {
		load_dir("sdmc:/");
	}

	while (aptMainLoop()) {
		input_poll();
		u32 k = input_keys_down();

		if (k & KEY_START)
			break;
		if (nentries) {
			if (k & (KEY_DOWN)) sel = (sel + 1) % nentries;
			if (k & (KEY_UP)) sel = (sel - 1 + nentries) % nentries;
			if (k & KEY_R) { sel += LINES; if (sel >= nentries) sel = nentries - 1; }
			if (k & KEY_L) { sel -= LINES; if (sel < 0) sel = 0; }
		}
		if (sel < scroll) scroll = sel;
		if (sel >= scroll + LINES) scroll = sel - LINES + 1;

		if ((k & KEY_B))
			go_up();
		else if ((k & KEY_A) && nentries) {
			char path[PATH_LEN];
			join(path, sizeof(path), cwd, entries[sel].name);
			if (entries[sel].dir) {
				load_dir(path);
			} else {
				snprintf(out_path, out_len, "%s", path);
				snprintf(g_cfg.last_dir, sizeof(g_cfg.last_dir), "%s", cwd);
				config_save();
				free_entries();
				return true;
			}
		}

		ui_begin();
				video_present(false, draw_top, draw_bottom, NULL);
	}
	free_entries();
	return false;
}
