// Save state browser. Every state keeps a screenshot of the moment it was
// saved (<game>.stN.thm next to <game>.stN). The bottom screen shows the ten
// slots as a grid of thumbnails; the top screen shows the selected one large
// with the time it was saved. Loading has two more pages (L/R): the save
// backups' ring of automatic saves (same grid) and the memory card copies
// (a list), see history.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include "psx3d.h"
#include "devtools.h"
#include "safefile.h"
#include "history.h"
#include "statefile.h"
#include <unistd.h>

#define SLOTS 10
#define TW 256          // thumbnail size, 4:3
#define TH 192
#define TEX 256         // texture is TEX x TEX
#define THM_MAGIC 0x48543350   // "P3TH"

typedef struct {
	u32 magic;
	u16 w, h;
	u32 saved;          // seconds since 1970, 3DS clock
} thm_hdr_t;

typedef struct {
	bool used;          // a state file exists
	bool thumb;         // ... and has a screenshot
	u32 saved;
	C3D_Tex tex;
	bool tex_ok;
	float lift, lift_v;
} slot_t;

#define CARD_ROWS 6
#define CARD_COPIES (2 * (1 + HIST_CARD_DAYS + HIST_CARD_PINS))

typedef struct {
	hist_entry_t e;
	int card;            // cards_backup_group() index
	const char *label;
} card_copy_t;

typedef struct {
	bool saving;
	const char *title;
	int page;            // STATES_PAGE_*
	slot_t slot[SLOTS];
	slot_t ring[SLOTS];  // the ring page: the newest automatic saves
	hist_entry_t ring_e[SLOTS];
	int ring_n, ring_sel;
	card_copy_t *cards;  // CARD_COPIES, on the heap while the browser is open
	int cards_n, card_sel, card_top, card_saves;   // saves on the selected copy
	C3D_Tex now;        // the paused frame: preview for saving to an empty slot
	bool now_ok;
	int sel;
	bool confirm;       // waiting for a second A (overwrite) or X (delete)
	bool confirm_delete;
	float t;
} browser_t;

#define accent (theme()->accent)

// ---------------------------------------------------------------- files

// slot STATE_AUTO is the auto-save made on exit (".sta")
static void slot_path(char *out, size_t len, const char *prefix, int slot, bool thm)
{
	if (slot == STATE_AUTO)
		snprintf(out, len, "%s.sta%s", prefix, thm ? ".thm" : "");
	else
		snprintf(out, len, "%s.st%d%s", prefix, slot, thm ? ".thm" : "");
}

// screenshot of the paused game (needs the GPU), NULL without a picture
u16 *state_thumb_take(void)
{
	u16 *px = malloc(TW * TH * 2);
	if (px && !video_snapshot(px, TW, TH)) {
		free(px);
		px = NULL;
	}
	return px;
}

#define THM_SIZE (sizeof(thm_hdr_t) + TW * TH * 2)

static bool thm_head_ok(const void *head)
{
	thm_hdr_t h;
	memcpy(&h, head, sizeof(h));
	return h.magic == THM_MAGIC && h.w == TW && h.h == TH;
}
static const safe_format_t thm_fmt = { THM_SIZE, sizeof(thm_hdr_t), thm_head_ok };

// a thumbnail file, crash-safe with the previous one as backup
bool state_thumb_save_file(const char *path, const u16 *px)
{
	u8 *file = malloc(THM_SIZE);
	if (!file)
		return false;
	thm_hdr_t h = { THM_MAGIC, TW, TH, (u32)time(NULL) };
	memcpy(file, &h, sizeof(h));
	memcpy(file + sizeof(h), px, TW * TH * 2);
	bool ok = safe_write(path, &thm_fmt, file, true);
	free(file);
	return ok;
}

// ... saved next to state <slot>
bool state_thumb_save(const char *prefix, int slot, const u16 *px)
{
	char path[PATH_LEN];
	slot_path(path, sizeof(path), prefix, slot, true);
	return state_thumb_save_file(path, px);
}

// thumbnail of <slot> into px (TW x TH); skip_current: only its backup (or
// an unfinished copy)
static bool thumb_read(const char *prefix, int slot, u16 *px, thm_hdr_t *h, bool skip_current)
{
	char path[PATH_LEN];
	slot_path(path, sizeof(path), prefix, slot, true);
	u8 *file = safe_load(path, &thm_fmt, skip_current ? 1u << SAFE_CURRENT : 0, NULL);
	if (!file)
		return false;
	memcpy(h, file, sizeof(*h));
	memcpy(px, file + sizeof(*h), TW * TH * 2);
	free(file);
	return true;
}

