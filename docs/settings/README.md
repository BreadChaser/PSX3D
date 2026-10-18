# Settings cleanup

Based on `10a5ac8` (`t3/fps-in-layout`). Frontend only; no console access or push.

## Before and after

Previously, library settings had a single scrolling list of 14 entries. The
paused game's Settings page mixed display, performance, controls, sound, resets
and diagnostics in another long list. Renderer appeared again in Core options.

Both now open the same six groups. A opens a group; B returns to the selected
group, then to the library or pause menu. Left/Right changes values; A also
changes values or opens a child page. Every ordinary group fits on one screen.

| Page | Library defaults | In-game settings |
| --- | --- | --- |
| Display | Screen mode, Smoothing, New 3DS 3D | Same |
| Performance | Renderer, Software-only Half-height 3D (faster), Show FPS | Same, plus Rewind when the console/preset permits |
| Controls | Controls..., New 3DS Bottom-screen buttons | Same; side choice also hidden for blank layouts and Detailed stats |
| Sound | Smooth audio when slow, Menu sounds, Library music | Same |
| General | Continue where I left off, Reset to defaults | Same; reset removes this game's overrides |
| Advanced | Core options | Core options; audio recording/test tone only with debug=1 |

The pause menu remains Resume, Save state, Load state, Swap disc (multi-disc
only), Cheats, Settings, Reset game, Exit to library. It has seven entries for
single-disc games and eight for multi-disc games. Diagnostics stay in Advanced.

## Renamed and hidden frontend choices

| Before | After / visibility | Reason |
| --- | --- | --- |
| Screen (default) / Screen | Screen mode | Same plain name in both menus; header/help explains scope |
| 3D | New 3DS only | Old 3DS forces `stereo=0` on defaults/load/reset/sync; its core stereo option also always returns disabled, even with a test override |
| screen depth / real (exp.) | Screen depth / Full 3D (test) | Full 3D remains debug-only and Software-only; GPU shows effective Screen depth for an existing full-3D preference |
| 3D resolution: full / half (faster) | Half-height 3D (faster): Off / On, under Performance | Software-only polygon shortcut, unrelated to the 3D slider; hidden for GPU |
| Stick-click buttons: Right / Left | Bottom-screen buttons: Right side / Left side | New 3DS layout mirrors touch buttons, Menu, logo and FPS corner; Old 3DS ignores this setting |
| Controls | Controls... inside the Controls group | Distinguishes the remapping page from its group |
| Reset to global defaults (in game) | Reset to defaults | Help explicitly says it restores the game preset/library defaults and keeps saves/cheats/shared settings |
| Renderer in Core options | Removed duplicate | Renderer lives only in Performance |
| Raw core stereo / widescreen (if registered) | Hidden | Frontend owns 3D and Screen mode; prevents duplicate controls |
| Debug: record audio / Debug: test tone | Advanced, debug=1 only | Troubleshooting, not normal player settings |

`Half-height 3D` is verified in `core/plugins/gpu_unai/gpulib_if.cpp`: it is
applied to polygon commands (`0x20..0x3f`). GPU rendering never consults it.
The frontend stereo field controls both menu/game second-eye rendering, so
forcing it off also prevents Old 3DS from drawing stereo UI. No separate stereo
quality control exists; the old resolution setting is not one, so none was
invented. New 3DS keeps its existing 3D modes.

The side setting is explicitly guarded by `g_new3ds` in `input.c` (button
positions, Menu/logo, stats tap corner) and `statsview.c` (FPS position). Old 3DS
mirrors none of these. In-game it is hidden when neither controls nor FPS are
visible, and in Detailed view, which has its own fixed layout. Library settings
keep it on New 3DS because they set the shared layout preference for other games.

## Advanced decision and inventory

Core options remain available without debug=1: BIOS selection, memory cards,
region, drawing timing, frame skipping and accuracy fixes can make a game boot,
restore missing effects, or correct a bad speed/compatibility choice. Hiding the
whole page would obstruct those fixes. Risky speed shortcuts keep their help
and warnings, with plain names. No emulation defaults were changed here, apart
from disabling stereo on Old 3DS.

The normal defaults expose 24 Old/GPU, 29 Old/Software, 23 New/GPU and 28
New/Software core choices. See [the generated exact inventory](core-inventory.tsv).
Conditional choices add rows when relevant. New visibility rules:

- Software drawing choices (older software drawing, transparency, lighting,
  faster lighting) and picture positioning are hidden for GPU.
- Manual sideways/up/down/height adjustments appear only for Manual position.
- The sound-buffer threshold appears only for Audio limit frame skipping.
- The skip limit disappears when frame skipping is Off.
- Extra core messages require debug=1.

Existing locks and exclusions still hide unused renderer plugins, lightguns,
rumble, multitap, mouse/steering controls, thread choices and redundant FPS
controls. Existing preset gates still decide rewind and remapping actions;
Old 3DS cannot enable rewind. Current preset metadata has no arbitrary
per-setting compatibility locks, so no untested game-specific restrictions
were added. In-game visibility uses the **running** renderer; a pending renderer
change takes effect after reopening the game. Library visibility uses the chosen
default renderer.

Core labels now read as follows; stored keys and values are unchanged:

