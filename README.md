# PSX3D

PSX3D is a standalone PlayStation emulator for Nintendo 3DS by **BreadChaser**,
built around PCSX-ReARMed. It runs without RetroArch. This project is a work
in progress.

<p align="center">
  <img src="docs/screenshots/library.png" alt="Old 3DS game library" width="300">
  <img src="docs/screenshots/menu.png" alt="In-game pause menu" width="300">
</p>
<p align="center">
  <img src="docs/screenshots/crash.png" alt="Crash Bandicoot gameplay" width="400">
  <img src="docs/screenshots/silent-hill.png" alt="Silent Hill gameplay" width="400">
</p>
<p align="center">
  <img src="docs/screenshots/touch-layout.png" alt="Old 3DS bottom-screen touch controls" width="320">
</p>

## Features

- **Old 3DS defaults to GPU (experimental); New 3DS defaults to Software.**
  Both renderers are included, with global and per-game selection.
- Per-console game presets choose defaults and available features. Uncurated
  games expose all options supported by the console.
- Library with box art, clean titles, multi-disc grouping and large SD support
  without scanning the entire card.
- Ten save-state slots, per-game memory cards, cheats and disc swapping.
- 4:3, stretched and widescreen display modes; stereo screen depth on New 3DS.
- Remappable controls, touch buttons and fast-forward/rewind hotkeys.
  **Old 3DS has no stereo 3D or rewind.**
- Smooth audio when slow, enabled by default on both consoles.
- Bottom-screen FPS readout; optional Detailed statistics.
- Global and per-game Reset to defaults.
- DSP firmware copied from the system on first start; DSP1 is not required.

## Performance

Measured without screen streaming: both consoles were rebooted without BootNTR,
with no NTR viewer requests and passive UDP monitoring during the session.
All figures below use the same release CIA, normal PS1 clock, 5 s warmup and
20 s sampling target (19–20 complete one-second intervals per run).
Frameskip is off unless marked automatic. Old 3DS uses slot 2;
New 3DS uses slot 0. These are short saved-scene checks, not whole-game guarantees;
the fight states differ between consoles. Software retains its Old 3DS
half-height polygon rendering.

FPS means host-frame throughput, not unique pictures or internal animation rate.
NTSC full speed is about 60 host frames/s. Automatic frameskip improves game
speed by drawing fewer pictures.

| Game | Old GPU FPS | Old Software FPS | New Software FPS |
|---|---:|---:|---:|
| Crash Bandicoot | 47.77 | 22.76 | 59.99 |
| Crash Team Racing | 30.02 | 20.59 | 59.99 |
| Gran Turismo 2 | 28.11 | 21.50 | 60.00 |
| Silent Hill | 35.01 | 28.64 | 60.03 |
| Ridge Racer Type 4 | 53.65 | 32.00 | 60.02 |
| Castlevania: SotN (Dracula fight) | 53.47 | 56.01 | 60.00 |

Old 3DS defaults to GPU (experimental), except the SotN Software preset;
New 3DS defaults to Software. Both Old renderer columns were measured back to
back in this session. Earlier results with NTR streaming are excluded.

### Castlevania: Symphony of the Night

| Console | GPU, retained regions | GPU, single square | Software | GPU, auto frameskip | Software, auto frameskip |
|---|---:|---:|---:|---:|---:|
| Old 3DS | 53.47 | 13.90 | 56.01 | 60.00 | 60.00 |
| New 3DS | 60.00 | 31.02 | 60.00 | 60.00 | 60.00 |

Menu measurements use spare slot 8, with the same warmup/sample durations.

| Console / SotN menu | Automatic GPU | Old RTT/cache | Software |
|---|---:|---:|---:|
| Old 3DS | 60.00 | 36.81 | 52.45 |
| New 3DS | 60.00 | 60.00 | 60.00 |

GPU now runs close to Software in the tested Dracula fight. **Software remains the
preset on both consoles for v0.3.0**, as it has the proven picture. Retained
regions (`rttcache=new`) remove repeated texture decoding; the diagnostic
single-square cache (`rttcache=old`) remains available. The title/menu RTT fix
also removes the white rectangles seen in earlier captures. Sparse HUD/menu
screenshots do not establish accuracy throughout the game.

R4 measured 53.65 FPS with automatic RTT and 33.41 with both old paths
(+60.58%). The gain persists without streaming, so the earlier R4 jump was not
solely a streaming artifact.

See the [complete hardware check](docs/release-check-v0.3.0.md) for same-binary RTT comparisons,
RC02 screening flags, save/load checks and picture limits.

## Presets and settings

Castlevania: Symphony of the Night uses **Software on both consoles** for v0.3.0.
The updated GPU now runs close to Software in the tested fight, but Software retains
the proven picture. The preset covers verified USA, Europe, Japan/Asia and Japanese reissue serials:
`SLUS00067`, `SLES00524`, `SCPS45196`, `SLPM86023`, `SLPM86073`, `SLPM87328`.
Serial identities were checked against the Redump PlayStation DAT in
libretro-database; hardware measurements use the USA edition.