// the state was loaded from its backup: bring the matching thumbnail back too
void state_thumb_restore(const char *prefix, int slot)
{
	char path[PATH_LEN];
	safe_info_t info;
	slot_path(path, sizeof(path), prefix, slot, true);
	u8 *file = safe_load(path, &thm_fmt, 1u << SAFE_CURRENT, &info);
	if (file)
		safe_repair(path, &thm_fmt, file, info.from);
	free(file);
}

// saved time of a state: its file, else its backup, else a good unfinished
// copy (a first save cut off before it was put in place)
static bool state_stat(const char *prefix, int slot, u32 *saved)
{
	char path[PATH_LEN];
	safe_format_t fmt = emu_state_format();
	time_t t;
	slot_path(path, sizeof(path), prefix, slot, false);
	if (!safe_stat(path, &fmt, &t))
		return false;
	*saved = (u32)t;
	return true;
}

bool state_thumb_write(const char *prefix, int slot)
{
	u16 *px = state_thumb_take();
	bool ok = px && state_thumb_save(prefix, slot, px);
	free(px);
	return ok;
}

// RGB565 image (TW x TH) into a TEX x TEX texture: 8x8 tiles, Morton order
// inside each tile
static void upload(C3D_Tex *t, const u16 *img)
{
	u16 *dst = t->data;
	memset(dst, 0, t->size);
	for (u32 y = 0; y < TH; y++) {
		u32 ty = y;
		for (u32 x = 0; x < TW; x++) {
			u32 i = (((ty >> 3) * (TEX >> 3) + (x >> 3)) << 6) |
				(x & 1) | ((ty & 1) << 1) | ((x & 2) << 1) | ((ty & 2) << 2) | ((x & 4) << 2) | ((ty & 4) << 3);
			dst[i] = img[y * TW + x];
		}
	}
	C3D_TexFlush(t);
	C3D_TexSetFilter(t, GPU_LINEAR, GPU_LINEAR);
}

static bool make_tex(C3D_Tex *t, const u16 *img)
{
	if (!C3D_TexInit(t, TEX, TEX, GPU_RGB565))
		return false;
	upload(t, img);
	return true;
}

static void scan(browser_t *b, const char *prefix)
{
	u16 *px = malloc(TW * TH * 2);

	for (int i = 0; i < SLOTS; i++) {
		slot_t *s = &b->slot[i];
		s->used = state_stat(prefix, i, &s->saved);
		if (!s->used || !px)
			continue;
		thm_hdr_t h;
		if (thumb_read(prefix, i, px, &h, false) && make_tex(&s->tex, px)) {
			s->tex_ok = s->thumb = true;
			s->saved = h.saved;
		}
	}
	if (px && video_snapshot(px, TW, TH))
		b->now_ok = make_tex(&b->now, px);
	if (!b->saving) {
		// the ring: <backups>/<game>/, the game being the prefix's file name
		const char *game = strrchr(prefix, '/');
		game = game ? game + 1 : prefix;
		b->ring_n = history_list(game, false, b->ring_e, SLOTS);
		for (int i = 0; i < b->ring_n; i++) {
			slot_t *s = &b->ring[i];
			char thm[620];
			s->used = true;
			s->saved = (u32)b->ring_e[i].when;
			snprintf(thm, sizeof(thm), "%s.thm", b->ring_e[i].path);
			u8 *file = px && b->ring_e[i].thumb ? safe_load(thm, &thm_fmt, 0, NULL) : NULL;
			if (file) {
				memcpy(px, file + sizeof(thm_hdr_t), TW * TH * 2);
				s->tex_ok = s->thumb = make_tex(&s->tex, px);
				free(file);
			}
		}
		// the memory cards' copies, newest first per card
		char group[PATH_LEN], ext[16];
		const char *label;
		hist_entry_t *list = malloc(CARD_COPIES * sizeof(*list));
		b->cards = malloc(CARD_COPIES * sizeof(*b->cards));
		for (int c = 0; list && b->cards && cards_backup_group(c, group, sizeof(group), ext, sizeof(ext), &label); c++) {
			int n = history_list(group, true, list, CARD_COPIES);
			for (int i = 0; i < n && b->cards_n < CARD_COPIES; i++)
				if (!strcmp(list[i].ext, ext))
					b->cards[b->cards_n++] = (card_copy_t){ list[i], c, label };
		}
		free(list);
	}
	free(px);
}

static void release(browser_t *b)
{
	for (int i = 0; i < SLOTS; i++) {
		if (b->slot[i].tex_ok)
			C3D_TexDelete(&b->slot[i].tex);
		if (b->ring[i].tex_ok)
			C3D_TexDelete(&b->ring[i].tex);
	}
	if (b->now_ok)
		C3D_TexDelete(&b->now);
	free(b->cards);
	b->cards = NULL;
}

// ---------------------------------------------------------------- backups

