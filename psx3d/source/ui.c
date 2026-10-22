// Minimal text and rectangle drawing using the system font.
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "psx3d.h"
#include "devtools.h"

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

// Shorten s with "..." so it fits width pixels at the given text size.
// Results are cached by text: menus ask for the same rows every frame.
const char *ui_fit(const char *s, float size, float width)
{
	static C2D_TextBuf measure_buf;
	static struct { char src[128]; float size, width; char out[96]; } cache[32];
	static unsigned next;
	for (unsigned i = 0; i < 32; i++)
		if (cache[i].size == size && cache[i].width == width && !strcmp(cache[i].src, s))
			return cache[i].out;
	if (!measure_buf && !(measure_buf = C2D_TextBufNew(128)))
		return s;
	unsigned slot = next++ % 32;
	char *out = cache[slot].out;
	snprintf(cache[slot].src, sizeof(cache[slot].src), "%s", s);
	cache[slot].size = size;
	cache[slot].width = width;
	snprintf(out, sizeof(cache[slot].out), "%s", s);
	for (size_t n = strlen(out);; ) {
		C2D_Text t;
		float w = 0;
		C2D_TextBufClear(measure_buf);
		if (!C2D_TextParse(&t, measure_buf, out))
			break;
		C2D_TextGetDimensions(&t, size, size, &w, NULL);
		if (w <= width || n == 0)
			break;
		// drop one character (whole UTF-8 sequence) and trailing spaces
		do n--; while (n > 0 && ((unsigned char)s[n] & 0xc0) == 0x80);
		while (n > 0 && s[n - 1] == ' ') n--;
		snprintf(out, sizeof(cache[slot].out), "%.*s...", (int)n, s);
	}
	return out;
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

// Word-wrapped text in the system font, width in pixels.
void ui_text_wrap(float x, float y, float size, u32 color, float width, const char *s)
{
	C2D_Text text;
	if (!C2D_TextParse(&text, textbuf, s))
		return;
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor | C2D_WordWrap, x, y, 0.7f, size, size, color, width);
}

// A tap on the bottom screen's header (title and colour rule) means Back.
u32 ui_header_back(u32 keys)
{
	if (!(keys & KEY_TOUCH))
		return keys;
	touchPosition p;
	hidTouchRead(&p);
	return p.py < 30 ? (keys & ~KEY_TOUCH) | KEY_B : keys;
}

// The same title font, colours and rule for Controls, Settings and the game menu.
void ui_menu_header(const char *title, const char *right)
{
	const theme_t *th = theme();
	ui_text_ex(libui_font(), 10, 6, 0.5f, th->header_title, 0, title);
	if (right)
		ui_text_ex(NULL, 310, 9, 0.42f, th->header_right, C2D_AlignRight, right);
	for (int i=0; i<4; i++)
		C2D_DrawRectSolid(10+i*18, 26, 0.2f, 16, 2, th->accent[i]);
}

// Pixel-sized ASCII labels with explicit baseline/tracking. A positive width
// fits the glyph ink bounds exactly (the bottom-screen brand is 62 pixels wide).
// A negative width is a maximum: shrink long labels uniformly, never enlarge.
void ui_text_pixels(C2D_Font font, float x, float baseline, float pixels, float spacing,
	float width, u32 color, u32 align, const char *s)
{
	C2D_Text glyphs[32];
	size_t n = strlen(s);
	if (!n || n > 32) return;
	// Citro2D normalizes every font to a 30px cell, including the title font.
	float sy = pixels / 30;
	float metrics = 30.0f / C2D_FontGetInfo(font)->tglp->cellHeight;
	float advances[32], total = spacing * (n-1);
	float first_left = 0, last_right = 0;
	for (size_t i = 0; i < n; i++) {
		char ch[] = { s[i], 0 };
		if (!C2D_TextFontParse(&glyphs[i], font, textbuf, ch)) return;
		C2D_TextGetDimensions(&glyphs[i], sy, sy, &advances[i], NULL);
		charWidthInfo_s *cw = C2D_FontGetCharWidthInfo(font, C2D_FontGlyphIndexFromCodePoint(font, s[i]));
		if (i == 0) first_left = cw->left*metrics*sy;
		if (i == n-1) last_right = (cw->left + cw->glyphWidth)*metrics*sy;
		total += advances[i];
	}
	if (width != 0) total += last_right - advances[n-1] - first_left;
	float fit = total > 0 ? (width > 0 ? width/total : width < 0 ? fminf(1, -width/total) : 1) : 1;
	total *= fit;
	if (align == C2D_AlignCenter) x -= total/2;
	else if (align == C2D_AlignRight) x -= total;
	if (width != 0) x -= first_left*fit;
	for (size_t i = 0; i < n; i++) {
		C2D_DrawText(&glyphs[i], C2D_WithColor | C2D_AtBaseline,
			x, baseline, 0.7f, sy*fit, width < 0 ? sy*fit : sy, color);
		x += (advances[i] + spacing)*fit;
	}
}

