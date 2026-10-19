<p align="center"><img src="psx3d/meta/banner.png" alt="PSX3D sunset and palms logo" width="384"></p>

<h1 align="center">PSX3D</h1>
<p align="center">Standalone PlayStation emulation for Nintendo 3DS.</p>

<p align="center">
  <a href="https://github.com/BreadChaser/PSX3D/releases/latest"><img src="https://img.shields.io/github/v/release/BreadChaser/PSX3D?color=ae68df" alt="Latest release"></a>
  <a href="https://github.com/BreadChaser/PSX3D/releases"><img src="https://img.shields.io/github/downloads/BreadChaser/PSX3D/total?color=e967aa" alt="Total downloads"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/licence-GPL--2.0-blue" alt="Licence: GPL-2.0"></a>
  <img src="https://img.shields.io/badge/platform-Nintendo%203DS-e60012" alt="Platform: Nintendo 3DS">
</p>

## What it is

PSX3D is a standalone PlayStation emulator for Old and New Nintendo 3DS by
**BreadChaser**. It combines PCSX-ReARMed with a renderer that draws on the 3DS
GPU, a box-art library and settings designed for the handheld.

**This is a work in progress.** The GPU renderer still has drawing errors and
missing effects. New 3DS runs the tested scenes at full speed; demanding games
remain slower on Old 3DS. See the [release notes](docs/release-notes-v0.4.0.md)
for current limits.

## Screenshots

Captured from v0.4.0 on real hardware.

<table>
  <tr>
    <td align="center"><img src="docs/screenshots/library.png" width="320" alt="Game library with downloaded box art"><br>Box-art library</td>
    <td align="center"><img src="docs/screenshots/menu.png" width="320" alt="Pause menu with save, load and settings actions"><br>Pause menu</td>
  </tr>
  <tr>
    <td align="center"><img src="docs/screenshots/settings.png" width="320" alt="Settings top level with categories"><br>Settings at a glance</td>
    <td align="center"><img src="docs/screenshots/settings-display.png" width="320" alt="Opened Display category with aspect ratio and 3D options"><br>Display settings</td>
  </tr>
</table>

<table>
  <tr>
    <td align="center"><img src="docs/screenshots/ctr.png" width="280" alt="Crash Team Racing gameplay"><br>Crash Team Racing</td>
    <td align="center"><img src="docs/screenshots/silent-hill.png" width="280" alt="Silent Hill gameplay"><br>Silent Hill</td>
    <td align="center"><img src="docs/screenshots/sotn.png" width="280" alt="Castlevania: Symphony of the Night gameplay"><br>Symphony of the Night</td>
  </tr>
</table>

<p align="center"><img src="docs/screenshots/touch-layout.png" width="320" alt="Old 3DS gameplay with bottom-screen touch controls"><br>Extra controls on the touch screen</p>

## Features

- GPU rendering on both Old and New 3DS, with automatic frameskip for slower games.
- Game presets with separate defaults for each console, plus per-game overrides.
- Box art, clean game titles, multi-disc grouping and support for large SD cards.
- Save states with picture previews, per-game memory cards, cheats and disc swapping.
- Remappable buttons, touch controls and fast-forward; optional rewind on New 3DS.
- Display modes, New 3DS screen-depth effects and smooth audio when games run slowly.
- Built-in HLE BIOS: no separate BIOS file required. DSP firmware is copied on first start.

## Performance

<picture><source media="(prefers-color-scheme: light)" srcset="docs/performance-light.svg"><img src="docs/performance-dark.svg" alt="PSX3D speed on Old 3DS and New 3DS" width="880"></picture>

Six saved scenes; 10-second warmup and 15-second sample.
[Methodology and limitations](docs/performance.md).

<details>
<summary>Exact numbers</summary>

Full speed is 60 emulated frames/s. Automatic frameskip shows fewer pictures
to improve game speed; the game itself may draw only 30 pictures/s.