static const char *ring_tag(hist_kind_t k)
{
	static const char *const tag[] = { [HIST_TIMER] = "Timer", [HIST_EXIT] = "Exit", [HIST_LOAD] = "Load",
		[HIST_SAVE] = "Save", [HIST_RESET] = "Reset", [HIST_START] = "Start", [HIST_DELETE] = "Delete",
		[HIST_RESTORE] = "Undo" };
	return k <= HIST_RESTORE ? tag[k] : "";
}

// why an automatic save exists, in words
static void ring_why(char *out, size_t len, const hist_entry_t *e)
{
	int slot = e->ext[0] && e->ext[1] == 's' && e->ext[2] == 't' && e->ext[3] >= '0' && e->ext[3] <= '9' ? e->ext[3] - '0' : -1;
	switch (e->kind) {
	case HIST_TIMER: snprintf(out, len, "Auto-saved while you played"); break;
	case HIST_EXIT: snprintf(out, len, "Taken when you left the game"); break;
	case HIST_LOAD: snprintf(out, len, "Before you loaded a save"); break;
	case HIST_SAVE: snprintf(out, len, "Slot %d before you saved over it", slot); break;
	case HIST_RESET: snprintf(out, len, "Before the game was reset"); break;
	case HIST_START: snprintf(out, len, "Your old spot when you chose Start game"); break;
	case HIST_DELETE: snprintf(out, len, "Slot %d before you deleted it", slot); break;
	case HIST_RESTORE: snprintf(out, len, "Before you went back to an older save"); break;
	default: snprintf(out, len, "-"); break;
	}
}

static const char *card_kind(hist_kind_t k)
{
	return k == HIST_CARD_SESSION ? "When you started" : k == HIST_CARD_DAY ? "First of the day" : "Kept for you";
}

// "12 min ago", "3 h ago", else the date
static void age_text(char *out, size_t len, u32 saved)
{
	time_t now = time(NULL);
	if (saved && (time_t)saved <= now && now - saved < 60)
		snprintf(out, len, "just now");
	else if (saved && (time_t)saved <= now && now - saved < 3600)
		snprintf(out, len, "%d min ago", (int)((now - saved) / 60));
	else if (saved && (time_t)saved <= now && now - saved < 12 * 3600)
		snprintf(out, len, "%d h ago", (int)((now - saved) / 3600));
	else {
		time_t tt = saved;
		struct tm *tm = gmtime(&tt);   // the 3DS clock is local time already
		if (!saved || !tm)
			snprintf(out, len, "-");
		else
			strftime(out, len, "%b %d  %H:%M", tm);
	}
}

static void card_select(browser_t *b)
{
	b->card_saves = -1;
	if (b->card_sel >= b->cards_n)
		return;
	FILE *f = fopen(b->cards[b->card_sel].e.path, "rb");
	u8 *buf = malloc(16 * 128);
	if (f && buf && fread(buf, 1, 16 * 128, f) == 16 * 128)
		b->card_saves = card_saves(buf, 16 * 128);
	if (f)
		fclose(f);
	free(buf);
}

// X X on a slot: it goes into the ring, so Undo in the pause menu brings it back
states_deleted_t states_deleted;
static bool slot_delete(const char *prefix, int slot)
{
	char path[PATH_LEN], thm[PATH_LEN], ext[8];
	const char *game = strrchr(prefix, '/');
	game = game ? game + 1 : prefix;
	slot_path(path, sizeof(path), prefix, slot, false);
	slot_path(thm, sizeof(thm), prefix, slot, true);
	snprintf(ext, sizeof(ext), ".st%d", slot);
	char bak[PATH_LEN + 8], tbak[PATH_LEN + 8];
	snprintf(bak, sizeof(bak), "%s.bak", path);
	snprintf(tbak, sizeof(tbak), "%s.bak", thm);
	// the slot's file, or its backup if that is all there is
	bool cur = access(path, F_OK) == 0, old = !cur && access(bak, F_OK) == 0;
	hist_entry_t e;
	unsigned reason, saved = 0, id;
	if (cur || old)
		state_file_info(cur ? path : bak, &reason, &saved, &id);
	if ((cur || old) && !history_adopt(game, HIST_DELETE, ext, cur ? path : bak, cur ? thm : tbak, saved, &e)) {
		emu_osd_alert("Could not delete: the save could not be moved to the backups");
		return false;
	}
	if (cur || old)
		states_deleted = (states_deleted_t){ true, slot, e };
	safe_remove(path);   // its backup and temp files
	safe_remove(thm);
	return true;
}

// ---------------------------------------------------------------- drawing

