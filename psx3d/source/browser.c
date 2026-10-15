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
static char msg[64];
static int msg_frames;

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

static void join(char *out, size_t len, const char *dir, const char *name)
{
	size_t n = strlen(dir);
	snprintf(out, len, "%s%s%s", dir, (n && dir[n - 1] == '/') ? "" : "/", name);
}

static void select_name(const char *name)
{
	for (int i = 0; i < nentries; i++)
		if (strcmp(entries[i].name, name) == 0) {
			sel = i;
			scroll = sel > LINES / 2 ? sel - LINES / 2 : 0;
			return;
		}
}

// up one folder, keeping the cursor on the folder we came from
static void go_up(void)
{
	char *slash, child[PATH_LEN];
	size_t len = strlen(cwd);

	if (len && cwd[len - 1] == '/')
		cwd[--len] = 0;
	slash = strrchr(cwd, '/');
	if (!slash)
		return;
	snprintf(child, sizeof(child), "%s", slash + 1);
	if (slash[-1] == ':') {
		load_dir("sdmc:/");
	} else {
		*slash = 0;
		char parent[PATH_LEN];
		snprintf(parent, sizeof(parent), "%s", cwd);
		load_dir(parent);
	}
	select_name(child);
}

static bool dir_exists(const char *path)
{
	DIR *d = opendir(path);
	if (d)
		closedir(d);
	return d != NULL;
}

// first run: guess where the PS1 games are
static void guess_games_dir(void)
{
	static const char *const guesses[] = {
		"sdmc:/roms/psx", "sdmc:/roms/PSX", "sdmc:/roms/ps1", "sdmc:/roms/PS1",
		"sdmc:/roms/playstation", "sdmc:/psx", "sdmc:/PSX", "sdmc:/roms",
	};
	if (g_cfg.games_dir[0])
		return;
	for (size_t i = 0; i < sizeof(guesses) / sizeof(guesses[0]); i++)
		if (dir_exists(guesses[i])) {
			snprintf(g_cfg.games_dir, sizeof(g_cfg.games_dir), "%s", guesses[i]);
			return;
		}
}

// a folder holding exactly one game and nothing else: that game
static bool single_game(char *out, size_t len)
{
	if (nentries != 1 || entries[0].dir)
		return false;
	join(out, len, cwd, entries[0].name);
	return true;
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
	ui_text(10, 62, 0.5f, white, "A: open    B: back    L/R: page");
	ui_text(10, 80, 0.5f, white, "Y: games folder    X: set games folder");
	ui_text(10, 98, 0.5f, white, "START: exit PSX3D");
	if (msg_frames > 0) {
		msg_frames--;
		ui_text(10, 180, 0.5f, C2D_Color32(140, 220, 140, 255), "%s", msg);
	}
	ui_text(10, 120, 0.45f, grey, "BIOS (optional): " PSX3D_BIOS_DIR);
	ui_text(10, 136, 0.45f, grey, __ctr_svchax ? "Dynarec: ON (fast)" :
		"Dynarec: OFF - needs Luma3DS (slow!)");
	if (truncated)
		ui_text(10, 160, 0.45f, C2D_Color32(255, 160, 120, 255),
			"Folder has over %d entries, list cut off", MAX_ENTRIES);
	ui_text(10, 200, 0.45f, grey, "%d item%s", nentries, nentries == 1 ? "" : "s");
}

// held-button repeat: fires on press, then after 18 frames every 3 frames
static bool repeat(u32 down, u32 held, u32 key, int *counter)
{
	if (down & key) {
		*counter = 0;
		return true;
	}
	if (!(held & key))
		return false;
	++*counter;
	return *counter > 18 && (*counter - 18) % 3 == 0;
}

// remember where to come back to: the folder and the entry that was picked
static char back_sel[PATH_LEN];

static bool launch(char *out_path, size_t out_len, const char *path,
	const char *from_dir, const char *from_entry)
{
	snprintf(out_path, out_len, "%s", path);
	snprintf(g_cfg.last_dir, sizeof(g_cfg.last_dir), "%s", from_dir);
	snprintf(back_sel, sizeof(back_sel), "%s", from_entry);
	config_save();
	free_entries();
	return true;
}

bool browser_run(char *out_path, size_t out_len)
{
	static int rep_down, rep_up, rep_l, rep_r;

	guess_games_dir();
	if (dir_exists(g_cfg.last_dir)) {
		load_dir(g_cfg.last_dir);
		if (back_sel[0])
			select_name(back_sel);
	}
	else if (g_cfg.games_dir[0] && dir_exists(g_cfg.games_dir))
		load_dir(g_cfg.games_dir);
	else
		load_dir("sdmc:/");

	while (aptMainLoop()) {
		input_poll();
		u32 k = input_keys_down(), h = input_keys_held();

		if (k & KEY_START)
			break;
		if (nentries) {
			if (repeat(k, h, KEY_DOWN, &rep_down)) sel = (sel + 1) % nentries;
			if (repeat(k, h, KEY_UP, &rep_up)) sel = (sel - 1 + nentries) % nentries;
			if (repeat(k, h, KEY_R, &rep_r)) { sel += LINES; if (sel >= nentries) sel = nentries - 1; }
			if (repeat(k, h, KEY_L, &rep_l)) { sel -= LINES; if (sel < 0) sel = 0; }
		}
		if (sel < scroll) scroll = sel;
		if (sel >= scroll + LINES) scroll = sel - LINES + 1;

		if (k & KEY_Y) {
			if (g_cfg.games_dir[0] && dir_exists(g_cfg.games_dir)) {
				load_dir(g_cfg.games_dir);
			} else {
				snprintf(msg, sizeof(msg), "No games folder yet: press X in it");
				msg_frames = 150;
			}
		} else if (k & KEY_X) {
			snprintf(g_cfg.games_dir, sizeof(g_cfg.games_dir), "%s", cwd);
			config_save();
			snprintf(msg, sizeof(msg), "Games folder set");
			msg_frames = 120;
		} else if (k & KEY_B) {
			go_up();
		} else if ((k & KEY_A) && nentries) {
			char path[PATH_LEN], from[PATH_LEN], name[PATH_LEN];
			join(path, sizeof(path), cwd, entries[sel].name);
			snprintf(from, sizeof(from), "%s", cwd);
			snprintf(name, sizeof(name), "%s", entries[sel].name);
			if (entries[sel].dir) {
				load_dir(path);
				// one game per folder (the usual layout): just start it
				char game[PATH_LEN];
				if (single_game(game, sizeof(game)))
					return launch(out_path, out_len, game, from, name);
			} else {
				return launch(out_path, out_len, path, from, name);
			}
		}

		ui_begin();
		video_present_vsync(false, draw_top, draw_bottom, NULL);
	}
	free_entries();
	return false;
}