<!-- perf-table -->
| Game | Old, off (FPS) | Old, auto (game speed) | Old, auto (frames shown) | New, off (FPS) | New, auto (game speed) |
|---|---:|---:|---:|---:|---:|
| Crash Bandicoot | 53.0 | 100.0% | 14 of 30/s | 60.0 | 100.0% |
| Crash Team Racing | 37.4 | 83.2% | 9 of 25/s | 60.0 | 100.0% |
| Gran Turismo 2 | 34.4 | 79.0% | 8 of 24/s | 60.0 | 100.0% |
| Silent Hill | 42.0 | 100.0% | 10 of 30/s | 60.0 | 100.0% |
| Ridge Racer Type 4 | 60.0 | 100.0% | 15 of 30/s | 60.0 | 100.0% |
| Symphony of the Night | 57.8 | 100.0% | 47 of 60/s | 60.0 | 100.0% |
<!-- /perf-table -->

</details>

These are measurements of individual scenes, not whole-game guarantees.

## Install

You need a 3DS or 2DS running Luma3DS and your own PlayStation game images.
New 3DS performs best. No games are included.

In **FBI → Remote Install → Scan QR Code**, scan the code below:

<p align="center"><img src="docs/qr-install.png" width="220" alt="QR code for installing the latest PSX3D CIA with FBI"></p>

Or download [**PSX3D.cia**](https://github.com/BreadChaser/PSX3D/releases/latest/download/PSX3D.cia)
and install it with FBI. A 3DSX is also available for Homebrew Launcher.

Put `.cue/.bin`, `.chd`, `.pbp`, `.iso` or `.m3u` games in `sdmc:/roms/psx/`,
or press **Y** in the library to choose a folder. Box art downloads automatically
from [libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
and is cached on the SD card. Internet access is only needed for downloads.

The built-in HLE BIOS is ready to use. An optional real BIOS, such as
`scph1001.bin`, goes in `sdmc:/3ds/PSX3D/bios/` and may help compatibility.
Memory cards and save states live under `sdmc:/3ds/PSX3D/`.

## Controls basics

| Where | Input | Action |
|---|---|---|
| Library | A / Y / SELECT / X | Start / choose folder / rescan / settings |
| In game | A / B / X / Y | Circle / Cross / Triangle / Square |
| In game | L / R / ZL / ZR | L1 / R1 / L2 / R2 |
| In game | Circle Pad / C-Stick | D-pad or left analog / right analog |
| In game | SELECT+R / SELECT+L | Fast-forward toggle / rewind hold, when available |
| In game | Touch MENU, or blank bottom screen | Pause menu |

Buttons and hotkeys can be remapped. Old 3DS supplies extra buttons and a right
stick on the touch screen. Rewind requires New 3DS and is enabled per game.

## Settings and presets

Press **X** in the library for global settings, or choose **Settings** in the
pause menu for the current game. Categories group Display, Performance, Sound,
Controls, Saves & general, and Advanced options.

Built-in presets choose defaults for recognised games on each console. Your
choices override those defaults; **Reset to defaults** restores them. Games
without a preset show every option supported by the console. Each setting
shows whether it affects one game or all games.

The bottom screen stays quiet when no touch controls are needed. Enable
**Show FPS** for a frame-rate readout or detailed statistics.
See the [settings reference](docs/settings-ui.md).

## Building from source

Install devkitPro/devkitARM, libctru, citro2d, citro3d, makerom and bannertool,
then configure the environment as described in [building and testing](docs/building.md).
The core and renderer dependencies are included.

```bash
git clone https://github.com/BreadChaser/PSX3D.git
cd PSX3D
make        # psx3d/PSX3D.3dsx
make cia    # psx3d/PSX3D.cia
```

Contributor tools: [remote testing](docs/dev-control.md) ·
[recording and replay](docs/recorder.md) ·
[updating performance measurements](docs/building.md#updating-the-performance-chart).

## Credits and licence


- **BreadChaser** — PSX3D author.
- [PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed): notaz, Exophase
  and libretro contributors; based on PCSX, PCSX-Reloaded and P.E.Op.S.
- The authors of the PCSX-ReARMed 3DS port.
- devkitPro: libctru, citro2d and citro3d.
- [DSP1](https://github.com/zoogie/DSP1) by zoogie: firmware-copying method.
- [Artwork and fonts](tools/art/CREDITS.md).

PSX3D is not affiliated with Sony, Nintendo, libretro or the PCSX-ReARMed developers.
GPL-2.0, matching PCSX-ReARMed. See [LICENSE](LICENSE).