static void draw_tex(C3D_Tex *t, float x, float y, float w, float h, float z)
{
	static Tex3DS_SubTexture sub = { TW, TH, 0.0f, 1.0f, TW / (float)TEX, 1.0f - TH / (float)TEX };
	C2D_Image img = { t, &sub };
	C2D_DrawImageAt(img, x, y, z, NULL, w / TW, h / TH);
}

static void when_text(char *out, size_t len, u32 saved)
{
	time_t tt = saved;
	struct tm *tm = gmtime(&tt);   // the 3DS clock is local time already
	if (!saved || !tm)
		snprintf(out, len, "-");
	else
		strftime(out, len, "%b %d %Y  %H:%M", tm);
}

static void draw_preview(C3D_Tex *t, int sel, const char *empty, const char *line)
{
	C2D_Font font = libui_font();
	u32 white = C2D_Color32(240, 242, 255, 255), dim = C2D_Color32(150, 160, 200, 255);
	const float pw = 264, ph = 198, px = (400 - pw) / 2, py = 10;
	float dx = ui_depth(-0.6f);
	C2D_DrawRectSolid(px - 3 + dx, py - 3, 0.62f, pw + 6, ph + 6, accent[sel & 3]);
	C2D_DrawRectSolid(px + dx, py, 0.63f, pw, ph, C2D_Color32(10, 12, 30, 255));
	if (t)
		draw_tex(t, px + dx, py, pw, ph, 0.64f);
	else
		ui_text_ex(font, 200 + dx, py + ph / 2 - 10, 0.6f, dim, C2D_AlignCenter, empty);
	ui_text_ex(NULL, 200 + dx, 215, 0.5f, white, C2D_AlignCenter, line);
}

static void draw_top(void *ud)
{
	browser_t *b = ud;
	C2D_DrawRectSolid(0, 0, 0.6f, 400, 240, C2D_Color32(4, 6, 18, 225));
	char line[128], when[40];
	if (b->page == STATES_PAGE_RING) {
		slot_t *s = &b->ring[b->ring_sel];
		if (b->ring_sel >= b->ring_n) {
			draw_preview(NULL, b->ring_sel, "Nothing here yet", "Automatic saves appear here");
			return;
		}
		char why[64];
		ring_why(why, sizeof(why), &b->ring_e[b->ring_sel]);
		age_text(when, sizeof(when), s->saved);
		snprintf(line, sizeof(line), "%s  -  %s", why, when);
		draw_preview(s->tex_ok ? &s->tex : NULL, b->ring_sel, "No preview", ui_fit(line, 0.5f, 390));
		return;
	}
	if (b->page == STATES_PAGE_CARDS) {
		C2D_Font font = libui_font();
		u32 white = C2D_Color32(240, 242, 255, 255), dim = C2D_Color32(150, 160, 200, 255);
		float dx = ui_depth(-0.6f);
		if (b->card_sel >= b->cards_n) {
			ui_text_ex(font, 200 + dx, 100, 0.6f, dim, C2D_AlignCenter, "No memory card copies yet");
			return;
		}
		card_copy_t *c = &b->cards[b->card_sel];
		age_text(when, sizeof(when), (u32)c->e.when);
		ui_text_ex(font, 200 + dx, 50, 0.7f, white, C2D_AlignCenter, c->label);
		snprintf(line, sizeof(line), "%s  -  %s", card_kind(c->e.kind), when);
		ui_text_ex(NULL, 200 + dx, 90, 0.55f, white, C2D_AlignCenter, line);
		int now = cards_saves_now(c->card);
		if (b->card_saves >= 0)
			snprintf(line, sizeof(line), "%d save%s on this copy, %d on the card now", b->card_saves,
				b->card_saves == 1 ? "" : "s", now);
		else
			snprintf(line, sizeof(line), "This copy could not be read");
		ui_text_ex(NULL, 200 + dx, 120, 0.5f, dim, C2D_AlignCenter, line);
		ui_text_ex(NULL, 200 + dx, 170, 0.45f, dim, C2D_AlignCenter,
			"Putting a card back restarts the game. The card as it is now is kept.");
		return;
	}
	slot_t *s = &b->slot[b->sel];
	when_text(when, sizeof(when), s->saved);
	if (b->saving && !s->used)
		snprintf(line, sizeof(line), "Slot %d  -  new save", b->sel);
	else if (s->used && !s->saved)   // copied in without a date
		snprintf(line, sizeof(line), "Slot %d", b->sel);
	else if (s->used)
		snprintf(line, sizeof(line), "Slot %d  -  %s", b->sel, when);
	else
		snprintf(line, sizeof(line), "Slot %d  -  empty", b->sel);
	// for saving to an empty slot: the frame that would be saved
	C3D_Tex *t = s->tex_ok ? &s->tex : (b->saving && b->now_ok ? &b->now : NULL);
	draw_preview(t, b->sel, s->used ? "No preview" : "Empty", line);
}