// Locate the actual ink of the six shoulder/stick labels, once per app run.
// Font cell height includes blank padding, so centring the cell/baseline leaves
// text visibly off-centre beside vector symbols. System sheets use tiled A4/A8.
static unsigned font_texel(int x, int y, int width)
{
	unsigned morton = (x&1) | ((y&1)<<1) | ((x&2)<<1) |
		((y&2)<<2) | ((x&4)<<2) | ((y&4)<<3);
	return ((y/8)*(width/8)+x/8)*64 + morton;
}

void ui_button_text(float x, float y, float pixels, float spacing, u32 color, int id)
{
	static bool ready[6];
	static float midline[6];
	int slot = id-RETRO_DEVICE_ID_JOYPAD_L;
	if (slot < 0 || slot >= 6) return;
	const char *s = input_ps1_name(id);
	if (!ready[slot]) {
		const TGLP_s *g = C2D_FontGetInfo(NULL)->tglp;
		int top = g->cellHeight, bottom = -1;
		if (g->sheetFmt == GPU_A4 || g->sheetFmt == GPU_A8) {
			for (const char *p=s; *p; p++) {
				int index = C2D_FontGlyphIndexFromCodePoint(NULL, *p);
				int per_sheet = g->nRows*g->nLines, local = index%per_sheet;
				const u8 *sheet = g->sheetData+(index/per_sheet)*g->sheetSize;
				int ox = (local%g->nRows)*(g->cellWidth+1)+1;
				int oy = (local/g->nRows)*(g->cellHeight+1)+1;
				int width = C2D_FontGetCharWidthInfo(NULL, index)->glyphWidth;
				for (int row=0; row<g->cellHeight; row++)
					for (int col=0; col<width; col++) {
						unsigned pos = font_texel(ox+col, oy+row, g->sheetWidth);
						unsigned alpha = g->sheetFmt == GPU_A8 ? sheet[pos] : (sheet[pos/2] >> (4*(pos&1))) & 15;
						if (alpha) { if (row<top) top=row; if (row>bottom) bottom=row; }
					}
			}
		}
		midline[slot] = bottom >= top ? ((top+bottom+1)*0.5f-g->baselinePos)*30/g->cellHeight : -11;
		ready[slot] = true;
	}
	ui_text_pixels(NULL, x, y-midline[slot]*pixels/30, pixels, spacing, -68,
		color, C2D_AlignCenter, s);
}

// Controller/action marks are geometry, independent of the installed fonts.
// Coordinates are centred; size is the full height of the square face symbols.
static void symbol_line(float x, float y, float x2, float y2, float stroke, u32 c)
{
	C2D_DrawLine(x, y, c, x2, y2, c, stroke, 0.70f);
	C2D_DrawCircleSolid(x, y, 0.70f, stroke/2, c);
	C2D_DrawCircleSolid(x2, y2, 0.70f, stroke/2, c);
}

// Shared action palette: touch accents and symbols, Controls highlights/icons.
u32 ui_input_color(int id)
{
	unsigned hex = 0x9aa5c8; // soft lavender / grey-blue for other actions
	switch (id) {
	// These four PlayStation hues belong only to their face buttons.
	case RETRO_DEVICE_ID_JOYPAD_X: hex = 0x2fa36b; break;
	case RETRO_DEVICE_ID_JOYPAD_A: hex = 0xd0313b; break;
	case RETRO_DEVICE_ID_JOYPAD_B: hex = 0x2f6fc4; break;
	case RETRO_DEVICE_ID_JOYPAD_Y: hex = 0xd77fb5; break;
	// Library-inspired synthwave hues, separate from the PS1 face palette.
	case RETRO_DEVICE_ID_JOYPAD_L: case RETRO_DEVICE_ID_JOYPAD_R:
	case RETRO_DEVICE_ID_JOYPAD_L2: case RETRO_DEVICE_ID_JOYPAD_R2: hex = 0xb05ade; break;
	case RETRO_DEVICE_ID_JOYPAD_L3: case RETRO_DEVICE_ID_JOYPAD_R3: hex = 0x45bfd0; break;
	case INPUT_BIND_HIDE: hex = 0x2b3360; break;
	}
	return C2D_Color32(hex>>16, hex>>8, hex, 255);
}

