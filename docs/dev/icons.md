# Menu icons

The menus draw line icons by Dreamstale (Flaticon), coloured by the menu theme. Design:
`devtools/ui-mock/ICONS-DESIGN.md` (owner picks 2026-10-22: motion style 1, draw-on and a
breathing glow; Host = arcade console, Join = arrow into a box, Advanced = chip; no picture
inside a settings category).

## Where

| Place | Icons | Look |
|---|---|---|
| Main menu tiles (bottom) | Share PSX3D, Settings; Host, Join and the Play together header when multiplayer is on | selected: theme gradient, glow, draw-on; others one dim colour at 90 % |
| Main menu top picture | the selected tile's icon (Share PSX3D, Settings, Host, Join); Continue and Library keep real pictures | gradient, glow, draw-on |
| Settings list (main menu and library, not in a game) | Display, Controls, Saves, Advanced, Games folder, Reset: the selected row's picture with its name and tip on the top screen | gradient, glow, draw-on |
| Button bars (every menu, the pause menu too) | the bar set: thick strokes, 16 px, drawn 1:1 | one colour (the bar ink); a favourite star is gold |

Continue's play mark, the Library tile's cards and the empty-library card stay drawn in code.

## Files

- `tools/icons/icons.tsv`: every icon (slot, Flaticon id, drawing). Pictures first, then the bar
  glyphs in `UI_ICON_*` order.
- `tools/icons/fetch.sh DIR` downloads the 512 px originals (never committed);
  `tools/icons/pack.py DIR` writes `psx3d/romfs/icons/dreamstale.bin` (about 512 KB):
  per picture a 128, 64 and 32 px mask (the small ones with slightly thickened strokes), the
  stroke order of each (for the draw-on: the mock's `strokeOrder()`), and a blurred 64 px glow;
  per bar glyph a 16 px mask with thick strokes. `--preview DIR` writes a contact sheet.
- `psx3d/source/icons.c`, `icons.h`.

## How it is drawn

- Masks are `GPU_A8` textures drawn with Citro2D's image tint at blend 1: colour from the tint,
  alpha from the mask. The gradient is the tint's four corners (`icon_a` top-left, `icon_b`
  bottom-right, their mix on the other two). Recolouring for a theme costs nothing.
- Size picks the mask: over 72 px the 128 one (one texture per picture), over 36 px the 64 one,
  else the 32 one (both in one 512x256 atlas with the glows, so a screen of tiles is one texture).
- Glow: the blurred mask at 1.625 x the icon's size in `icon_glow`, added to the screen
  (`GPU_ONE` blend, then citro2d's normal blend again), under the icon. Strength
  `icon_glow.a x (0.5 + 0.18 sin(2 pi t / 3.6 s) + 0.5 x (1 - draw-on))`.
- Draw-on: a slot (`ICON_SLOT_TOP`, `_TILE`, `_GROUP`) restarts when it shows another icon or
  was not drawn for 0.15 s. For 0.8 s (ease-out cubic) the CPU fills the slot's own small
  textures: mask x ramp(stroke order passed by the pen), and the pen tip (a bump just behind the
  front) drawn on top in white. One table lookup per texel; afterwards the static mask is used.
  The cache is written back with `svcStoreProcessDataCache` (no GSP IPC).
- Memory: pictures load on first use in a menu (about 300 KB linear + 290 KB heap) and
  `icons_release()` frees them before every game (`main.c` `start_game`); nothing of them
  exists in a game. The bar glyphs (8 KB) stay loaded because the pause menu has bars.
  If the file is missing, pictures are left out and bars use the old drawn shapes.
- With the developer remote on, the log gets `icons: X ms per frame, worst Y ms` every 5 s
  (CPU time in `icons.c`).
- `icons.o` links late (`LATE_OFILES`); no new C library functions; per-frame frontend objects
  keep their pinned page offsets (`tools/layout/compare.py` and a per-function page-offset check).

## Licence

Flaticon free licence with attribution: "Icons by Dreamstale from Flaticon" in the app
(Share PSX3D screen), README credits and `tools/art/CREDITS.md`. The icons are not GPL. Only
the icons the app draws ship, as masks; never the pack or the originals.