// the 5 x 2 grid of slots or automatic saves
static void draw_grid(browser_t *b, slot_t *slots, int sel, bool ring)
{
	u32 white = C2D_Color32(235, 238, 255, 255), dim = C2D_Color32(110, 120, 160, 255);
	const float tw = 56, th = 42, gap = 6, x0 = (320 - (5 * tw + 4 * gap)) / 2, y0 = 42, row = 62;
	for (int i = 0; i < SLOTS; i++) {
		slot_t *s = &slots[i];
		float lift = s->lift, bob = sinf(b->t * 1.7f + i * 0.8f) * (0.4f + 0.8f * lift);
		float sc = 1.0f + 0.14f * lift;
		float w = tw * sc, h = th * sc;
		float cx = x0 + (i % 5) * (tw + gap) + tw / 2, cy = y0 + (i / 5) * row + th / 2 - 3 * lift + bob;
		float x = cx - w / 2, y = cy - h / 2, z = 0.3f + 0.05f * lift;
		bool is_sel = i == sel;

		if (is_sel) {
			C2D_DrawRectSolid(x - 5, y - 5, z, w + 10, h + 10, (accent[i & 3] & 0x00ffffff) | 0x40000000);
			C2D_DrawRectSolid(x - 2, y - 2, z + 0.01f, w + 4, h + 4, accent[i & 3]);
		}
		C2D_DrawRectSolid(x, y, z + 0.02f, w, h, C2D_Color32(18, 22, 48, 255));
		if (s->tex_ok) {
			draw_tex(&s->tex, x, y, w, h, z + 0.03f);
			if (!is_sel)
				C2D_DrawRectSolid(x, y, z + 0.04f, w, h, C2D_Color32(6, 9, 24, 90));
		} else {
			ui_text_ex(NULL, cx, cy - 8, 0.45f, dim, C2D_AlignCenter,
				s->used ? "?" : (b->saving ? "+" : "-"));
		}
		char n[12];
		if (ring)
			snprintf(n, sizeof(n), "%s", i < b->ring_n ? ring_tag(b->ring_e[i].kind) : "");
		else
			snprintf(n, sizeof(n), "%d", i);
		ui_text_ex(NULL, cx, y + h + 2, 0.4f, is_sel ? white : dim, C2D_AlignCenter, n);
	}
}

#define STATES_BAR_MAX 5

// B Back; loading: R next page. Slots: X Delete (a used slot), A Save / Load
// (nothing to load: no A). Auto-saves: A Load. Cards: A Restore.
static int states_bar(const browser_t *b, ui_button_t *out)
{
	int n = 0;
	out[n++] = (ui_button_t){KEY_B, b->confirm ? "Cancel" : "Back", b->confirm ? UI_ICON_CROSS : UI_ICON_BACK, false};
	if (!b->saving && !b->confirm) {   // the pages around this one, by name
		static const char *const page[] = { "Slots", "Auto-saves", "Cards" };
		out[n++] = (ui_button_t){KEY_L, page[(b->page + 2) % 3], UI_ICON_LEFT, false};
		out[n++] = (ui_button_t){KEY_R, page[(b->page + 1) % 3], UI_ICON_RIGHT, false};
	}
	if (b->page == STATES_PAGE_RING) {
		if (b->ring_sel < b->ring_n)
			out[n++] = (ui_button_t){KEY_A, "Load", UI_ICON_LOAD, false};
	} else if (b->page == STATES_PAGE_CARDS) {
		if (b->cards_n)
			out[n++] = (ui_button_t){KEY_A, "Restore", UI_ICON_LOAD, false};
	} else {
		const slot_t *s = &b->slot[b->sel];
		if (s->used)
			out[n++] = (ui_button_t){KEY_X, "Delete", UI_ICON_TRASH, false};
		if (b->saving || s->used)
			out[n++] = (ui_button_t){KEY_A, b->saving ? "Save" : "Load", b->saving ? UI_ICON_SAVE : UI_ICON_LOAD, false};
	}
	return n;
}