bool ui_input_symbol(int id, float x, float y, float size, u32 color)
{
	float r = size/2, stroke = size/11;
	color = (ui_input_color(id) & 0xffffff) | (color & 0xff000000);
	if (id == RETRO_DEVICE_ID_JOYPAD_X) {
		symbol_line(x, y-r*0.9f, x-r, y+r*0.9f, stroke, color);
		symbol_line(x-r, y+r*0.9f, x+r, y+r*0.9f, stroke, color);
		symbol_line(x+r, y+r*0.9f, x, y-r*0.9f, stroke, color);
	} else if (id == RETRO_DEVICE_ID_JOYPAD_A) {
		// Outline without painting over the button's pressed gradient.
		for (int j = 0; j < 32; j++) {
			float a = j*(2*(float)M_PI/32), b = (j+1)*(2*(float)M_PI/32);
			C2D_DrawLine(x+r*cosf(a), y+r*sinf(a), color,
				x+r*cosf(b), y+r*sinf(b), color, stroke, 0.70f);
		}
	} else if (id == RETRO_DEVICE_ID_JOYPAD_B) {
		symbol_line(x-r*0.8f, y-r*0.8f, x+r*0.8f, y+r*0.8f, stroke, color);
		symbol_line(x+r*0.8f, y-r*0.8f, x-r*0.8f, y+r*0.8f, stroke, color);
	} else if (id == RETRO_DEVICE_ID_JOYPAD_Y) {
		symbol_line(x-r*0.85f, y-r*0.85f, x+r*0.85f, y-r*0.85f, stroke, color);
		symbol_line(x+r*0.85f, y-r*0.85f, x+r*0.85f, y+r*0.85f, stroke, color);
		symbol_line(x+r*0.85f, y+r*0.85f, x-r*0.85f, y+r*0.85f, stroke, color);
		symbol_line(x-r*0.85f, y+r*0.85f, x-r*0.85f, y-r*0.85f, stroke, color);
	} else if (id >= RETRO_DEVICE_ID_JOYPAD_UP && id <= RETRO_DEVICE_ID_JOYPAD_RIGHT) {
		float dx = id == RETRO_DEVICE_ID_JOYPAD_LEFT ? -1 : id == RETRO_DEVICE_ID_JOYPAD_RIGHT ? 1 : 0;
		float dy = id == RETRO_DEVICE_ID_JOYPAD_UP ? -1 : id == RETRO_DEVICE_ID_JOYPAD_DOWN ? 1 : 0;
		C2D_DrawTriangle(x+dx*r*0.825f, y+dy*r*0.825f, color,
			x-dx*r*0.825f-dy*r*0.8f, y-dy*r*0.825f+dx*r*0.8f, color,
			x-dx*r*0.825f+dy*r*0.8f, y-dy*r*0.825f-dx*r*0.8f, color, 0.70f);
	} else if (id == INPUT_FF_TOGGLE || id == INPUT_FF_HOLD || id == INPUT_REWIND_HOLD) {
		float dir = id == INPUT_REWIND_HOLD ? -1 : 1;
		for (int j = 0; j < 2; j++) {
			float cx = x + (j ? 0.5f : -0.5f)*r;
			C2D_DrawTriangle(cx+dir*r*0.45f, y, color,
				cx-dir*r*0.45f, y-r*0.65f, color,
				cx-dir*r*0.45f, y+r*0.65f, color, 0.70f);
		}
	} else if (id == RETRO_DEVICE_ID_JOYPAD_START) {
		// Rounded triangular PS1 START button.
		float ax=x-r*0.725f, bx=x+r*0.725f, h=r*0.55f;
		C2D_DrawTriangle(ax, y-h, color, ax, y+h, color, bx, y, color, 0.70f);
		symbol_line(ax, y-h, ax, y+h, stroke, color);
		symbol_line(ax, y+h, bx, y, stroke, color);
		symbol_line(bx, y, ax, y-h, stroke, color);
	} else if (id == RETRO_DEVICE_ID_JOYPAD_SELECT) {
		// Flat rounded SELECT pill, with quarter-circle corners.
		float w=r*1.8f, h=r*0.75f, cr=r*0.18f;
		C2D_DrawRectSolid(x-w/2+cr, y-h/2, 0.70f, w-2*cr, h, color);
		C2D_DrawRectSolid(x-w/2, y-h/2+cr, 0.70f, w, h-2*cr, color);
		for (int i=0; i<4; i++)
			C2D_DrawCircleSolid(x+(i&1 ? 1 : -1)*(w/2-cr),
				y+(i&2 ? 1 : -1)*(h/2-cr), 0.70f, cr, color);
	} else return false;
	return true;
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

void flist_draw_sections(const flist_t *f, int n, int sel, float x, float y0, float w, float row_h, int rows,
	const char *(*label)(int, void *), const char *(*value)(int, void *), void *ud, u32 accent, bool (*header)(int, void *))
{
	const theme_t *th = theme();
	if (value == input_control_value) accent = input_control_color(sel);
	// highlight: soft glow, gradient bar, accent edge
	float hy = y0 + (f->hl - f->scroll) * row_h;
	int highlight_row = (int)floorf(f->hl + 0.5f);
	if (hy > y0 - row_h && hy < y0 + rows * row_h &&
	    !(header && highlight_row >= 0 && highlight_row < n && header(highlight_row, ud))) {
		float bob = sinf(f->t * 2.0f) * 0.8f;
		C2D_DrawRectSolid(x - 2, hy - 2 + bob, 0.2f, w + 4, row_h + 4, th->hl_glow);
		C2D_DrawRectangle(x, hy + bob, 0.21f, w, row_h, th->hl_top, th->hl_bottom, th->hl_top, th->hl_bottom);
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
		if (header && header(i, ud)) {
			u8 alpha = (u8)(255 * edge);
			ui_text_ex(NULL, x + 12, ry + 4, 0.33f, theme_alpha(th->section, alpha), 0, label(i, ud));
			for (int j = 0; j < 4; j++)
				C2D_DrawRectSolid(x + 12 + j * 10, ry + 19, 0.3f, 8, 1, theme_alpha(th->accent[j], alpha));
			C2D_DrawRectSolid(x + 56, ry + 19, 0.3f, w - 68, 1, theme_alpha(th->section_rule, alpha));
			continue;
		}
		float lift = i < FLIST_MAX ? f->lift[i] : (i == sel);
		float bob = sinf(f->t * 1.6f + i * 0.9f) * (0.6f + 0.8f * lift);
		u8 a = (u8)(255 * edge);
		u32 col = theme_row(th, lift, a);
		float tx = x + 12 + 8 * lift;
		const char *s = label(i, ud);
		ui_text_ex(NULL, tx, ry + (row_h - 15) / 2 + bob, 0.5f, col, 0, s);
		if (value) {
			const char *v = value(i, ud);
			if (value == input_control_value && input_control_slot(i) >= 0) {
				int id = input_map_get(input_control_slot(i));
				if (id >= INPUT_FF_TOGGLE && !((id == INPUT_REWIND_HOLD) ? game_capabilities().rewind : game_capabilities().fast_forward)) id = -1;
				C2D_Text measure;
				float vw=0;
				C2D_TextFontParse(&measure, NULL, textbuf, v);
				C2D_TextGetDimensions(&measure, 0.45f, 0.45f, &vw, NULL);
				ui_input_symbol(id, x+w-8-vw-14, ry+row_h/2+bob, 12,
					theme_alpha(th->value, a));
			}
			if (v && v[0])
				ui_text_ex(NULL, x + w - 8, ry + (row_h - 15) / 2 + bob, 0.45f,
					theme_alpha(th->value, a), C2D_AlignRight, v);
		}
	}
}

void flist_draw(const flist_t *f, int n, int sel, float x, float y0, float w, float row_h, int rows,
	const char *(*label)(int, void *), const char *(*value)(int, void *), void *ud, u32 accent)
{
	flist_draw_sections(f, n, sel, x, y0, w, row_h, rows, label, value, ud, accent, NULL);
}

// ---------------------------------------------------------------- help panel
// Lower part of the top screen: what the selected option does and its speed
// impact. y0 is the top of the panel.
void ui_help_panel(float y0, const char *title, const char *impact, const char *text)
{
	if ((!text || !text[0]) && !impact)
		return;
	float dx = ui_depth(-0.4f);
	const theme_t *th = theme();
	C2D_DrawRectSolid(12 + dx, y0, 0.66f, 376, 236 - y0, th->help_bg);
	C2D_DrawRectSolid(12 + dx, y0, 0.67f, 3, 236 - y0, th->help_bar);
	float y = y0 + 6;
	if (title) {
		ui_text_ex(NULL, 24 + dx, y, 0.5f, th->help_title, 0, title);
		y += 18;
	}
	if (impact) {
		ui_text_ex(NULL, 24 + dx, y, 0.42f, th->help_impact, 0, impact);
		y += 15;
	}
	if (text && text[0]) {
		C2D_Text t;
		if (C2D_TextParse(&t, textbuf, text)) {
			C2D_TextOptimize(&t);
			C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, 24 + dx, y, 0.82f, 0.4f, 0.4f,
				th->help_text, 352.0f);
		}
	}
}

