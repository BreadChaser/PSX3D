# PSX3D v0.3.0

This release makes the 3DS GPU renderer the default. The software renderer
remains available as a fallback. Built-in presets choose the renderer and
features per game and separately for Old and New 3DS; games without a preset
show all options. The goal is a useful default without per-game setup.

## What's new

- Game-specific touch controls, with remappable buttons and left/right flip.
  Old 3DS can show right stick and L2/R2/L3/R3; New 3DS shows L3/R3 for games
  that use stick clicks. The bottom screen stays blank and its backlight off
  when controls are not needed.
- Remappable fast forward and rewind. Defaults: SELECT+R and SELECT+L.
  Rewind requires New 3DS.
- Smooth audio when the game runs slowly, enabled by default.
- Show FPS modes: Off, FPS, Detailed.
- Reset to defaults globally or for an individual game.

## Performance

Measured 2026-10-17. Old 3DS FPS is measured with frameskip off; game speed is
measured with frameskip on.

| Game | Old 3DS GPU FPS | Old 3DS software FPS | GPU game speed | Software game speed |
|---|---:|---:|---:|---:|
| Crash Bandicoot | 40.0 | 24.2 | 84% | 65% |
| Gran Turismo 2 | 28.4 | 23.4 | 59% | 51% |
| Crash Team Racing | 25.0 | 20.7 | 55% | 51% |
| Ridge Racer Type 4 | 32.5 | 32.2 | 77% | 77% |
| Silent Hill | 29.8 | 28.5 | 69% | 68% |

On New 3DS the software renderer reached 60 FPS in all five games. The GPU
renderer reached 60 in Crash Bandicoot and Gran Turismo 2, about 58.5 in Silent
Hill, and about 56 in Crash Team Racing with the GPU worker on the second core.

## Known issues

- The GPU renderer is not pixel-accurate. Some games have drawing errors, and
  one blend mode with mask checking is approximate.
- Display centering is not implemented; interlaced modes are not handled.
- Demanding 3D games run below full speed on Old 3DS. Frameskip helps.
- Rewind requires New 3DS.

## Upgrade notes

The settings keys used by this release are `show_fps=0/1/2`, `map_TOUCH_*` and
`touch_slots` for touch mappings, `hotkey_ff` and `hotkey_rewind` for hotkeys,
and `touch_click_left` for the left/right touch layout. Existing configuration
files with older key names may not retain those particular settings; use the
in-app defaults/reset controls if mappings or hotkeys do not match expectations.

Save states and memory cards are stored separately from these settings. This
release is intended to keep existing saves and states compatible, but state
compatibility across builds is not guaranteed. Keep a backup before updating.