static void draw_bottom(void *ud)
{
	browser_t *b = ud;
	C2D_Font font = libui_font();
	u32 white = C2D_Color32(235, 238, 255, 255), dim = C2D_Color32(110, 120, 160, 255);

	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, theme()->menu_bg);
	ui_text_ex(font, 10, 6, 0.5f, white, 0, b->saving ? "Save state" : "Load state");
	if (b->saving)
		ui_text_ex(NULL, 310, 9, 0.42f, dim, C2D_AlignRight, b->title);
	else {   // the pages, L/R
		static const char *const page[] = { "Slots", "Auto-saves", "Cards" };
		float x = 312;
		for (int p = 2; p >= 0; p--) {
			ui_text_ex(NULL, x, 9, 0.42f, p == b->page ? white : dim, C2D_AlignRight, page[p]);
			x -= p == 2 ? 46 : 64;
		}
	}
	for (int i = 0; i < 4; i++)
		C2D_DrawRectSolid(10 + i * 18, 26, 0.2f, 16, 2, accent[i]);

	char when[40], line[96];
	if (b->page == STATES_PAGE_RING) {
		draw_grid(b, b->ring, b->ring_sel, true);
		if (b->ring_sel < b->ring_n) {
			age_text(when, sizeof(when), b->ring[b->ring_sel].saved);
			snprintf(line, sizeof(line), "%s  -  %s", ring_tag(b->ring_e[b->ring_sel].kind), when);
		} else
			snprintf(line, sizeof(line), "The last %d automatic saves are kept here", SLOTS);
		ui_text_ex(NULL, 160, 180, 0.5f, white, C2D_AlignCenter, line);
	} else if (b->page == STATES_PAGE_CARDS) {
		for (int r = 0; r < CARD_ROWS && b->card_top + r < b->cards_n; r++) {
			int i = b->card_top + r;
			card_copy_t *c = &b->cards[i];
			float y = 40 + r * 22;
			if (i == b->card_sel)
				C2D_DrawRectSolid(6, y - 2, 0.2f, 308, 20, C2D_Color32(30, 40, 90, 255));
			age_text(when, sizeof(when), (u32)c->e.when);
			snprintf(line, sizeof(line), "Card %s  -  %s", c->label + 12 /* "Memory card N" */, card_kind(c->e.kind));
			ui_text_ex(NULL, 12, y, 0.45f, i == b->card_sel ? white : dim, 0, line);
			ui_text_ex(NULL, 308, y, 0.45f, i == b->card_sel ? white : dim, C2D_AlignRight, when);
		}
		if (!b->cards_n)
			ui_text_ex(NULL, 160, 90, 0.5f, dim, C2D_AlignCenter, "No memory card copies yet");
	} else {
		draw_grid(b, b->slot, b->sel, false);
		slot_t *s = &b->slot[b->sel];
		when_text(when, sizeof(when), s->saved);
		if (b->confirm)
			snprintf(line, sizeof(line), b->confirm_delete ? "Delete slot %d? Press X again" :
				"Save over slot %d? Press A again", b->sel);
		else if (s->used && !s->saved)
			snprintf(line, sizeof(line), "Saved state (no date)");
		else if (s->used)
			snprintf(line, sizeof(line), "Saved %s", when);
		else
			snprintf(line, sizeof(line), b->saving ? "Empty slot" : "Nothing saved here yet");
		ui_text_ex(NULL, 160, 180, 0.5f, b->confirm ? theme()->message : white, C2D_AlignCenter, line);
		if (b->confirm)   // where the old save goes
			ui_text_ex(NULL, 160, 196, 0.42f, dim, C2D_AlignCenter, "The old save goes to Auto-saves first");
	}

	ui_button_t bar[STATES_BAR_MAX];
	ui_bar_draw(bar, states_bar(b, bar));
}

// ---------------------------------------------------------------- run

static void lift_update(browser_t *b, slot_t *slots, int sel)
{
	for (int i = 0; i < SLOTS; i++) {
		slot_t *si = &slots[i];
		float target = i == sel ? 1.0f : 0.0f;
		si->lift_v += ((target - si->lift) * 300.0f - si->lift_v * 22.0f) / 60.0f;
		si->lift += si->lift_v / 60.0f;
	}
	(void)b;
}

// grid navigation and touch; true: the selected cell was picked
static bool grid_input(u32 k, int *sel)
{
	bool pick = k & KEY_A;
	if (k & KEY_RIGHT) *sel = (*sel + 1) % SLOTS;
	if (k & KEY_LEFT) *sel = (*sel + SLOTS - 1) % SLOTS;
	if (k & (KEY_DOWN | KEY_UP)) *sel = (*sel + 5) % SLOTS;
	if (k & KEY_TOUCH) {
		touchPosition tp;
		hidTouchRead(&tp);
		const float tw = 56, gap = 6, x0 = (320 - (5 * tw + 4 * gap)) / 2, y0 = 42, row = 62;
		int c = (int)((tp.px - x0 + gap / 2) / (tw + gap)), r = (int)((tp.py - y0 + 8) / row);
		if (tp.px >= x0 - gap / 2 && c >= 0 && c < 5 && r >= 0 && r < 2) {
			int i = r * 5 + c;
			if (i == *sel)
				pick = true;   // tap the selected cell again to use it
			*sel = i;
		}
	}
	return pick;
}