// Destructive settings actions require a fresh A press. B always cancels.
typedef struct { const char *title, *message, *action; ui_button_t bar[2]; char word[24]; } confirm_t;
static void confirm_draw(void *ud)
{
	confirm_t *c = ud;
	// Behind the header's colour rule, like the other menus.
	const theme_t *th = theme();
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, th->menu_bg);
	ui_menu_header(c->title, config_in_game() ? ui_fit(emu_game_title(), .42f, 150) : "All games");
	C2D_Text t;
	if (C2D_TextParse(&t, textbuf, c->message))
		C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, 16.0f, 62.0f, 0.7f, 0.5f, 0.5f,
			th->help_text, 288.0f);
	ui_bar_draw(c->bar, 2);
}

bool ui_confirm(const char *title, const char *message, bool show_game)
{
	return ui_confirm_action(title, message, "A  Reset to defaults", show_game);
}

bool ui_confirm_action(const char *title, const char *message, const char *action, bool show_game)
{
	// "A  Reset game" names the A button's action
	const char *label = action[0] == 'A' && action[1] == ' ' ? action + 1 : action;
	while (*label == ' ') label++;
	confirm_t c = { title, message, action, { { KEY_B, "Cancel", UI_ICON_CROSS, false }, { KEY_A, NULL, UI_ICON_CHECK, false } }, "" };
	// the tab shows the action's first word ("Reset game" -> Reset)
	snprintf(c.word, sizeof(c.word), "%.*s", (int)strcspn(label, " "), label);
	c.bar[1].label = c.word;
	if (!strcmp(c.word, "Reset"))
		c.bar[1].icon = UI_ICON_RESET;
	bool released = false;
	while (aptMainLoop()) {
		input_poll();
		// The pause menu also keeps answering the remote loop while this is open.
		if ((settings_ui_active() || show_game) && (dev_tick(show_game) || dev_ui_pending())) return false;
		u32 held = input_keys_held(), k = ui_bar_input(c.bar, 2, input_keys_down());
		if (released && (k & KEY_B)) return false;
		if (released && (k & KEY_A)) return true;
		if (!(held & (KEY_A | KEY_B | KEY_TOUCH))) released = true;
		ui_begin();
		video_present_vsync(show_game, NULL, confirm_draw, &c);
	}
	return false;
}

