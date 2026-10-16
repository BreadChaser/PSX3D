// Minimal text and rectangle drawing using the system font.
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "psx3d.h"

static C2D_TextBuf textbuf;

void ui_init(void)
{
	textbuf = C2D_TextBufNew(8192);
}

void ui_exit(void)
{
	C2D_TextBufDelete(textbuf);
}

// call once per frame before drawing text
void ui_begin(void)
{
	C2D_TextBufClear(textbuf);
}

void ui_text(float x, float y, float size, u32 color, const char *fmt, ...)
{
	char buf[256];
	C2D_Text text;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (!C2D_TextParse(&text, textbuf, buf))
		return; // text buffer full this frame
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor, x, y, 0.5f, size, size, color);
}

void ui_rect(float x, float y, float w, float h, u32 color)
{
	C2D_DrawRectSolid(x, y, 0.5f, w, h, color);
}

void ui_text_ex(C2D_Font font, float x, float y, float size, u32 color, u32 flags, const char *s)
{
	C2D_Text text;
	if (!C2D_TextFontParse(&text, font, textbuf, s))
		return;
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor | flags, x, y, 0.7f, size, size, color);
}

// ---------------------------------------------------------------- floaty list
// A list where nothing jumps: the highlight glides between rows on a spring,
// the selected row drifts out to the right, every row bobs a little, and
// the list scrolls smoothly. Used by the library and the pause menu.

static float spring(float *x, float *v, float target, float k, float damp)
{
	const float dt = 1.0f / 60.0f;
	*v += ((target - *x) * k - *v * damp) * dt;
	*x += *v * dt;
	return *x;
}

void flist_update(flist_t *f, int sel, int n, int rows)
{
	if (!f->init) {
		f->hl = sel;
		f->init = true;
	}
	f->t += 1.0f / 60.0f;
	// keep the selection a row away from the edges when it can be
	int top = (int)(f->scroll_target + 0.5f);
	if (sel < top + 1) top = sel - 1;
	if (sel > top + rows - 2) top = sel - rows + 2;
	if (top > n - rows) top = n - rows;
	if (top < 0) top = 0;
	f->scroll_target = top;
	spring(&f->hl, &f->hl_v, sel, 260, 24);
	spring(&f->scroll, &f->scroll_v, top, 200, 24);
	for (int i = 0; i < FLIST_MAX && i < n; i++) {
		float target = i == sel ? 1.0f : 0.0f;
		f->lift[i] += (target - f->lift[i]) * 0.2f;
	}
}

// jump straight to a selection: no scrolling or highlight glide
void flist_snap(flist_t *f, int sel, int n, int rows)
{
	memset(f, 0, sizeof(*f));
	int top = sel - rows / 2;
	if (top > n - rows) top = n - rows;
	if (top < 0) top = 0;
	f->scroll = f->scroll_target = top;
	f->hl = sel;
	if (sel >= 0 && sel < FLIST_MAX)
		f->lift[sel] = 1;
	f->init = true;
}

int flist_row_at(const flist_t *f, float y0, float row_h, float py)
{
	return (int)floorf(f->scroll + (py - y0) / row_h);
}

void flist_draw(const flist_t *f, int n, int sel, float x, float y0, float w, float row_h, int rows,
	const char *(*label)(int, void *), const char *(*value)(int, void *), void *ud, u32 accent)
{
	// highlight: soft glow, gradient bar, accent edge
	float hy = y0 + (f->hl - f->scroll) * row_h;
	if (hy > y0 - row_h && hy < y0 + rows * row_h) {
		float bob = sinf(f->t * 2.0f) * 0.8f;
		C2D_DrawRectSolid(x - 2, hy - 2 + bob, 0.2f, w + 4, row_h + 4, C2D_Color32(60, 90, 200, 40));
		C2D_DrawRectangle(x, hy + bob, 0.21f, w, row_h,
			C2D_Color32(40, 58, 130, 235), C2D_Color32(24, 34, 84, 235),
			C2D_Color32(40, 58, 130, 235), C2D_Color32(24, 34, 84, 235));
		C2D_DrawRectSolid(x, hy + bob, 0.22f, 3, row_h, accent);
	}
	int first = (int)floorf(f->scroll) - 1;
	for (int i = first < 0 ? 0 : first; i < n && i <= f->scroll + rows; i++) {
		float ry = y0 + (i - f->scroll) * row_h;
		// rows fade out as they slide past the list's edges
		float edge = 1.0f;
		if (ry < y0) edge = 1.0f - (y0 - ry) / row_h;
		if (ry + row_h > y0 + rows * row_h) edge = 1.0f - (ry + row_h - (y0 + rows * row_h)) / row_h;
		if (edge <= 0.02f)
			continue;
		float lift = i < FLIST_MAX ? f->lift[i] : (i == sel);
		float bob = sinf(f->t * 1.6f + i * 0.9f) * (0.6f + 0.8f * lift);
		u8 c = (u8)(150 + 105 * lift), a = (u8)(255 * edge);
		u32 col = C2D_Color32(c, c, c + 30 > 255 ? 255 : c + 30, a);
		float tx = x + 12 + 8 * lift;
		const char *s = label(i, ud);
		ui_text_ex(NULL, tx, ry + (row_h - 15) / 2 + bob, 0.5f, col, 0, s);
		if (value) {
			const char *v = value(i, ud);
			if (v && v[0])
				ui_text_ex(NULL, x + w - 8, ry + (row_h - 15) / 2 + bob, 0.45f,
					C2D_Color32(130, 160, 230, a), C2D_AlignRight, v);
		}
	}
}