| Core description before | Player name after |
| --- | --- |
| Region | Game region |
| BIOS Selection | BIOS |
| Memory Card 1 Type (Restart) | Memory card 1 |
| Memory Card 2 Type | Memory card 2 |
| CD read-ahead | Read disc ahead |
| PSX CPU Clock Speed (%) | Game CPU speed |
| Dithering Pattern | PS1 colour shading |
| Frameskip | Skip frames when slow |
| Frameskip Threshold (%) | Start skipping below |
| Frameskip Interval | Frames to skip at most |
| Display Informational Notifications | Debug: core messages |
| Framebuffer readout | Picture timing |
| (GPU) Slow linked list processing | Careful drawing order |
| (GPU) Screen centering | Picture position |
| (GPU) Manual position X | Move picture sideways |
| (GPU) Manual position Y | Move picture up or down |
| (GPU) Manual height adjustment | Adjust picture height |
| (GPU) Old renderer | Older software drawing |
| (GPU) Texture Blending | Transparency effects |
| (GPU) Lighting Effects | Lighting effects |
| (GPU) Fast Lighting | Faster lighting |
| Audio Reverb Effects | Sound echo |
| CD Audio | Disc music |
| XA Decoding | Disc voices and effects |
| Analog Axis Bounds | Stick response |
| DualShock Analog Mode Toggle Key Combo | Analog mode shortcut |
| Instruction Cache Emulation | Accurate CPU cache |
| Exception and Breakpoint Emulation | Accurate CPU errors |
| Turbo CD | Faster disc loading |
| Disable Automatic Compatibility Hacks | Disable automatic game fixes |
| (Hack) Disable Pipeline Delays | Simplify CPU loads |
| (Speed Hack) Disable SMC Checks | Skip code safety checks |
| (Speed Hack) Assume GTE Registers Unneeded | Faster 3D calculations |
| (Speed Hack) Disable GTE Flags | Simpler 3D clipping |
| Disable CPU/GTE Stalls | Simpler CPU timing |

Common displayed values are now On/Off/Auto, Audio limit/Always, Manual,
Game-controlled/No borders, Early/Late, Built-in, Game save/Per disc/Shared, and
Square/Round. These are labels only. Core value strings such as `enabled`,
`disabled`, `auto_threshold`, `fixed_interval` and `libretro` remain intact.
Long core values are no longer silently cut to 14 characters. Full labels and
all values in the normal inventory pass the host font-spacing check.

## Configuration compatibility

No keys were renamed and no migration is required. `stereo`, `half3d`,
`touch_click_left`, `renderer`, `stretch`, `smooth`, `show_fps`, `audio_stretch`,
`sounds`, `music`, `autosave`, `core.*`, mapping and hotkey keys still load/save
in their existing format. An Old 3DS ignores an old stereo enable and saves
`stereo=0`. Existing `half3d` and side preferences are preserved while hidden.

Setting scopes are unchanged: renderer, screen mode, remapping and rewind can
belong to the game. Other frontend choices and core options remain shared.
In-game help identifies shared settings. Per-game reset clears game overrides;
global reset keeps per-game overrides, saves, cheats and library paths.

## Offline screenshots and validation

These use the production menu tables, visibility filters and drawing functions
through the existing bottom-screen host renderer. The title font is Michroma;
the system font is approximated with DejaVu Sans. They are **offline mockups**,
not hardware screenshots. Individual images cover every group, both models and
both menu contexts, with Old/Software alternatives and Core options samples.

| Context | Old 3DS | New 3DS |
| --- | --- | --- |
| Library groups | ![Old global](old-global-Settings.png) | ![New global](new-global-Settings.png) |
| In-game groups | ![Old game](old-game-Settings.png) | ![New game](new-game-Settings.png) |
| Library Display | ![Old display](old-global-Display.png) | ![New display](new-global-Display.png) |
| In-game Display | ![Old display](old-game-Display.png) | ![New display](new-game-Display.png) |
| Library Performance | ![Old performance](old-global-Performance.png) | ![New performance](new-global-Performance.png) |
| In-game Performance | ![Old performance](old-game-Performance.png) | ![New performance](new-game-Performance.png) |

[Old Software library](old-global-Software.png) ·
[Old Software in game](old-game-Software.png) ·
[Interactive gallery](index.html)

Checks:

- `python3 tests/settings/run.py`: ASan/UBSan; production menu filters and
  drawing, all four console/renderer combinations, preset restrictions,
  blank/Detailed/FPS side visibility, disc gating, debug gating, conditional
  core rows, main selection restoration, full-label/value spacing, mockups.
- `bash tests/renderer_config.sh`: default renderers, single renderer control,
  legacy stereo/half-height/side values, Old stereo lock including debug/test
  override, New stereo persistence, per-game renderer overrides and resets.
- `bash psx3d/tests/host/run.sh`: existing controls, presets, hotkeys, reset
  confirmation, touch layouts and FPS tests under ASan/UBSan.
- `source /home/tony/Development/PS1_3ds/devtools/env.sh && make -j16`.
- `make cia` with the supplied bannertool and makerom paths.

Both packages build. Existing path-truncation/unused-variable and executable-stack
warnings remain. No hardware interaction or runtime hardware validation was done.
