// Save state browser. Every state keeps a screenshot of the moment it was
// saved (<game>.stN.thm next to <game>.stN). The bottom screen shows the ten
// slots as a grid of thumbnails; the top screen shows the selected one large
// with the time it was saved.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include "psx3d.h"
#include "devtools.h"

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

typedef struct {
	bool saving;
	const char *title;
	slot_t slot[SLOTS];
	C3D_Tex now;        // the paused frame: preview for saving to an empty slot
	bool now_ok;
	int sel;
	bool confirm;       // waiting for a second A (overwrite) or X (delete)
	bool confirm_delete;
	float t;
} browser_t;

static const u32 accent[4] = { 0xff3c1ee6, 0xff14cdfa, 0xff78be1e, 0xffe66e28 }; // ABGR: red, yellow, green, blue

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

// ... saved next to state <slot>
bool state_thumb_save(const char *prefix, int slot, const u16 *px)
{
	char path[PATH_LEN];
	bool ok = false;
	slot_path(path, sizeof(path), prefix, slot, true);
	FILE *f = fopen(path, "wb");
	if (f) {
		thm_hdr_t h = { THM_MAGIC, TW, TH, (u32)time(NULL) };
		ok = fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(px, 2, TW * TH, f) == TW * TH;
		fclose(f);
	}
	return ok;
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
	char path[PATH_LEN];
	struct stat st;
	u16 *px = malloc(TW * TH * 2);

	for (int i = 0; i < SLOTS; i++) {
		slot_t *s = &b->slot[i];
		slot_path(path, sizeof(path), prefix, i, false);
		s->used = stat(path, &st) == 0;
		s->saved = s->used ? (u32)st.st_mtime : 0;
		if (!s->used || !px)
			continue;
		slot_path(path, sizeof(path), prefix, i, true);
		FILE *f = fopen(path, "rb");
		thm_hdr_t h;
		if (f && fread(&h, sizeof(h), 1, f) == 1 && h.magic == THM_MAGIC && h.w == TW && h.h == TH &&
		    fread(px, 2, TW * TH, f) == TW * TH && make_tex(&s->tex, px)) {
			s->tex_ok = s->thumb = true;
			s->saved = h.saved;
		}
		if (f)
			fclose(f);
	}
	if (px && video_snapshot(px, TW, TH))
		b->now_ok = make_tex(&b->now, px);
	free(px);
}