int states_run(bool saving, const char *prefix, const char *title, int *slot, states_pick_t *pick)
{
	static browser_t b;
	memset(&b, 0, sizeof(b));
	b.saving = saving;
	b.title = title;
	b.page = saving ? STATES_PAGE_SLOTS : pick->page;
	b.sel = (*slot >= 0 && *slot < SLOTS) ? *slot : 0;
	scan(&b, prefix);
	if (!saving && !b.slot[b.sel].used)   // loading: start on the newest save
		for (int i = 0, best = -1; i < SLOTS; i++)
			if (b.slot[i].used && (best < 0 || b.slot[i].saved > (u32)best)) {
				best = (int)b.slot[i].saved;
				b.sel = i;
			}
	card_select(&b);
	sfx_play(SFX_SELECT);

	int result = -1;
	while (aptMainLoop()) {
		if (dev_tick(true) || dev_ui_pending()) break;
		input_poll();
		ui_button_t bar[STATES_BAR_MAX];
		u32 k = ui_header_back(ui_bar_input(bar, states_bar(&b, bar), input_keys_down()));

		if (!saving && (k & (KEY_L | KEY_R))) {
			b.page = (b.page + (k & KEY_R ? 1 : 2)) % 3;
			b.confirm = false;
			sfx_play(SFX_MOVE);
			k = 0;
		}
		if (k & KEY_B) {
			sfx_play(SFX_BACK);
			if (!b.confirm)
				break;
			b.confirm = false;
			k = 0;
		}
		if (b.page == STATES_PAGE_RING) {
			int before = b.ring_sel;
			if (grid_input(k, &b.ring_sel) && b.ring_sel < b.ring_n) {
				pick->page = STATES_PAGE_RING;
				pick->entry = b.ring_e[b.ring_sel];
				result = STATES_PAGE_RING;
				break;
			}
			if (b.ring_sel != before)
				sfx_play(SFX_MOVE);
		} else if (b.page == STATES_PAGE_CARDS) {
			int before = b.card_sel;
			if ((k & KEY_DOWN) && b.card_sel + 1 < b.cards_n) b.card_sel++;
			if ((k & KEY_UP) && b.card_sel > 0) b.card_sel--;
			if (k & KEY_TOUCH) {
				touchPosition tp;
				hidTouchRead(&tp);
				int r = (tp.py - 38) / 22;
				if (tp.py >= 38 && r >= 0 && r < CARD_ROWS && b.card_top + r < b.cards_n) {
					if (b.card_top + r == b.card_sel)
						k |= KEY_A;
					b.card_sel = b.card_top + r;
				}
			}
			if (b.card_sel < b.card_top) b.card_top = b.card_sel;
			if (b.card_sel >= b.card_top + CARD_ROWS) b.card_top = b.card_sel - CARD_ROWS + 1;
			if (b.card_sel != before) {
				card_select(&b);
				sfx_play(SFX_MOVE);
			}
			if ((k & KEY_A) && b.card_sel < b.cards_n &&
			    ui_confirm_action("Put this memory card back?",
					"The game restarts with this card. The card as it is now and the game as it is now are kept in the backups.",
					"A  Put it back", true)) {
				pick->page = STATES_PAGE_CARDS;
				pick->entry = b.cards[b.card_sel].e;
				result = STATES_PAGE_CARDS;
				break;
			}
		} else {
			int before = b.sel;
			bool picked = grid_input(k, &b.sel);
			if (b.sel != before) {
				b.confirm = false;
				sfx_play(SFX_MOVE);
			}
			slot_t *s = &b.slot[b.sel];
			if (picked && saving) {
				if (s->used && !(b.confirm && !b.confirm_delete)) {
					b.confirm = true;
					b.confirm_delete = false;
					sfx_play(SFX_MOVE);
				} else {
					result = STATES_PAGE_SLOTS;
					break;
				}
			} else if (picked && s->used) {
				result = STATES_PAGE_SLOTS;
				break;
			}
			if ((k & KEY_X) && s->used) {
				if (b.confirm && b.confirm_delete) {
					if (slot_delete(prefix, b.sel)) {
						if (s->tex_ok)
							C3D_TexDelete(&s->tex);
						memset(s, 0, sizeof(*s));
					}
					b.confirm = false;
					sfx_play(SFX_BACK);
				} else {
					b.confirm = true;
					b.confirm_delete = true;
					sfx_play(SFX_MOVE);
				}
			}
		}

		b.t += 1.0f / 60.0f;
		lift_update(&b, b.page == STATES_PAGE_RING ? b.ring : b.slot, b.page == STATES_PAGE_RING ? b.ring_sel : b.sel);
		ui_begin();
		video_present_vsync(true, draw_top, draw_bottom, &b);
	}
	*slot = b.sel;
	release(&b);
	return result;
}

// ---------------------------------------------------------------- resume

typedef struct { C3D_Tex tex; bool tex_ok, older; u32 saved; float t; } resume_t;