// ---------------------------------------------------------------- button bar
// The bar of touch buttons that replaces every menu's prompt line, in the
// owner's chosen style (button-refs ref7, like TWiLight Menu++ and
// Universal-Updater): flat tabs filling the strip edge to edge, thin dividers,
// a small icon over a one-word caption, the main action (A) filled in the
// theme's highlight colour, no button letters. B is always leftmost, A
// rightmost; physical presses light the matching tab. Colours come from the
// theme exactly as the UI mock's barTokensFor() derives them.

#define BAR_TOP 208
#define BAR_ICON_Y 217.0f    // icon centre
#define BAR_CAPTION_Y 233.0f // caption centre
#define BAR_CAPTION 0.36f    // caption size; shrinks to BAR_CAPTION_MIN before shortening
#define BAR_CAPTION_MIN 0.31f

static float text_width(float size, const char *s)
{
	static C2D_TextBuf wbuf;
	if (!wbuf && !(wbuf = C2D_TextBufNew(256)))
#ifdef PSX3D_TEST_PRESETS
		;   // the host test's text stubs need no buffer
#else
		return 0;
#endif
	C2D_Text t;
	float w = 0;
	if (wbuf)
		C2D_TextBufClear(wbuf);
	if (C2D_TextParse(&t, wbuf, s))
		C2D_TextGetDimensions(&t, size, size, &w, NULL);
	return w;
}

static u32 mix(u32 a, u32 b, float t, int alpha)
{
	int r = (int)((a & 255) + (((int)(b & 255)) - (int)(a & 255)) * t);
	int g = (int)(((a >> 8) & 255) + (((int)((b >> 8) & 255)) - (int)((a >> 8) & 255)) * t);
	int bl = (int)(((a >> 16) & 255) + (((int)((b >> 16) & 255)) - (int)((a >> 16) & 255)) * t);
	return C2D_Color32(r, g, bl, alpha);
}

u32 ui_bar_color(int which)
{
	const theme_t *th = theme();
	switch (which) {
	case UI_COL_BAR: return mix(th->menu_bg, 0, 0.35f, 255);
	case UI_COL_RULE: return mix(th->menu_bg, th->hl_top, 0.55f, 255);
	case UI_COL_NOTICE: return theme_alpha(th->menu_bg, 225);
	case UI_COL_STAR: return th->help_impact;
	}
	return 0;
}

static u32 flash_key;
static int flash_t;

// A five-point star from triangles; hollow draws a smaller one in its colour inside.
void ui_star(float cx, float cy, float r, u32 c, u32 hollow, float z)
{
	float px[10], py[10];
	for (int i = 0; i < 10; i++) {
		float a = -(float)M_PI / 2 + i * (float)M_PI / 5, rr = i & 1 ? r * 0.45f : r;
		px[i] = cx + cosf(a) * rr;
		py[i] = cy + sinf(a) * rr;
	}
	for (int i = 0; i < 10; i++) {
		int j = (i + 1) % 10;
		C2D_DrawTriangle(cx, cy, c, px[i], py[i], c, px[j], py[j], c, z);
	}
	if (hollow)
		ui_star(cx, cy + r * 0.06f, r * 0.55f, hollow, 0, z + 0.001f);
}

// an open circular arrow; dir 1 clockwise (rescan), -1 anticlockwise (reset)
static void bar_arc(float cx, float cy, float s, u32 ink, int dir, float z)
{
	float r = s * 0.4f, w = fmaxf(1.4f, s * 0.14f), a0 = -(float)M_PI * 0.35f, a1 = (float)M_PI * 1.25f;
	for (int i = 0; i < 10; i++) {
		float p = a0 + (a1 - a0) * i / 10, q = a0 + (a1 - a0) * (i + 1) / 10;
		C2D_DrawLine(cx + dir * cosf(p) * r, cy + sinf(p) * r, ink, cx + dir * cosf(q) * r, cy + sinf(q) * r, ink, w, z);
	}
	float hx = cx + dir * cosf(a0) * r, hy = cy + sinf(a0) * r;
	C2D_DrawTriangle(hx - dir * s * 0.3f, hy - s * 0.12f, ink, hx + dir * s * 0.12f, hy - s * 0.3f, ink,
		hx + dir * s * 0.08f, hy + s * 0.2f, ink, z);
}

// an arrow along y: dir -1 up, 1 down
static void bar_vertical_arrow(float cx, float cy, float s, u32 ink, int dir, float z)
{
	float tip = cy + dir * s * 0.45f, base = cy + dir * s * 0.02f;
	C2D_DrawTriangle(cx, tip, ink, cx - s * 0.36f, base, ink, cx + s * 0.36f, base, ink, z);
	float y0 = fminf(base, cy - dir * s * 0.4f), y1 = fmaxf(base, cy - dir * s * 0.4f);
	C2D_DrawRectSolid(cx - s * 0.11f, y0, z, s * 0.22f, y1 - y0, ink);
}

