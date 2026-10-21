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
	ui_text_ex(libui_font(), 10, 6, 0.5f, C2D_Color32(235,238,255,255), 0, title);
	if (right)
		ui_text_ex(NULL, 310, 9, 0.42f, C2D_Color32(110,120,160,255), C2D_AlignRight, right);
	const unsigned colors[] = { 0xe61e3c, 0xfacd14, 0x1ebe78, 0x286ee6 };
	for (int i=0; i<4; i++)
		C2D_DrawRectSolid(10+i*18, 26, 0.2f, 16, 2,
			C2D_Color32(colors[i]>>16, colors[i]>>8, colors[i], 255));
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
	if (value == input_control_value) accent = input_control_color(sel);
	// highlight: soft glow, gradient bar, accent edge
	float hy = y0 + (f->hl - f->scroll) * row_h;
	int highlight_row = (int)floorf(f->hl + 0.5f);
	if (hy > y0 - row_h && hy < y0 + rows * row_h &&
	    !(header && highlight_row >= 0 && highlight_row < n && header(highlight_row, ud))) {
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
		if (header && header(i, ud)) {
			u8 alpha = (u8)(255 * edge);
			ui_text_ex(NULL, x + 12, ry + 4, 0.33f,
				C2D_Color32(125, 145, 190, alpha), 0, label(i, ud));
			const u32 colors[] = { 0xff3c1ee6, 0xff14cdfa, 0xff78be1e, 0xffe66e28 };
			for (int j = 0; j < 4; j++)
				C2D_DrawRectSolid(x + 12 + j * 10, ry + 19, 0.3f, 8, 1,
					(colors[j] & 0xffffff) | ((u32)alpha << 24));
			C2D_DrawRectSolid(x + 56, ry + 19, 0.3f, w - 68, 1,
				C2D_Color32(30, 42, 72, alpha));
			continue;
		}
		float lift = i < FLIST_MAX ? f->lift[i] : (i == sel);
		float bob = sinf(f->t * 1.6f + i * 0.9f) * (0.6f + 0.8f * lift);
		u8 c = (u8)(150 + 105 * lift), a = (u8)(255 * edge);
		u32 col = C2D_Color32(c, c, c + 30 > 255 ? 255 : c + 30, a);
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
					C2D_Color32(130, 160, 230, a));
			}
			if (v && v[0])
				ui_text_ex(NULL, x + w - 8, ry + (row_h - 15) / 2 + bob, 0.45f,
					C2D_Color32(130, 160, 230, a), C2D_AlignRight, v);
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
	C2D_DrawRectSolid(12 + dx, y0, 0.66f, 376, 236 - y0, C2D_Color32(6, 9, 24, 225));
	C2D_DrawRectSolid(12 + dx, y0, 0.67f, 3, 236 - y0, C2D_Color32(40, 110, 230, 255));
	float y = y0 + 6;
	if (title) {
		ui_text_ex(NULL, 24 + dx, y, 0.5f, C2D_Color32(235, 238, 255, 255), 0, title);
		y += 18;
	}
	if (impact) {
		ui_text_ex(NULL, 24 + dx, y, 0.42f, C2D_Color32(250, 205, 20, 255), 0, impact);
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

// Destructive settings actions require a fresh A press. B always cancels.
typedef struct { const char *title, *message, *action; } confirm_t;
static void confirm_draw(void *ud)
{
	confirm_t *c = ud;
	// Behind the header's colour rule, like the other menus.
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, C2D_Color32(6, 9, 24, 255));
	ui_menu_header(c->title, config_in_game() ? ui_fit(emu_game_title(), .42f, 150) : "All games");
	C2D_Text t;
	if (C2D_TextParse(&t, textbuf, c->message))
		C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, 16.0f, 62.0f, 0.7f, 0.5f, 0.5f,
			C2D_Color32(170, 185, 220, 255), 288.0f);
	ui_text_ex(NULL, 160, 183, 0.5f, C2D_Color32(235, 238, 255, 255), C2D_AlignCenter, c->action);
	ui_text_ex(NULL, 160, 213, 0.45f, C2D_Color32(130, 150, 190, 255), C2D_AlignCenter, "B  Cancel");
}

bool ui_confirm(const char *title, const char *message, bool show_game)
{
	return ui_confirm_action(title, message, "A  Reset to defaults", show_game);
}

bool ui_confirm_action(const char *title, const char *message, const char *action, bool show_game)
{
	confirm_t c = { title, message, action };
	bool released = false;
	while (aptMainLoop()) {
		input_poll();
		// The pause menu also keeps answering the remote loop while this is open.
		if ((settings_ui_active() || show_game) && (dev_tick(show_game) || dev_ui_pending())) return false;
		u32 held = input_keys_held(), k = input_keys_down();
		if (released && (k & KEY_B)) return false;
		if (released && (k & KEY_A)) return true;
		if (!(held & (KEY_A | KEY_B | KEY_TOUCH))) released = true;
		ui_begin();
		video_present_vsync(show_game, NULL, confirm_draw, &c);
	}
	return false;
}
