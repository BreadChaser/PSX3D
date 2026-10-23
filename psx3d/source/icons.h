// Menu icons: Dreamstale line icons (Flaticon, credited in tools/art/CREDITS.md),
// coloured by the menu theme. tools/icons/pack.py makes romfs:/icons/dreamstale.bin.
#pragma once
#include <stdbool.h>
#include <3ds.h>

// pictures, in tools/icons/icons.tsv order
enum { ICON_TOGETHER, ICON_SHARE, ICON_SETTINGS, ICON_HOST, ICON_JOIN, ICON_DISPLAY, ICON_CONTROLS,
	ICON_SAVES, ICON_ADVANCED, ICON_FOLDER, ICON_RESET, ICON_PICTURES };

// Where a selected picture moves: each slot draws itself on when it starts
// showing an icon (or shows again after a pause), then its glow breathes.
// ICON_STILL never moves.
enum { ICON_STILL = -1, ICON_SLOT_TOP, ICON_SLOT_TILE, ICON_SLOT_GROUP, ICON_SLOTS };

// Picture `id` centred at (cx, cy), `size` pixels across. lit: the theme's
// gradient, glow and motion (slot); otherwise one dim colour, still. Menus only:
// the pictures load on first use and icons_release() frees them before a game.
// False when the pictures are missing (the caller draws nothing or its own).
bool icons_draw(int slot, int id, float cx, float cy, float size, bool lit, float alpha, float z);
// A button bar glyph (UI_ICON_*, about 14 px, drawn 1:1) in one colour.
bool icons_glyph(int ui_icon, float cx, float cy, u32 ink, float z);
// Frees every texture and CPU buffer. A later menu reloads lazily.
void icons_release(void);