static void release(browser_t *b)
{
	for (int i = 0; i < SLOTS; i++)
		if (b->slot[i].tex_ok)
			C3D_TexDelete(&b->slot[i].tex);
	if (b->now_ok)
		C3D_TexDelete(&b->now);
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

static void draw_top(void *ud)
{
	browser_t *b = ud;
	slot_t *s = &b->slot[b->sel];
	C2D_Font font = libui_font();
	u32 white = C2D_Color32(240, 242, 255, 255), dim = C2D_Color32(150, 160, 200, 255);

	C2D_DrawRectSolid(0, 0, 0.6f, 400, 240, C2D_Color32(4, 6, 18, 225));

	// the preview: the slot's screenshot, or for saving to an empty slot the
	// frame that would be saved
	const float pw = 264, ph = 198, px = (400 - pw) / 2, py = 10;
	float dx = ui_depth(-0.6f);
	C3D_Tex *t = s->tex_ok ? &s->tex : (b->saving && b->now_ok ? &b->now : NULL);
	C2D_DrawRectSolid(px - 3 + dx, py - 3, 0.62f, pw + 6, ph + 6, accent[b->sel & 3]);
	C2D_DrawRectSolid(px + dx, py, 0.63f, pw, ph, C2D_Color32(10, 12, 30, 255));
	if (t)
		draw_tex(t, px + dx, py, pw, ph, 0.64f);
	else
		ui_text_ex(font, 200 + dx, py + ph / 2 - 10, 0.6f, dim, C2D_AlignCenter,
			s->used ? "No preview" : "Empty");

	char line[96], when[40];
	when_text(when, sizeof(when), s->saved);
	if (b->saving && !s->used)
		snprintf(line, sizeof(line), "Slot %d  -  new save", b->sel);
	else if (s->used && !s->saved)   // copied in without a date
		snprintf(line, sizeof(line), "Slot %d", b->sel);
	else if (s->used)
		snprintf(line, sizeof(line), "Slot %d  -  %s", b->sel, when);
	else
		snprintf(line, sizeof(line), "Slot %d  -  empty", b->sel);
	ui_text_ex(NULL, 200 + dx, 215, 0.5f, white, C2D_AlignCenter, line);
}

static void draw_bottom(void *ud)
{
	browser_t *b = ud;
	C2D_Font font = libui_font();
	u32 white = C2D_Color32(235, 238, 255, 255), dim = C2D_Color32(110, 120, 160, 255);

	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, C2D_Color32(6, 9, 24, 255));
	ui_text_ex(font, 10, 6, 0.5f, white, 0, b->saving ? "Save state" : "Load state");
	ui_text_ex(NULL, 310, 9, 0.42f, dim, C2D_AlignRight, b->title);
	for (int i = 0; i < 4; i++)
		C2D_DrawRectSolid(10 + i * 18, 26, 0.2f, 16, 2, accent[i]);

	// 5 x 2 grid of thumbnails
	const float tw = 56, th = 42, gap = 6, x0 = (320 - (5 * tw + 4 * gap)) / 2, y0 = 42, row = 62;
	for (int i = 0; i < SLOTS; i++) {
		slot_t *s = &b->slot[i];
		float lift = s->lift, bob = sinf(b->t * 1.7f + i * 0.8f) * (0.4f + 0.8f * lift);
		float sc = 1.0f + 0.14f * lift;
		float w = tw * sc, h = th * sc;
		float cx = x0 + (i % 5) * (tw + gap) + tw / 2, cy = y0 + (i / 5) * row + th / 2 - 3 * lift + bob;
		float x = cx - w / 2, y = cy - h / 2, z = 0.3f + 0.05f * lift;
		bool sel = i == b->sel;

		if (sel) {
			C2D_DrawRectSolid(x - 5, y - 5, z, w + 10, h + 10, (accent[i & 3] & 0x00ffffff) | 0x40000000);
			C2D_DrawRectSolid(x - 2, y - 2, z + 0.01f, w + 4, h + 4, accent[i & 3]);
		}
		C2D_DrawRectSolid(x, y, z + 0.02f, w, h, C2D_Color32(18, 22, 48, 255));
		if (s->tex_ok) {
			draw_tex(&s->tex, x, y, w, h, z + 0.03f);
			if (!sel)
				C2D_DrawRectSolid(x, y, z + 0.04f, w, h, C2D_Color32(6, 9, 24, 90));
		} else {
			ui_text_ex(NULL, cx, cy - 8, 0.45f, dim, C2D_AlignCenter,
				s->used ? "?" : (b->saving ? "+" : "-"));
		}
		char n[4];
		snprintf(n, sizeof(n), "%d", i);
		ui_text_ex(NULL, cx, y + h + 2, 0.4f, sel ? white : dim, C2D_AlignCenter, n);
	}

	// details and buttons
	slot_t *s = &b->slot[b->sel];
	char when[40], line[96];
	when_text(when, sizeof(when), s->saved);
	if (b->confirm)
		snprintf(line, sizeof(line), b->confirm_delete ? "Delete slot %d? Press X again" :
			"Overwrite slot %d? Press A again", b->sel);
	else if (s->used && !s->saved)
		snprintf(line, sizeof(line), "Saved state (no date)");
	else if (s->used)
		snprintf(line, sizeof(line), "Saved %s", when);
	else
		snprintf(line, sizeof(line), b->saving ? "Empty slot" : "Nothing saved here yet");
	ui_text_ex(NULL, 160, 180, 0.5f, b->confirm ? C2D_Color32(255, 210, 120, 255) : white,
		C2D_AlignCenter, line);

	const char *hint = b->saving ? (s->used ? "A save    X delete    B back" : "A save    B back")
	                             : (s->used ? "A load    X delete    B back" : "B back");
	ui_text_ex(NULL, 160, 222, 0.42f, dim, C2D_AlignCenter, hint);
}

// ---------------------------------------------------------------- run