static void resume_top(void *ud)
{
	resume_t *r = ud;
	C2D_DrawRectSolid(0, 0, 0.6f, 400, 240, C2D_Color32(4, 6, 18, 255));
	const float pw = 264, ph = 198, px = (400 - pw) / 2, py = 10;
	float dx = ui_depth(-0.6f);
	C2D_DrawRectSolid(px - 3 + dx, py - 3, 0.62f, pw + 6, ph + 6, accent[2]);
	if (r->tex_ok)
		draw_tex(&r->tex, px + dx, py, pw, ph, 0.64f);
	char when[40], line[64];
	when_text(when, sizeof(when), r->saved);
	snprintf(line, sizeof(line), r->saved ? "Left off %s" : "", when);
	ui_text_ex(NULL, 200 + dx, 215, 0.5f, C2D_Color32(240, 242, 255, 255), C2D_AlignCenter, line);
}

// Y (an older auto-save) only when the backups have one
static const ui_button_t resume_bar[3] = { { KEY_B, "Restart", UI_ICON_RESET, false }, { KEY_A, "Continue", UI_ICON_PLAY, false }, { KEY_Y, "Older", UI_ICON_LOAD, false } };

static void resume_bottom(void *ud)
{
	resume_t *r = ud;
	const theme_t *th = theme();
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, th->menu_bg);
	ui_text_ex(libui_font(), 160, 70, 0.6f, th->header_title, C2D_AlignCenter, "Continue?");
	ui_text_wrap(30, 112, 0.5f, th->help_text, 260, "Pick up where you left off, or start the game from the beginning.");
	ui_text_wrap(30, 160, 0.42f, th->hint_lib, 260, "Starting the game keeps this spot in Auto-saves.");
	ui_bar_draw(resume_bar, r->older ? 3 : 2);
}

// The game was auto-saved when it was last closed: offer to continue.
// RESUME_CONTINUE: load the auto-save; RESUME_START: start the game;
// RESUME_OLDER: show the ring of automatic saves; RESUME_NONE: no prompt.
int states_resume_prompt(const char *prefix)
{
	static resume_t r;
	memset(&r, 0, sizeof(r));
	{
		const char *game = strrchr(prefix, '/');
		hist_entry_t e;
		r.older = history_list(game ? game + 1 : prefix, false, &e, 1) > 0;
	}
	if (!state_stat(prefix, STATE_AUTO, &r.saved))
		return RESUME_NONE;
	u16 *px = malloc(TW * TH * 2);
	thm_hdr_t h;
	if (px && thumb_read(prefix, STATE_AUTO, px, &h, false)) {
		r.tex_ok = make_tex(&r.tex, px);
		r.saved = h.saved;
	}
	free(px);
	int resume = RESUME_NONE;
	while (aptMainLoop()) {
		dev_tick_keys();
		input_poll();
		u32 k = ui_bar_input(resume_bar, r.older ? 3 : 2, input_keys_down());
		if (k & KEY_A) { resume = RESUME_CONTINUE; sfx_play(SFX_SELECT); break; }
		if (k & KEY_B) { resume = RESUME_START; sfx_play(SFX_BACK); break; }
		if ((k & KEY_Y) && r.older) { resume = RESUME_OLDER; sfx_play(SFX_SELECT); break; }
		ui_begin();
		video_present_vsync(false, resume_top, resume_bottom, &r);
	}
	if (r.tex_ok)
		C3D_TexDelete(&r.tex);
	return resume;
}

// ---------------------------------------------------------------- main menu

// The main menu's Continue tile: is there an auto-save, when was it made and
// its picture (tex is made only when *tex_ok comes back true).
bool states_auto_peek(const char *prefix, u32 *saved, C3D_Tex *tex, bool *tex_ok)
{
	char path[PATH_LEN], bak[PATH_LEN + 8];
	struct stat st;
	*tex_ok = false;
	*saved = 0;
	slot_path(path, sizeof(path), prefix, STATE_AUTO, false);
	snprintf(bak, sizeof(bak), "%s.bak", path);
	if (stat(path, &st) && stat(bak, &st))
		return false;
	// the time comes from the thumbnail only: the 3DS gives SD files no times
	u16 *px = malloc(TW * TH * 2);
	thm_hdr_t h;
	if (px && thumb_read(prefix, STATE_AUTO, px, &h, false)) {
		*tex_ok = make_tex(tex, px);
		*saved = h.saved;
	}
	free(px);
	return true;
}

void states_thumb_draw(C3D_Tex *t, float x, float y, float w, float h, float z)
{
	draw_tex(t, x, y, w, h, z);
}

void states_when_text(char *out, size_t len, u32 saved)
{
	when_text(out, len, saved);
}