// Icons are simple shapes drawn in code, about s pixels across. All of them sit
// under the caption text, which ui_text_ex draws at 0.7.
static void bar_icon(int icon, float cx, float cy, float s, u32 ink, u32 bg, bool on)
{
	const float z = 0.68f, w = fmaxf(1.5f, s * 0.15f);
	switch (icon) {
	case UI_ICON_STAR:
		ui_star(cx, cy, s * 0.62f, on ? ui_bar_color(UI_COL_STAR) : ink, on ? 0 : bg, z);
		break;
	case UI_ICON_SORT:
		for (int i = 0; i < 3; i++)
			C2D_DrawRectSolid(cx - s * 0.5f, cy - s * 0.42f + i * s * 0.36f, z, s * (1 - i * 0.3f), fmaxf(1.5f, s * 0.16f), ink);
		break;
	case UI_ICON_RESCAN:
		bar_arc(cx, cy, s, ink, 1, z);
		break;
	case UI_ICON_RESET:
		bar_arc(cx, cy, s, ink, -1, z);
		break;
	case UI_ICON_LEFT:
		C2D_DrawTriangle(cx - s * 0.32f, cy, ink, cx + s * 0.22f, cy - s * 0.4f, ink, cx + s * 0.22f, cy + s * 0.4f, ink, z);
		break;
	case UI_ICON_RIGHT:
		C2D_DrawTriangle(cx + s * 0.32f, cy, ink, cx - s * 0.22f, cy - s * 0.4f, ink, cx - s * 0.22f, cy + s * 0.4f, ink, z);
		break;
	case UI_ICON_FOLDER:
		C2D_DrawRectSolid(cx - s * 0.5f, cy - s * 0.3f, z, s * 0.4f, s * 0.15f, ink);
		C2D_DrawRectSolid(cx - s * 0.5f, cy - s * 0.18f, z, s, s * 0.55f, ink);
		break;
	case UI_ICON_BACK:   // the mock's arrow: a head and a shaft
		C2D_DrawTriangle(cx - s * 0.5f, cy, ink, cx, cy - s * 0.42f, ink, cx, cy + s * 0.42f, ink, z);
		C2D_DrawRectSolid(cx - 1, cy - s * 0.15f, z, s * 0.5f + 1, s * 0.3f, ink);
		break;
	case UI_ICON_PLAY:
		C2D_DrawTriangle(cx - s * 0.3f, cy - s * 0.42f, ink, cx - s * 0.3f, cy + s * 0.42f, ink, cx + s * 0.42f, cy, ink, z);
		break;
	case UI_ICON_CHECK:
		C2D_DrawLine(cx - s * 0.45f, cy, ink, cx - s * 0.12f, cy + s * 0.35f, ink, w * 1.3f, z);
		C2D_DrawLine(cx - s * 0.18f, cy + s * 0.35f, ink, cx + s * 0.48f, cy - s * 0.38f, ink, w * 1.3f, z);
		break;
	case UI_ICON_CROSS:
		C2D_DrawLine(cx - s * 0.38f, cy - s * 0.38f, ink, cx + s * 0.38f, cy + s * 0.38f, ink, w * 1.3f, z);
		C2D_DrawLine(cx - s * 0.38f, cy + s * 0.38f, ink, cx + s * 0.38f, cy - s * 0.38f, ink, w * 1.3f, z);
		break;
	case UI_ICON_PLUS:
		C2D_DrawRectSolid(cx - s * 0.45f, cy - w * 0.65f, z, s * 0.9f, w * 1.3f, ink);
		C2D_DrawRectSolid(cx - w * 0.65f, cy - s * 0.45f, z, w * 1.3f, s * 0.9f, ink);
		break;
	case UI_ICON_TRASH:   // lid with a handle, a body with two slots
		C2D_DrawRectSolid(cx - s * 0.12f, cy - s * 0.5f, z, s * 0.24f, s * 0.1f, ink);
		C2D_DrawRectSolid(cx - s * 0.45f, cy - s * 0.4f, z, s * 0.9f, s * 0.12f, ink);
		C2D_DrawRectSolid(cx - s * 0.34f, cy - s * 0.22f, z, s * 0.68f, s * 0.7f, ink);
		C2D_DrawRectSolid(cx - s * 0.14f, cy - s * 0.12f, z + 0.001f, 1, s * 0.5f, bg);
		C2D_DrawRectSolid(cx + s * 0.12f, cy - s * 0.12f, z + 0.001f, 1, s * 0.5f, bg);
		break;
	case UI_ICON_SAVE:   // an arrow down into a tray
	case UI_ICON_LOAD:   // an arrow up out of it
		bar_vertical_arrow(cx, cy - s * 0.1f, s * 0.85f, ink, icon == UI_ICON_SAVE ? 1 : -1, z);
		C2D_DrawRectSolid(cx - s * 0.48f, cy + s * 0.42f, z, s * 0.96f, w, ink);
		break;
	case UI_ICON_UP:
		bar_vertical_arrow(cx, cy, s, ink, -1, z);
		break;
	case UI_ICON_OPEN:   // a chevron
		C2D_DrawLine(cx - s * 0.18f, cy - s * 0.42f, ink, cx + s * 0.24f, cy, ink, w * 1.3f, z);
		C2D_DrawLine(cx + s * 0.24f, cy, ink, cx - s * 0.18f, cy + s * 0.42f, ink, w * 1.3f, z);
		break;
	case UI_ICON_CHANGE:   // two small arrow heads apart
		C2D_DrawTriangle(cx - s * 0.5f, cy, ink, cx - s * 0.1f, cy - s * 0.36f, ink, cx - s * 0.1f, cy + s * 0.36f, ink, z);
		C2D_DrawTriangle(cx + s * 0.5f, cy, ink, cx + s * 0.1f, cy - s * 0.36f, ink, cx + s * 0.1f, cy + s * 0.36f, ink, z);
		break;
	case UI_ICON_TOGGLE:   // a switch: a capsule and its knob
		C2D_DrawCircleSolid(cx - s * 0.26f, cy, z, s * 0.26f, ink);
		C2D_DrawCircleSolid(cx + s * 0.26f, cy, z, s * 0.26f, ink);
		C2D_DrawRectSolid(cx - s * 0.26f, cy - s * 0.26f, z, s * 0.52f, s * 0.52f, ink);
		C2D_DrawCircleSolid(cx + s * 0.26f, cy, z + 0.001f, s * 0.17f, bg);
		break;
	case UI_ICON_DISC:
		C2D_DrawCircleSolid(cx, cy, z, s * 0.48f, ink);
		C2D_DrawCircleSolid(cx, cy, z + 0.001f, s * 0.13f, bg);
		break;
	case UI_ICON_EXIT:   // an open box and an arrow leaving it
		C2D_DrawRectSolid(cx - s * 0.48f, cy - s * 0.42f, z, w, s * 0.84f, ink);
		C2D_DrawRectSolid(cx - s * 0.48f, cy - s * 0.42f, z, s * 0.4f, w, ink);
		C2D_DrawRectSolid(cx - s * 0.48f, cy + s * 0.42f - w, z, s * 0.4f, w, ink);
		C2D_DrawRectSolid(cx - s * 0.18f, cy - w / 2, z, s * 0.4f, w, ink);
		C2D_DrawTriangle(cx + s * 0.5f, cy, ink, cx + s * 0.18f, cy - s * 0.3f, ink, cx + s * 0.18f, cy + s * 0.3f, ink, z);
		break;
	}
}