// Returns the slot to save to / load from, or -1. *slot is the slot to
// start on and is updated with the last one selected.
int states_run(bool saving, const char *prefix, const char *title, int *slot)
{
	static browser_t b;
	memset(&b, 0, sizeof(b));
	b.saving = saving;
	b.title = title;
	b.sel = (*slot >= 0 && *slot < SLOTS) ? *slot : 0;
	scan(&b, prefix);
	if (!saving && !b.slot[b.sel].used)   // loading: start on the newest save
		for (int i = 0, best = -1; i < SLOTS; i++)
			if (b.slot[i].used && (best < 0 || b.slot[i].saved > (u32)best)) {
				best = (int)b.slot[i].saved;
				b.sel = i;
			}
	sfx_play(SFX_SELECT);

	int result = -1;
	while (aptMainLoop()) {
		if (dev_tick(true) || dev_ui_pending()) break;
		input_poll();
		u32 k = input_keys_down();
		int before = b.sel;

		if (k & KEY_RIGHT) b.sel = (b.sel + 1) % SLOTS;
		if (k & KEY_LEFT) b.sel = (b.sel + SLOTS - 1) % SLOTS;
		if (k & (KEY_DOWN | KEY_UP)) b.sel = (b.sel + 5) % SLOTS;
		bool pick = k & KEY_A;
		if (k & KEY_TOUCH) {
			touchPosition tp;
			hidTouchRead(&tp);
			const float tw = 56, gap = 6, x0 = (320 - (5 * tw + 4 * gap)) / 2, y0 = 42, row = 62;
			int c = (int)((tp.px - x0 + gap / 2) / (tw + gap)), r = (int)((tp.py - y0 + 8) / row);
			if (tp.px >= x0 - gap / 2 && c >= 0 && c < 5 && r >= 0 && r < 2) {
				int i = r * 5 + c;
				if (i == b.sel)
					pick = true;   // tap the selected slot again to use it
				b.sel = i;
			}
		}
		if (b.sel != before) {
			b.confirm = false;
			sfx_play(SFX_MOVE);
		}
		if (k & KEY_B) {
			if (b.confirm)
				b.confirm = false;
			else
				break;
			sfx_play(SFX_BACK);
		}

		slot_t *s = &b.slot[b.sel];
		if (pick && saving) {
			if (s->used && !(b.confirm && !b.confirm_delete)) {
				b.confirm = true;
				b.confirm_delete = false;
				sfx_play(SFX_MOVE);
			} else {
				result = b.sel;
				break;
			}
		} else if (pick && s->used) {
			result = b.sel;
			break;
		}
		if ((k & KEY_X) && s->used) {
			if (b.confirm && b.confirm_delete) {
				char path[PATH_LEN];
				slot_path(path, sizeof(path), prefix, b.sel, false);
				remove(path);
				slot_path(path, sizeof(path), prefix, b.sel, true);
				remove(path);
				if (s->tex_ok)
					C3D_TexDelete(&s->tex);
				memset(s, 0, sizeof(*s));
				b.confirm = false;
				sfx_play(SFX_BACK);
			} else {
				b.confirm = true;
				b.confirm_delete = true;
				sfx_play(SFX_MOVE);
			}
		}

		b.t += 1.0f / 60.0f;
		for (int i = 0; i < SLOTS; i++) {
			slot_t *si = &b.slot[i];
			float target = i == b.sel ? 1.0f : 0.0f;
			si->lift_v += ((target - si->lift) * 300.0f - si->lift_v * 22.0f) / 60.0f;
			si->lift += si->lift_v / 60.0f;
		}
		ui_begin();
		video_present_vsync(true, draw_top, draw_bottom, &b);
	}
	*slot = b.sel;
	release(&b);
	return result;
}

// ---------------------------------------------------------------- resume

typedef struct { C3D_Tex tex; bool tex_ok; u32 saved; float t; } resume_t;

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
	snprintf(line, sizeof(line), "Left off %s", when);
	ui_text_ex(NULL, 200 + dx, 215, 0.5f, C2D_Color32(240, 242, 255, 255), C2D_AlignCenter, line);
}

static void resume_bottom(void *ud)
{
	(void)ud;
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, C2D_Color32(6, 9, 24, 255));
	ui_text_ex(libui_font(), 160, 70, 0.6f, C2D_Color32(235, 238, 255, 255), C2D_AlignCenter, "Continue?");
	ui_text_ex(NULL, 160, 120, 0.5f, C2D_Color32(170, 185, 230, 255), C2D_AlignCenter, "A  pick up where you left off");
	ui_text_ex(NULL, 160, 145, 0.5f, C2D_Color32(170, 185, 230, 255), C2D_AlignCenter, "B  start the game");
}

// The game was auto-saved when it was last closed: offer to continue.
// Returns true to load the auto-save.
bool states_resume_prompt(const char *prefix)
{
	char path[PATH_LEN];
	struct stat st;
	slot_path(path, sizeof(path), prefix, STATE_AUTO, false);
	if (stat(path, &st) != 0)
		return false;
	static resume_t r;
	memset(&r, 0, sizeof(r));
	r.saved = (u32)st.st_mtime;
	u16 *px = malloc(TW * TH * 2);
	slot_path(path, sizeof(path), prefix, STATE_AUTO, true);
	FILE *f = fopen(path, "rb");
	thm_hdr_t h;
	if (f && px && fread(&h, sizeof(h), 1, f) == 1 && h.magic == THM_MAGIC && h.w == TW && h.h == TH &&
	    fread(px, 2, TW * TH, f) == TW * TH) {
		r.tex_ok = make_tex(&r.tex, px);
		r.saved = h.saved;
	}
	if (f)
		fclose(f);
	free(px);
	bool resume = false;
	while (aptMainLoop()) {
		dev_tick_keys();
		input_poll();
		u32 k = input_keys_down();
		if (k & KEY_TOUCH) {
			touchPosition tp;
			hidTouchRead(&tp);
			if (tp.py >= 110 && tp.py < 135) k |= KEY_A;
			if (tp.py >= 135 && tp.py < 160) k |= KEY_B;
		}
		if (k & KEY_A) { resume = true; sfx_play(SFX_SELECT); break; }
		if (k & KEY_B) { sfx_play(SFX_BACK); break; }
		ui_begin();
		video_present_vsync(false, resume_top, resume_bottom, &r);
	}
	if (r.tex_ok)
		C3D_TexDelete(&r.tex);
	return resume;
}