Presets are selected by the disc serial, separately for Old and New 3DS.
Explicit per-game choices override presets; a game reset drops its overrides
and returns to its preset/global defaults. Global reset restores the console's
defaults. The library's **X** button opens global settings; **Settings** in the
pause menu applies to the current game. Renderer changes require reopening it.

Settings form one scrolling list grouped as **Display, Performance, Controls,
Sound, General, Advanced**. **Renderer** and **Show FPS** are under Performance;
**Smooth audio when slow** is under Sound; **Reset to defaults** is under General.
Advanced contains **Core options...**, filtered to the active renderer/console.
Show FPS cycles **Off / FPS / Detailed**. FPS shares the bottom touch layout;
Detailed replaces it with statistics and a MENU button. The game image stays
clear of debug overlays.

Old 3DS can use its full bottom screen for L2/R2/L3/R3 and a right stick.
New 3DS can show stick-click buttons where needed. Touch controls can be
remapped per game, and New 3DS side buttons can be flipped left/right. The
bottom screen is blank with its backlight off when no controls or readout are
needed. Uncurated games retain the full available control set.

## Requirements

- A 3DS or 2DS running Luma3DS. New 3DS performs best; demanding Old 3DS games
  still run below full speed, even with frameskip.
- Your game images: `.cue/.bin`, `.chd`, `.pbp`, `.iso` or `.m3u`. No games included.
- Optional BIOS (for example `scph1001.bin`) in `sdmc:/3ds/PSX3D/bios/`.
  The core uses its HLE BIOS otherwise; some games need a real BIOS.

## Install

In **FBI → Remote Install → Scan QR Code**, scan this code to install the latest
release. It always points to the release asset named **PSX3D.cia**.

![FBI remote install QR code](docs/qr-install.png)

Alternatively, download [PSX3D.cia](https://github.com/BreadChaser/PSX3D/releases/latest/download/PSX3D.cia)
and install it with FBI. Place games in `sdmc:/roms/psx/`, or press **Y** in the
library to choose another folder. A 3DSX is also available for Homebrew Launcher.

## Controls

| Context | Input | Action |
|---|---|---|
| Library | A / Y / SELECT / X | Start / choose folder / rescan / settings |
| Library | START | Exit |
| In game | A / B / X / Y | Circle / Cross / Triangle / Square |
| In game | L / R / ZL / ZR | L1 / R1 / L2 / R2 |
| In game | Circle Pad / C-Stick | D-pad or left analog / right analog |
| In game | SELECT+R / SELECT+L | Fast-forward toggle / rewind hold, when available |
| In game | Touch MENU, or blank bottom screen | Pause menu |
| In game | Touch controls | Mapped game buttons |

Hotkeys are remappable; rewind requires New 3DS and enabling it for that game.

## Box art and files

Box art downloads from [libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
and is cached in `sdmc:/3ds/PSX3D/covers/`. Custom PNGs can use the matching
filename. Internet is needed only for initial downloads. Developer remote
sessions suppress cover loading to reserve network resources.

| Under `sdmc:/3ds/PSX3D/` | Contents |
|---|---|
| `saves/` | Memory cards |
| `states/` | Save states |
| `covers/` | Box art |
| `bios/` | Optional BIOS |
| `config.ini`, `games/` | Global settings and per-game overrides |
| `log.txt` | Most recent session log |

## Known issues

- GPU rendering remains experimental and can differ from PlayStation output;
  one mask-checking blend mode is approximate.
- Display centering and interlaced modes are not implemented.
- Demanding 3D games run below full speed on Old 3DS.
- Save-state thumbnails can be blank or stale, especially on Old 3DS.
- Heavy Wi-Fi traffic and slow cover downloads can cause audio clicks.

When reporting a problem, include the affected session's `log.txt`.

## Building

Requires devkitPro/devkitARM, libctru, citro2d, citro3d, makerom and bannertool.
Both renderers are included; no renderer build flag is needed.

```bash
git clone --recursive https://github.com/BreadChaser/PSX3D.git
cd PSX3D
git submodule update --init core
git -C core submodule update --init deps/libchdr deps/libretro-common
make patch
make        # psx3d/PSX3D.3dsx
make cia    # psx3d/PSX3D.cia
```

## Credits

- **BreadChaser** — PSX3D author.
- [PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed): notaz, Exophase,
  senquack and libretro contributors; based on PCSX, PCSX-Reloaded and P.E.Op.S.
- The authors of the PCSX-ReARMed 3DS port.
- devkitPro: libctru, citro2d and citro3d.
- [DSP1](https://github.com/zoogie/DSP1) by zoogie: firmware-copying method.
- [Artwork and fonts](tools/art/CREDITS.md).

PSX3D is not affiliated with Sony, Nintendo, libretro or the PCSX-ReARMed developers.
GPL-2.0, matching PCSX-ReARMed. See [LICENSE](LICENSE).