// buttons without their own icon or caption: B goes back, A confirms
static int bar_icon_of(const ui_button_t *p)
{
	return p->icon ? p->icon : p->key == KEY_B ? UI_ICON_BACK : p->key == KEY_A ? UI_ICON_CHECK : UI_ICON_NONE;
}

static const char *bar_caption(const ui_button_t *p)
{
	if (p->label)
		return p->label;
	switch (p->icon) {
	case UI_ICON_STAR: return "Favourite";
	case UI_ICON_SORT: return "Sort";
	case UI_ICON_RESCAN: return "Rescan";
	case UI_ICON_FOLDER: return "Folders";
	}
	return p->key == KEY_B ? "Back" : p->key == KEY_A ? "OK" : "";
}

// B first, A last, the others in the order given; every tab is the same width.
static int bar_layout(const ui_button_t *b, int n, int *order, float *x, float *w)
{
	int m = 0;
	for (int i = 0; i < n; i++) if (b[i].key == KEY_B) order[m++] = i;
	for (int i = 0; i < n; i++) if (b[i].key != KEY_B && b[i].key != KEY_A) order[m++] = i;
	for (int i = 0; i < n; i++) if (b[i].key == KEY_A) order[m++] = i;
	for (int i = 0; i < m; i++) {
		x[i] = 320.0f * i / m;
		w[i] = 320.0f * (i + 1) / m - x[i];
	}
	return m;
}

void ui_bar_draw(const ui_button_t *b, int n)
{
	if (n > UI_BAR_MAX) n = UI_BAR_MAX;
	// everything under the text, which ui_text_ex draws at 0.7
	const theme_t *th = theme();
	u32 strip = ui_bar_color(UI_COL_BAR);
	C2D_DrawRectSolid(0, BAR_TOP, 0.64f, 320, 240 - BAR_TOP, strip);
	int order[UI_BAR_MAX];
	float xs[UI_BAR_MAX], ws[UI_BAR_MAX];
	int m = bar_layout(b, n, order, xs, ws);
	u32 main_face = mix(th->hl_bottom, th->hl_top, 0.2f, 255);   // the mock's btnMainBottom
	u32 divider = theme_alpha(ui_bar_color(UI_COL_RULE), 110);
	u32 ink = theme_row(th, 0.6f, 255), ink_main = theme_row(th, 1, 255);
	if (flash_t > 0)
		flash_t--;
	for (int i = 0; i < m; i++) {
		const ui_button_t *p = &b[order[i]];
		float x = xs[i], w = ws[i], cx = x + w / 2;
		bool main = p->key == KEY_A, lit = flash_t > 0 && flash_key == p->key;
		u32 face = main ? main_face : strip;
		if (main)
			C2D_DrawRectSolid(x, BAR_TOP, 0.65f, w, 240 - BAR_TOP, main_face);
		else if (i)
			C2D_DrawRectSolid(x, BAR_TOP + 4, 0.65f, 1, 240 - BAR_TOP - 8, divider);
		if (lit)
			C2D_DrawRectSolid(x, BAR_TOP, 0.655f, w, 240 - BAR_TOP, mix(th->hl_top, 0xffffffff, 0.45f, 110 * flash_t / 9));
		u32 c = main || lit ? ink_main : ink;
		bar_icon(bar_icon_of(p), cx, BAR_ICON_Y, 11, c, face, p->on);
		// one short word; a long one first gets smaller, then shorter
		const char *caption = bar_caption(p);
		if (!caption[0])
			continue;
		float size = BAR_CAPTION, room = w - 6;
		float tw = text_width(size, caption);
		if (tw > room)
			size = fmaxf(BAR_CAPTION_MIN, size * room / tw);
		caption = ui_fit(caption, size, room);
		ui_text_ex(NULL, cx, BAR_CAPTION_Y - 20 * size, size, c, C2D_AlignCenter, caption);
	}
}

