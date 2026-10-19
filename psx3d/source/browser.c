// Game browser. Reads one folder at a time with readdir() and never stats
// files or asks the card for its free space, so it stays fast on huge
// (1TB+) SD cards and folders with thousands of files.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "psx3d.h"
#include "devtools.h"

#define MAX_ENTRIES 4096
#define ROWS 7      // visible list rows
#define ROW_H 23

typedef struct {
	char *name;
	bool dir;
} entry_t;

static entry_t entries[MAX_ENTRIES];
static int nentries, sel;   // sel: list row; row 0 is "Use this folder"
static flist_t lst;
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
	sel = 0;
	lst.init = false;
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
/* Bounded truncation is intentional; preserve the validated release code layout. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
	snprintf(out, len, "%s%s%s", dir, (n && dir[n - 1] == '/') ? "" : "/", name);
#pragma GCC diagnostic pop
}

static void select_name(const char *name)
{
	for (int i = 0; i < nentries; i++)
		if (strcmp(entries[i].name, name) == 0) {
			sel = i + 1;
			flist_snap(&lst, sel, nentries + 1, ROWS);
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
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, C2D_Color32(6, 9, 24, 255));
	ui_text_ex(libui_font(), 16, 14, 0.8f, C2D_Color32(235, 238, 255, 255), 0, "PSX3D");
	ui_text_ex(NULL, 16, 46, 0.42f, C2D_Color32(130, 150, 190, 255), 0,
		"Version " PSX3D_VERSION ", based on PCSX-ReARMed (GPL v2)");
	ui_text_ex(NULL, 16, 70, 0.39f, C2D_Color32(110, 120, 160, 255), 0, "Games folder");
	ui_text_ex(NULL, 16, 84, 0.42f, C2D_Color32(170, 185, 220, 255), 0,
		g_cfg.games_dir[0] ? ui_fit(g_cfg.games_dir, .42f, 368) : "None chosen yet");
	if (!__ctr_svchax)
		ui_text_ex(NULL, 16, 108, 0.42f, C2D_Color32(255, 160, 120, 255), 0,
			"Fast CPU mode needs Luma3DS: games will run slowly");
	ui_help_panel(150, "This folder", truncated ? "Over 4096 entries; the list is cut off" : NULL, cwd);
}

static const char *row_label(int i, void *ud)
{
	(void)ud;
	if (!i)
		return "Use this folder for games";
	return ui_fit(entries[i - 1].name, 0.5f, entries[i - 1].dir ? 270 : 250);
}

static const char *row_value(int i, void *ud)
{
	(void)ud;
	if (!i)
		return strcmp(cwd, g_cfg.games_dir) ? NULL : "In use";
	return entries[i - 1].dir ? ">" : "Play";
}

static void draw_bottom(void *ud)
{
	(void)ud;
	u32 muted = C2D_Color32(130, 150, 190, 255);
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, C2D_Color32(6, 9, 24, 255));
	const char *name = strrchr(cwd, '/');
	ui_menu_header("Folders", ui_fit(name && name[1] ? name + 1 : cwd, .42f, 170));
	flist_draw(&lst, nentries + 1, sel, 0, 40, 320, ROW_H, ROWS, row_label, row_value, NULL,
		(const u32[]){0xff3c1ee6, 0xff14cdfa, 0xff78be1e, 0xffe66e28}[sel & 3]);
	if (nentries + 1 > ROWS) {
		float h = ROWS * (float)ROW_H;
		C2D_DrawRectSolid(317, 40 + h * lst.scroll / (nentries + 1), .3f, 2, h * ROWS / (nentries + 1), muted);
	}
	if (msg_frames > 0) {
		msg_frames--;
		ui_text_ex(NULL, 12, 204, .4f, C2D_Color32(255, 210, 120, 255), 0, msg);
	} else if (!nentries)
		ui_text_ex(NULL, 12, 204, .4f, muted, 0, "No games or folders here");
	ui_text_ex(NULL, 160, 222, .39f, muted, C2D_AlignCenter,
		g_cfg.games_dir[0] ? "A open    B up    Y games folder    START library"
		                   : "A open    B up    START quit");
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

	// Back from a game started here: that folder. Otherwise the games folder.
	guess_games_dir();
	if (back_sel[0] && dir_exists(g_cfg.last_dir)) {
		load_dir(g_cfg.last_dir);
		select_name(back_sel);
	} else if (g_cfg.games_dir[0] && dir_exists(g_cfg.games_dir))
		load_dir(g_cfg.games_dir);
	else
		load_dir("sdmc:/");

	while (aptMainLoop()) {
		if (dev_tick(false))
			return false;
		if (dev_ui_take() == DEV_UI_LIBRARY)
			break;   // remote: back to the library
		input_poll();
		u32 k = input_keys_down(), h = input_keys_held();
		int n = nentries + 1, before = sel;

		if (k & KEY_START)
			break;
		if (repeat(k, h, KEY_DOWN, &rep_down)) sel = (sel + 1) % n;
		if (repeat(k, h, KEY_UP, &rep_up)) sel = (sel - 1 + n) % n;
		if (repeat(k, h, KEY_R, &rep_r)) { sel += ROWS; if (sel >= n) sel = n - 1; }
		if (repeat(k, h, KEY_L, &rep_l)) { sel -= ROWS; if (sel < 0) sel = 0; }
		if (k & KEY_TOUCH) {
			touchPosition tp;
			hidTouchRead(&tp);
			int row = flist_row_at(&lst, 40, ROW_H, tp.py);
			if (tp.py >= 40 && tp.py < 40 + ROWS * ROW_H && row >= 0 && row < n) {
				if (row == sel)
					k |= KEY_A;
				sel = row;
			}
		}
		if (sel != before)
			sfx_play(SFX_MOVE);

		if (k & KEY_Y) {
			if (g_cfg.games_dir[0] && dir_exists(g_cfg.games_dir)) {
				load_dir(g_cfg.games_dir);
			} else {
				snprintf(msg, sizeof(msg), "No games folder yet: choose one first");
				msg_frames = 150;
			}
		} else if (k & KEY_B) {
			sfx_play(SFX_BACK);
			if (strcmp(cwd, "sdmc:/"))
				go_up();
			else if (g_cfg.games_dir[0])
				break;   // nothing above the card: back to the library
		} else if ((k & KEY_A) && !sel) {
			// the library loads this folder when the browser closes
			sfx_play(SFX_SELECT);
			snprintf(g_cfg.games_dir, sizeof(g_cfg.games_dir), "%s", cwd);
			config_save();
			break;
		} else if (k & KEY_A) {
			const entry_t *e = &entries[sel - 1];
			char path[PATH_LEN], from[PATH_LEN], name[PATH_LEN];
			join(path, sizeof(path), cwd, e->name);
			snprintf(from, sizeof(from), "%s", cwd);
			snprintf(name, sizeof(name), "%s", e->name);
			sfx_play(SFX_SELECT);
			if (e->dir) {
				load_dir(path);
				// one game per folder (the usual layout): just start it
				char game[PATH_LEN];
				if (single_game(game, sizeof(game)))
					return launch(out_path, out_len, game, from, name);
			} else {
				return launch(out_path, out_len, path, from, name);
			}
		}

		flist_update(&lst, sel, n, ROWS);
		ui_begin();
		video_present_vsync(false, draw_top, draw_bottom, NULL);
	}
	back_sel[0] = 0;
	free_entries();
	return false;
}
