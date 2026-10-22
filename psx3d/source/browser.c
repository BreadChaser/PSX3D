#define _GNU_SOURCE   // strcasestr
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

// First run: find the PS1 games without asking. The usual folders first,
// then any folder on the card or in sdmc:/roms named like PlayStation, then
// sdmc:/roms itself; a folder counts only when the scan finds PS1 discs in it
// (other systems' folders and images are left out, library.c). Returns true
// with games_dir set and the library loaded.
static bool ps1_name(const char *n)
{
	return strcasestr(n, "psx") || strcasestr(n, "ps1") || strcasestr(n, "playstation") || strcasestr(n, "psone");
}

bool library_guess_dir(void)
{
	static const char *const guesses[] = {
		"sdmc:/roms/psx", "sdmc:/roms/ps1", "sdmc:/roms/playstation", "sdmc:/psx", "sdmc:/ps1",
	};
	char (*found)[PATH_LEN] = malloc(24 * PATH_LEN);   // once, at first start
	int n = 0;
	if (!found)
		return false;
	for (size_t i = 0; i < sizeof(guesses) / sizeof(guesses[0]); i++)
		if (dir_exists(guesses[i]))
			snprintf(found[n++], PATH_LEN, "%s", guesses[i]);
	static const char *const parents[] = { "sdmc:/roms", "sdmc:" };
	for (int p = 0; p < 2; p++) {
		DIR *d = opendir(p ? "sdmc:/" : parents[p]);
		struct dirent *de;
		while (d && n < 22 && (de = readdir(d)))
			if (de->d_name[0] != '.' && ps1_name(de->d_name)) {
				char path[PATH_LEN];
				snprintf(path, sizeof(path), "%s/%s", parents[p], de->d_name);
				bool dup = false;
				for (int k = 0; k < n; k++)
					dup |= !strcasecmp(found[k], path);
				if (!dup && dir_exists(path))
					snprintf(found[n++], PATH_LEN, "%s", path);
			}
		if (d)
			closedir(d);
	}
	if (dir_exists("sdmc:/roms"))
		snprintf(found[n++], PATH_LEN, "sdmc:/roms");
	for (int i = 0; i < n; i++)
		if (library_load(found[i], true)) {
			log_printf("library: first run, PS1 games found in %s\n", found[i]);
			snprintf(g_cfg.games_dir, sizeof(g_cfg.games_dir), "%.*s", (int)sizeof(g_cfg.games_dir) - 1, found[i]);
			config_save();
			free(found);
			return true;
		}
	free(found);
	log_printf("library: first run, no PS1 games found (%d folders tried)\n", n);
	library_load(NULL, false);   // nothing listed
	return false;
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
	const theme_t *th = theme();
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, th->menu_bg);
	ui_text_ex(libui_font(), 16, 14, 0.8f, th->header_title, 0, "PSX3D");
	char version[64];
	snprintf(version, sizeof(version), "Version " PSX3D_VERSION " (game presets %d), based on PCSX-ReARMed (GPL v2)",
		game_presets_pack());
	ui_text_ex(NULL, 16, 46, 0.42f, th->muted, 0, version);
	ui_text_ex(NULL, 16, 70, 0.39f, th->header_right, 0, "Games folder");
	ui_text_ex(NULL, 16, 84, 0.42f, th->folder, 0,
		g_cfg.games_dir[0] ? ui_fit(g_cfg.games_dir, .42f, 368) : "None chosen yet");
	if (slow_mode_notice(false))
		ui_text_ex(NULL, 16, 108, 0.42f, th->slow_text, 0,
			ui_fit(slow_mode_notice(true), .42f, 368));
	ui_help_panel(150, "This folder", truncated ? "Over 4096 entries; the list is cut off" : NULL, cwd);
	ui_notice(msg, msg_frames);
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

// B Up (Back at the top of the card), Y the games folder, START Done, A Open
static int browser_bar(ui_button_t *b)
{
	int n = 0;
	bool root = !strcmp(cwd, "sdmc:/");
	if (!root || g_cfg.games_dir[0])
		b[n++] = (ui_button_t){KEY_B, root ? "Back" : "Up", root ? UI_ICON_BACK : UI_ICON_UP, false};
	if (g_cfg.games_dir[0] && strcmp(cwd, g_cfg.games_dir))
		b[n++] = (ui_button_t){KEY_Y, "Games", UI_ICON_FOLDER, false};
	b[n++] = (ui_button_t){KEY_START, "Done", UI_ICON_EXIT, false};
	b[n++] = !sel ? (ui_button_t){KEY_A, "Use", UI_ICON_CHECK, false}
		: sel <= nentries && entries[sel - 1].dir ? (ui_button_t){KEY_A, "Open", UI_ICON_OPEN, false}
		: (ui_button_t){KEY_A, "Play", UI_ICON_PLAY, false};
	return n;
}

static void draw_bottom(void *ud)
{
	(void)ud;
	const theme_t *th = theme();
	u32 muted = th->muted;
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, th->menu_bg);
	const char *name = strrchr(cwd, '/');
	ui_menu_header("Folders", ui_fit(name && name[1] ? name + 1 : cwd, .42f, 170));
	flist_draw(&lst, nentries + 1, sel, 0, 40, 320, ROW_H, ROWS, row_label, row_value, NULL,
		th->accent[sel & 3]);
	if (nentries + 1 > ROWS) {
		float h = ROWS * (float)ROW_H;
		C2D_DrawRectSolid(317, 40 + h * lst.scroll / (nentries + 1), .3f, 2, h * ROWS / (nentries + 1), muted);
	}
	if (msg_frames > 0)
		msg_frames--;
	if (!nentries)
		ui_text_ex(NULL, 12, 72, .4f, muted, 0, "No games or folders here");
	ui_button_t bar[4];
	ui_bar_draw(bar, browser_bar(bar));
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
		ui_button_t bar[4];
		u32 k = ui_header_back(ui_bar_input(bar, browser_bar(bar), input_keys_down())), h = input_keys_held();
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