u32 ui_bar_input(const ui_button_t *b, int n, u32 keys)
{
	if (n > UI_BAR_MAX) n = UI_BAR_MAX;
	for (int i = 0; i < n; i++)
		if (keys & b[i].key) {
			flash_key = b[i].key;
			flash_t = 9;
		}
	if (!(keys & KEY_TOUCH))
		return keys;
	touchPosition p;
	hidTouchRead(&p);
	if (p.py < BAR_TOP || (!p.px && !p.py))
		return keys;
	keys &= ~KEY_TOUCH;   // a tap on the bar is never a tap on the list above it
	int order[UI_BAR_MAX];
	float xs[UI_BAR_MAX], ws[UI_BAR_MAX];
	int m = bar_layout(b, n, order, xs, ws);
	for (int i = 0; i < m; i++)
		if (p.px >= xs[i] && p.px < xs[i] + ws[i]) {   // tabs touch: exactly one
			keys |= b[order[i]].key;
			flash_key = b[order[i]].key;
			flash_t = 9;
		}
	return keys;
}

// "A open    B back    X add code" -> buttons. Parts are split at two or more
// spaces; D-pad parts ("Left/Right change") are dropped (rows are tapped), L/R
// becomes two arrow buttons. Labels point into a static copy of hint.
int ui_bar_parse(const char *hint, ui_button_t *out, int max)
{
	static char copies[4][160];
	static int next;
	char *s = copies[next++ & 3];
	snprintf(s, sizeof(copies[0]), "%s", hint);
	int n = 0;
	for (char *part = s; *part && n < max; ) {
		while (*part == ' ') part++;
		if (!*part) break;
		char *end = strstr(part, "  ");
		if (end) *end = 0;
		char *sp = strchr(part, ' ');
		char *label = sp ? sp + 1 : "";
		if (sp) *sp = 0;
		while (*label == ' ') label++;
		static const struct { const char *word; u32 key; } words[] = {
			{ "A", KEY_A }, { "B", KEY_B }, { "X", KEY_X }, { "Y", KEY_Y }, { "L", KEY_L }, { "R", KEY_R },
			{ "SELECT", KEY_SELECT }, { "START", KEY_START },
		};
		if (!strcmp(part, "L/R") && n + 1 < max) {
			out[n++] = (ui_button_t){ KEY_L, NULL, UI_ICON_LEFT, false };
			out[n++] = (ui_button_t){ KEY_R, NULL, UI_ICON_RIGHT, false };
		} else
			for (size_t w = 0; w < sizeof(words) / sizeof(words[0]); w++)
				if (!strcmp(part, words[w].word)) {
					if (*label >= 'a' && *label <= 'z') *label -= 32;
					out[n++] = (ui_button_t){ words[w].key, *label ? label : NULL, 0, false };
				}
		if (!end) break;
		part = end + 2;
	}
	return n;
}

// A short notice in the middle of the top screen (sorted, favourites, rescan).
void ui_notice(const char *s, int frames)
{
	if (frames <= 0 || !s || !s[0])
		return;
	int a = frames > 20 ? 255 : frames * 255 / 20;
	float w = text_width(0.48f, s) + 28, x = 200 - w / 2;
	// under the text (ui_text_ex draws at 0.7), over covers and titles
	C2D_DrawRectSolid(x, 104, 0.66f, w, 30, theme_alpha(ui_bar_color(UI_COL_NOTICE), a * 225 / 255));
	C2D_DrawRectSolid(x, 104, 0.665f, 3, 30, theme_alpha(theme()->accent[3], a));
	ui_text_ex(NULL, 200, 111, 0.48f, theme_alpha(theme()->header_title, a), C2D_AlignCenter, s);
}

// The first letter of a title for a placeholder card: a whole UTF-8 sequence,
// never half of one (out has room for 8 bytes).
void ui_first_letter(char *out, const char *title)
{
	size_t n = 1;
	unsigned char c = (unsigned char)title[0];
	if (c >= 0xf0) n = 4;
	else if (c >= 0xe0) n = 3;
	else if (c >= 0xc0) n = 2;
	size_t i = 0;
	for (; i < n && title[i]; i++)
		out[i] = title[i];
	out[i] = 0;
}