// ---------------------------------------------------------------- help panel
// Lower part of the top screen: what the selected option does and its speed
// impact. y0 is the top of the panel.
void ui_help_panel(float y0, const char *title, const char *impact, const char *text)
{
	if ((!text || !text[0]) && !impact)
		return;
	float dx = ui_depth(-0.4f);
	C2D_DrawRectSolid(12 + dx, y0, 0.66f, 376, 236 - y0, C2D_Color32(6, 9, 24, 225));
	C2D_DrawRectSolid(12 + dx, y0, 0.67f, 3, 236 - y0, C2D_Color32(40, 110, 230, 255));
	float y = y0 + 6;
	if (title) {
		ui_text_ex(NULL, 24 + dx, y, 0.5f, C2D_Color32(235, 238, 255, 255), 0, title);
		y += 18;
	}
	if (impact) {
		char b[96];
		snprintf(b, sizeof(b), "Speed: %s", impact);
		ui_text_ex(NULL, 24 + dx, y, 0.42f, C2D_Color32(250, 205, 20, 255), 0, b);
		y += 15;
	}
	if (text && text[0]) {
		C2D_Text t;
		if (C2D_TextParse(&t, textbuf, text)) {
			C2D_TextOptimize(&t);
			C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, 24 + dx, y, 0.82f, 0.4f, 0.4f,
				C2D_Color32(170, 180, 215, 255), 352.0f);
		}
	}
}

// help for PSX3D's own settings, by name
static const char *const own_help[][3] = {
	{ "Screen", "Widescreen can be slower", "4:3 is the original picture. Stretch fills the screen. Widescreen makes 3D games draw a wider view (uses the game's own widescreen code when there is one). Remembered per game." },
	{ "Screen (default)", "Widescreen can be slower", "The screen mode for games you haven't set one for. 4:3 is the original picture, stretch fills the screen, widescreen draws a wider view in 3D games." },
	{ "3D", "No speed effect", "screen depth puts the game picture slightly behind the screen when the 3D slider is up." },
	{ "3D resolution", "Half is faster in 3D games", "Draws 3D scenes with every other line copied, while text, menus and 2D stay sharp. Worth it on Old 3DS; New 3DS doesn't need it." },
	{ "Smoothing", "No speed effect", "Smooths the picture when it's scaled up to the 3DS screen. Off gives sharp pixels." },
	{ "Show FPS", "No speed effect", "Shows the frame rate on the bottom screen while playing." },
	{ "Controls", "No speed effect", "Choose which PS1 button each 3DS button presses, for this game." },
	{ "Rewind", "Costs a little, New 3DS only", "Hold SELECT + L to go back in time. Saves a snapshot 6 times a second; turn it on for games where you want it." },
	{ "Continue where I left off", "No speed effect", "Saves when you leave a game and offers to continue from there next time." },
	{ "Core options", NULL, "Settings of the emulator itself: accuracy, speed hacks, BIOS and more. Each one explains itself." },
	{ "Menu sounds", "No speed effect", NULL },
	{ "Library music", "No speed effect", NULL },
};

void ui_help_for(float y0, const char *name)
{
	for (size_t i = 0; i < sizeof(own_help) / sizeof(own_help[0]); i++)
		if (!strcmp(own_help[i][0], name)) {
			ui_help_panel(y0, name, own_help[i][1], own_help[i][2]);
			return;
		}
}
