# Menu themes

Settings > Display > Theme (library only, one look for the whole app) picks one of five
palettes. They are the palettes of the UI mock-up, token for token: `Original (v0.5.0)`,
`Midnight teal`, `PS1 grey`, `Sunset magenta`, `High contrast`. Original is the default and
the look the menus always had. Every alternative overrides only some of the Original's
tokens. The console adds a few fields of its own (cards, wordmark, backdrop; see below), so the
top-screen backdrop changes with the palette too: the sunset picture is recoloured through the
palette's colour matrix and the wire floor uses its grid colours.

- Table: `psx3d/source/theme.c`; API: `psx3d/source/theme.h`.
- Config: `theme=<id>` in config.ini (`original`, `midnight-teal`, `ps1-grey`, `sunset-magenta`,
  `high-contrast`). The default is never written, so a config without the key is the default.
- The Settings row changes `g_cfg.theme` and every menu reads `theme()` each frame, so the
  new colours show at once.
- `tools/themes/check-mock.py` compares the table with the mock-up's theme code
  (`devtools/ui-mock/js/theme.js`): names, order and all 34 values per palette. The mock may
  list more palettes (proposals) after the five shipped ones; those are not checked.

## API

```c
const theme_t *th = theme();          // the active palette; th->menu_bg, th->accent[i] ...
int theme_count(void); const theme_t *theme_get(int i);   // for lists
u32 theme_alpha(u32 c, int a);        // c with alpha a
u32 theme_row(const theme_t *t, float lift, int a);   // row text colour, lift 0 (idle) .. 1 (selected)
```

Colours are `u32` in `C2D_Color32` layout (`TH_RGB(r,g,b)`, `TH_RGBA(r,g,b,a)` build them).
Colours the palettes never change stay literals where they are drawn; `theme.h` names the
ones a new screen is likely to need (`TH_CARD_TOP`, `TH_COVER_SHADOW`, ...).

## Tokens and where they are used

| Field (mock token) | Used for |
|---|---|
| `menu_bg` (menuBg) | menu pages on the bottom screen, plain top screens (browser, confirm) |
| `header_title`, `header_right` (headerTitle, headerRight) | `ui_menu_header` title and scope text; browser "Games folder" label; confirm action line |
| `accent[4]` (accent0..3) | rule under page titles, highlight edge, section rule dots, cover accents, save state frames |
| `hl_glow`, `hl_top`, `hl_bottom` | highlight bar of `flist_draw_sections` |
| `row_dim`, `row_lit` (rowDim, rowLit) | row text, idle to selected (`theme_row`) |
| `value` | right-hand value column |
| `section`, `section_rule` | section headings and their rule |
| `muted`, `white` | tips, footers, browser lines; the "Same as all games" line |
| `hint_lib`, `hint_menu` (hintLib, hintMenu) | library footer; pause menu footer |
| `message`, `error` | notices (library, settings, browser, state picker confirm), failures |
| `scrollbar` | library scroll indicator |
| `help_bg`, `help_bar`, `help_title`, `help_impact`, `help_text` | top-screen help panel; `help_impact` also the "tested settings" line |
| `slow_bg`, `slow_text` | "fast mode is off" banner in the library |
| `game_title`, `game_sub` | library title and second line; pause menu game name; Continue prompt lines |
| `pause_veil` | veil over the paused game |
| `touch_face`, `touch_border` | kept for the mock-up; the in-game touch panel (input.c) keeps its v0.5.0 colours in every theme, so its per-frame code stays where it was |

Console-only fields (not in the mock's palettes):

| Field | Used for |
|---|---|
| `card_top`, `card_bottom`, `card_ink` | cover placeholder cards (library, main menu), unselected main menu tiles and the Play together box, picture placeholders and icon holes |
| `card_stack` | the cards stacked behind a series' cover (each card further back 8 levels darker) |
| `version_ink` | the version in the top screen's corner |
| `logo`, `logo_3d` | "PSX" and "3D" of the small wordmark on the main menu |
| `folder` | the games folder line on the folder browser's top screen |
| `sky[3][4]` | colour matrix for the sunset picture: each output channel is `(row[0]·r + row[1]·g + row[2]·b) / 256 + row[3]`, clamped. The Original's is the identity, so the picture is drawn as stored. The hills under the floor are filled with the picture's floor colour (16,2,44) through the same matrix. |
| `grid_near`, `grid_side`, `grid_far` | wire floor: the middle near the viewer, the sides, toward the horizon (the Original's 150,60,255 / 40,90,255 / 255,70,200) |

The backdrop picture is recoloured in place (`libui.c` `sky_sync`) the first time a menu frame
draws it after the theme changed: a pass over the 512x256 RGB565 texture, then a cache flush. When
it holds another theme's colours it is loaded again from romfs first. Menu frames begin only after
the previous frame has finished on the GPU, and the sky is the first thing a frame draws, so
nothing reads the texture while it changes. Nothing of this runs in a game.

Not themed (constant in the mock-up too): touch accents and symbol colours (`ui_input_color`,
shared with the in-game touch panel), `brandInk`, `osdText`, shadows, the save state picker's veil
and cards (`savestates.c`, `TH_SLOT_*`). Fixed on purpose and marked `// fixed:` where drawn:
dimming toward black, black fades, the QR code's black on white.

## Differences from the mock-up while the console has the v0.5.0 layout

The mock-up also shows the new main menu, library button bar, favourite stars, letter jump and
notice strip. Their derived tokens (`barBg`, `btnTop`, `favStar`, ...) are computed from each
palette in the mock (`barTokensFor`). The console has none of these screens yet; the ui-impl
branch builds them on this table and should derive the same values from `menu_bg`, `hl_top`,
`hl_bottom`, `row_dim`, `row_lit`, `help_impact`, `header_title`.

Row text mid-animation uses the mock's per-channel blend (150,150,180 to 255,255,255); v0.5.0 raised
the blue channel by 30 until it saturated, so rows differ by a few levels while they slide.
