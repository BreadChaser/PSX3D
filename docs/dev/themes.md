# Animated menu themes

Settings > Display > Theme selects eight palettes with the approved round-3 animated
backgrounds. The setting is global and available from the library settings.

| Theme | Scene |
|---|---|
| Original (default) | Outrun sunset |
| Midnight teal | Star chart |
| PS1 grey | Quicksilver |
| Sunset magenta | Silk waves |
| High contrast | Signal grid |
| Phosphor | Fireflies and glyphs |
| Eclipse | Corona and orbiting glint |
| Ember | Rising embers |

`scene.c` draws both menu screens with Citro2D. Layer compositions, deterministic
seeds, colours and animation rates come from the approved mock's `js/backdrops.js`.
`tools/themes/import-scenes.py devtools/ui-mock` reproduces `scene_data.h`.
Only effects used by these eight scenes are compiled; the mock's unused proposals
(Shape drift, Haze, Motes) are not shipped. There is no intensity setting.

The engine uses a sine lookup table, a decay lookup table and one 128x128 A8 radial
gradient generated in code from the mock's gradient stops. This replaces expensive
triangle fans for glow sprites. Silk waves uses fewer strands and wider columns
on the Old 3DS bottom screen. Curves are sampled polylines/meshes; the vignette is
a coarse radial mesh. Citro2D's rasterization and these approximations can differ
slightly from the browser canvas, especially at thin glows.

`scene_release()` and `icons_release()` drain outstanding GPU work and release all
scene working memory, glow texture, text buffer and icon textures before a game
starts. There is no scene thread or gameplay callback. In-game settings draw a flat
palette background; they cannot allocate scene resources or the Dreamstale atlas.
Returning to the library loads menu resources lazily.

Menu colour roles live in `theme.c`/`theme.h`; per-scene layer colours live in the
generated theme-indexed table. `tools/themes/check-mock.py` compares all eight
palettes' 39 shared tokens, including icon and effect colours. QR codes retain
fixed black/white for scanning. Input controller artwork, gameplay OSD and stats
colours are **not themed yet** in the menu implementation commit; their later
colour pass requires its own layout and Old performance gate.

Dreamstale icons use selected-item draw-on and breathing glow. Unselected icons
are static. Source, packing and attribution are documented in
[icons.md](icons.md); the required credit is in `tools/art/CREDITS.md`.

Frontend pinning is mandatory. Compare against the frozen v0.5.1 rc9 ELF using
`tools/layout/compare.py` and `tools/layout/check-pinned.py`; also run the generic
`tools/layout/check.sh` growth proof. The old backdrop's archive entry points
remain forced link roots so replacing its image does not remove library objects
and move gameplay code. They are never called by the scene engine.

The private evidence/report is `devtools/themes-v06/REPORT.md`. Its gallery labels
Azahar and real-console images separately. Emulator timing is not an Old 3DS
performance result. Developer logs report CPU drawing time and draws/second over
five-second intervals; captures temporarily disturb cadence, so measure separate
intervals without screenshots.

The procedural no-cover disc's material colours and the icon draw-on tip also
live in the theme table. Material colours are shared defaults (including its
rainbow sheen), so missing box art stays recognizable across palettes. Their
roles can be overridden per palette without adding drawing-time literals.

### Menu submission

Scene primitives append complete vertices to the pinned Citro2D 1.7.0 buffers,
using the SDK shader and state transitions. `scene_batch.h` guards private ABI
sizes/offsets; `tools/build/toolchain.lock` pins the SDK archive and headers.
The ARM/QEMU scene-batch test compares vertex/index output against the actual
SDK triangle, rectangle, line, image and single-glyph functions. GPU writes are
stubbed in this test; physical timing and screenshots remain separate gates.
Phosphor caches system-font glyph metadata and its static grain at menu entry.
Ember groups additive tails, glows and cores to avoid per-particle shader changes.
All cached metadata and working buffers are released before gameplay.

Quicksilver retains all fibres and 30 path integration steps. Paths are cached
at 8 Hz, with vertex positions interpolated every frame. The host error check
compares those positions to exact integration for both screen sizes (180 time
samples up to ~51 minutes); the observed worst displacement is 0.025 pixels.
Fibre and Ember additive glows are grouped to avoid repeated shader switches.
