# PSX3D

PSX3D is a standalone PlayStation emulator for Nintendo 3DS, built around the
PCSX-ReARMed emulation core. It runs without RetroArch. This project is a work
in progress.

<!-- TODO: Replace these references with the v0.3.0 screenshots when available. -->
<p align="center">
  <img src="docs/screenshots/library.png" alt="Game library" width="300">
  <img src="docs/screenshots/menu.png" alt="In-game menu" width="300">
</p>
<p align="center">
  <img src="docs/screenshots/crash.png" alt="Crash Bandicoot" width="400">
  <img src="docs/screenshots/silent-hill.png" alt="Silent Hill" width="400">
</p>

## Features

- GPU renderer by default, with the software renderer available as a fallback
- Per-game presets choose renderer, features, and touch controls separately for
  Old and New 3DS; uncurated games expose all options
- Game library with box art, cleaned-up titles, and multi-disc grouping
- Save states (10 slots), per-game memory cards, and disc swapping
- Display modes: 4:3, stretched, and widescreen in supported titles
- Remappable fast forward and rewind hotkeys (rewind requires New 3DS)
- Smooth audio when the game runs slowly (enabled by default)
- Detailed stats view: Off, FPS, or Detailed
- Reset settings to defaults globally or for the current game
- Large SD card support without scanning the whole card
- No DSP1 required; DSP firmware is copied from the system on first start

## Performance

Measurements below were made on 2026-10-17. FPS is rendering speed with
frameskip off. Game speed is the percentage of normal game speed with frameskip
on.

### Old 3DS

| Game | FPS, GPU | FPS, software | Game speed, GPU | Game speed, software |
|---|---:|---:|---:|---:|
| Crash Bandicoot | 40.0 | 24.2 | 84% | 65% |
| Gran Turismo 2 | 28.4 | 23.4 | 59% | 51% |
| Crash Team Racing | 25.0 | 20.7 | 55% | 51% |
| Ridge Racer Type 4 | 32.5 | 32.2 | 77% | 77% |
| Silent Hill | 29.8 | 28.5 | 69% | 68% |

### New 3DS

The software renderer reached 60 FPS in all five games. With the GPU renderer,
Crash Bandicoot and Gran Turismo 2 reached 60 FPS, Silent Hill about 58.5 FPS,
and Crash Team Racing about 56 FPS with the GPU worker on the second core.

## Presets and setup

Curated games start with a preset tuned for the console model. It selects the
renderer and available features, and shows touch controls only when the game
needs them. Uncurated games show all options. The goal is to require no setup
for ordinary play while leaving settings available when needed.

The bottom screen stays blank with its backlight off when no touch controls are
needed. On Old 3DS, games can show right stick and L2/R2/L3/R3 controls. New
3DS shows L3/R3 when the game uses stick clicks. Touch buttons can be remapped
per game and flipped left/right.

Fast forward and rewind are remappable. The defaults are SELECT+R for fast
forward and SELECT+L for rewind. Rewind is available only on New 3DS. In debug
settings, Show FPS can be Off, FPS, or Detailed. Smooth audio while the game is
running slowly is enabled by default. Settings can be reset globally or per
game.

## Requirements

- A 3DS or 2DS running Luma3DS. New 3DS models run most tested titles at or near
  full speed. Old 3DS models do not reach full speed in demanding 3D games;
  frameskip improves game speed.
- Game images in `.cue/.bin`, `.chd`, `.pbp`, `.iso`, or `.m3u` format. No games
  are included.
- Optional PlayStation BIOS image (for example `scph1001.bin`) in
  `sdmc:/3ds/PSX3D/bios/`. Without one, the core's HLE BIOS is used.

## Installation

1. Download `PSX3D.cia` from [Releases](https://github.com/BreadChaser/PSX3D/releases).
2. Install it with FBI or another CIA installer.
3. Place games in `sdmc:/roms/psx/`. Select another folder from the library
   with Y.

## Controls

| Context | Input | Action |
|---|---|---|
| Library | A | Start game |
| Library | Y | Select games folder |
| Library | SELECT | Rescan folder |
| Library | START | Exit |
| In game | A / B / X / Y | Circle / Cross / Triangle / Square |
| In game | L / R / ZL / ZR | L1 / R1 / L2 / R2 |
| In game | Circle Pad / C-Stick | D-pad or left analog / right analog |
| In game | SELECT+R / SELECT+L | Fast forward / rewind (remappable) |
| In game | Touch controls, when shown | Game-specific mapped buttons |
| In game | Touch bottom screen, when no controls shown | Open menu |

## Box art

Box art is downloaded from [libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
when a game is first displayed and cached in `sdmc:/3ds/PSX3D/covers/`. An
internet connection is needed only for the initial download. Custom art can be
added as a PNG in the same folder using the matching file name.

## File locations

| Path | Contents |
|---|---|
| `sdmc:/3ds/PSX3D/saves/` | Memory cards |
| `sdmc:/3ds/PSX3D/states/` | Save states |
| `sdmc:/3ds/PSX3D/covers/` | Box art cache |
| `sdmc:/3ds/PSX3D/bios/` | Optional BIOS |
| `sdmc:/3ds/PSX3D/config.ini` | Settings |
| `sdmc:/3ds/PSX3D/log.txt` | Most recent session log |

## Known issues

- The GPU renderer is not pixel-accurate. Some games still have drawing errors;
  one blend mode with mask checking is approximate.
- Display centering is not implemented, and interlaced modes are not handled.
- Demanding 3D games do not reach full speed on Old 3DS. Frameskip helps.
- Rewind requires New 3DS.
- Box art downloads are slow on the 3DS network stack. Heavy Wi-Fi activity can
  cause audio clicks.
- Some titles require a real BIOS to boot.
- On Old 3DS, the Continue screen shows an empty frame instead of its screenshot.

When reporting a bug, include `log.txt` from the session where it happened.

## Building

Requires [devkitPro](https://devkitpro.org/) with devkitARM, libctru, citro2d,
and citro3d. The GPU renderer is built by default; no renderer build flag is
needed. Both renderers are included in the application.

```bash
git clone --recursive https://github.com/BreadChaser/PSX3D.git
cd PSX3D
git -C core submodule update --init deps/libchdr deps/libretro-common
make        # psx3d/PSX3D.3dsx
make cia    # psx3d/PSX3D.cia; requires makerom and bannertool
```

## Credits

- [PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed): notaz, Exophase,
  senquack and the libretro contributors, based on PCSX, PCSX-Reloaded and the
  P.E.Op.S. plugins
- The authors of the PCSX-ReARMed 3DS port
- devkitPro: libctru, citro2d, citro3d
- [DSP1](https://github.com/zoogie/DSP1) by zoogie: the method for copying the DSP firmware
- Artwork and font credits: [tools/art/CREDITS.md](tools/art/CREDITS.md)

PSX3D is not affiliated with Sony, Nintendo, libretro or the PCSX-ReARMed developers.

## License

GPL-2.0, matching PCSX-ReARMed. See [LICENSE](LICENSE).
