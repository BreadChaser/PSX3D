<p align="center"><img src="psx3d/meta/banner.png" alt="PSX3D" width="384"></p>

<h1 align="center">PSX3D</h1>
<p align="center">A standalone PlayStation emulator for Nintendo 3DS.</p>

<p align="center">
  <a href="https://github.com/BreadChaser/PSX3D/releases/latest"><img src="https://img.shields.io/github/v/release/BreadChaser/PSX3D?color=ae68df" alt="Latest release"></a>
  <a href="https://github.com/BreadChaser/PSX3D/releases"><img src="https://img.shields.io/github/downloads/BreadChaser/PSX3D/total?color=e967aa" alt="Downloads"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/licence-GPL--2.0-blue" alt="GPL-2.0"></a>
  <img src="https://img.shields.io/badge/platform-Nintendo%203DS-e60012" alt="Nintendo 3DS">
</p>

PSX3D runs PlayStation games on Old and New 3DS with GPU rendering, a box-art
library and tested settings for each game. It's built on PCSX-ReARMed and needs
no BIOS file.

> **Work in progress.** Expect some drawing errors. Demanding games run slower on
> Old 3DS; see [Performance](#performance).

## Screenshots

<table>
  <tr>
    <td align="center"><img src="docs/screenshots/library.png" width="300" alt="Box-art library"><br>Library</td>
    <td align="center"><img src="docs/screenshots/menu.png" width="300" alt="Pause menu"><br>Pause menu</td>
  </tr>
  <tr>
    <td align="center"><img src="docs/screenshots/settings.png" width="300" alt="Settings"><br>Settings</td>
    <td align="center"><img src="docs/screenshots/settings-display.png" width="300" alt="Display settings"><br>Display settings</td>
  </tr>
</table>

<p align="center"><img src="docs/screenshots/touch-layout.png" width="300" alt="Old 3DS touch controls"><br>Old 3DS touch controls</p>

<table>
  <tr>
    <td align="center"><img src="docs/screenshots/ctr.png" width="280" alt="Crash Team Racing"><br>Crash Team Racing</td>
    <td align="center"><img src="docs/screenshots/silent-hill.png" width="280" alt="Silent Hill"><br>Silent Hill</td>
    <td align="center"><img src="docs/screenshots/sotn.png" width="280" alt="Symphony of the Night"><br>Symphony of the Night</td>
  </tr>
</table>

## Features

- GPU rendering on Old and New 3DS, with smart frameskip
- Tested settings for each game and each console, applied automatically
- Real widescreen for games with a tested widescreen fix
- A main menu with Continue, a box-art library with recently played games,
  favourites, sorting and a letter jump
- Sharp box art, clean titles and multi-disc grouping
- Save states with previews, memory cards with backups, cheats and disc swapping
- Save backups: the last ten automatic saves and dated memory card copies per game, and Undo in the pause menu
- "Continue where I left off": your place is saved when you quit, on sleep and every few minutes
- Remappable buttons and touch controls, fast forward in every game
- Built-in BIOS: no BIOS file needed
- European discs with LibCrypt protection (some PAL releases) need a matching `.sbi` file next to the disc image, see [docs/presets.md](docs/presets.md#regions)

### New 3DS extras

- **Real 3D** (experimental): raise the 3D slider and each object gets its own
  depth, drawn from the game's own 3D data.
- **Rewind**: on by default; hold the rewind button to go back a few seconds.
- **Faster PS1 processor** for games that slowed down on a real PlayStation
  (Crash Team Racing, Gran Turismo 2, Silent Hill, Need for Speed: High Stakes),
  so busy scenes stay smooth.

### Settings

The menu holds what is a matter of taste: screen shape, smoothing, the FPS
counter, controls and saves. Technical options live under **Advanced**. Each
tested game comes with recommended values for its console, marked **P**, and
*Reset to recommended* brings them back. Everything else is set for you.

## Performance

<picture><source media="(prefers-color-scheme: light)" srcset="docs/performance-light.svg"><img src="docs/performance-dark.svg" alt="PSX3D performance" width="880"></picture>

<details>
<summary>Exact numbers</summary>

Old 3DS, heaviest tested scene of each game. Speed: 100% = full speed. Most PS1
3D games run at 30 FPS at most; Symphony of the Night, Tekken 3 and Klonoa at 60.
See [methodology](docs/performance.md).

<!-- perf-table -->
| Game (Old 3DS) | Frameskip off: speed / FPS | Auto frameskip: speed / FPS |
|---|---:|---:|
| Crash Bandicoot | 88.3% / 21.0 | 97.9% / 16.0 |
| Crash Bandicoot 2 | 76.0% / 23.1 | 92.4% / 16.3 |
| Crash Team Racing | 78.8% / 23.5 | 89.7% / 15.2 |
| Gran Turismo 2 | 53.8% / 14.1 | 54.3% / 12.7 |
| Ridge Racer Type 4 | 82.5% / 25.2 | 90.2% / 18.2 |
| Need for Speed: High Stakes | 35.8% / 11.5 | 36.0% / 10.7 |
| Tekken 3 | 66.8% / 40.6 | 92.1% / 15.2 |
| Final Fantasy VII | 100.4% / 15.2 | 100.2% / 15.3 |
| Klonoa | 88.3% / 53.6 | 99.8% / 15.1 |
| Silent Hill (alley) | 74.0% / 22.6 | 86.4% / 15.2 |
| Silent Hill (café) | 73.2% / 17.3 | 77.2% / 15.2 |
| Symphony of the Night | 98.6% / 59.6 | 100.0% / 53.0 |
<!-- /perf-table -->

</details>

**New 3DS:** every game in the table runs at full speed in its heaviest tested
scene with Auto frameskip. With frameskip off, Gran Turismo 2 and Need for Speed
races run at 94–98%, and the opening area of Metal Gear Solid at about 80%.

## Install

You need a 3DS or 2DS with Luma3DS and your own game images. Luma3DS is what
lets PSX3D use its fast mode; without it, or in a PC emulator such as Azahar,
PSX3D still starts but runs much slower.

In **FBI → Remote Install → Scan QR Code**, scan:

<p align="center"><img src="docs/qr-install.png" width="200" alt="QR code for the latest PSX3D.cia"></p>

Or install [**PSX3D.cia**](https://github.com/BreadChaser/PSX3D/releases/latest/download/PSX3D.cia)
with FBI (a 3DSX for Homebrew Launcher is also on the release page).

Put games (`.cue/.bin`, `.chd`, `.pbp`, `.iso`, `.m3u`) in `sdmc:/roms/psx/`; PSX3D also
finds them in other usual folders, or pick one in **Settings → Games folder**. Box art downloads automatically, even for
files with other names; your own picture goes in `sdmc:/3ds/PSX3D/covers/`
([how](docs/box-art.md)).

Got a real BIOS? Copy it to `sdmc:/3ds/PSX3D/bios/` and PSX3D uses it by itself
(`scph5500`, `scph5501`, `scph5502`, `psxonpsp660`, `scph101`, `scph7001` or `scph1001`, all `.bin`).

## Controls

| Where | Input | Action |
|---|---|---|
| Main menu | A / X | Open / settings |
| Library | A / X / Y | Play / sort / favourite |
| Library | L / R | Jump to the previous / next letter |
| In game | A / B / X / Y | Circle / Cross / Triangle / Square |
| In game | L / R / ZL / ZR | L1 / R1 / L2 / R2 |
| In game | Circle Pad / C-Stick | D-pad or left stick / right stick |
| In game | Touch MENU | Pause menu |
| In game | SELECT + R | Fast forward on/off |
| In game | Hold SELECT + L | Rewind (New 3DS) |

Everything can be remapped. Old 3DS gets extra buttons and a right stick on the
touch screen.

## Saves and logs

Everything lives in `sdmc:/3ds/PSX3D/`: `saves/` (memory cards), `states/` (save states and Continue), `backups/` (save backups), `config.ini` and `games/` (settings).

If something breaks, `log.txt` there says what happened (older sessions are `log.1.txt` to `log.4.txt`). Attach it to your issue.

## Known issues

- Some games show drawing glitches, flicker or thin cracks at polygon edges.
- Games with LibCrypt protection (some PAL discs) need a `.sbi` file next to the disc image.
- `.ccd` images can't be played; use `.cue/.bin` or `.chd`.
- Gran Turismo 1 hangs and Mega Man Legends 2 goes black after the intro with the built-in BIOS; a real BIOS fixes the first one.

## Building

Needs devkitPro (devkitARM, libctru, citro2d, citro3d), makerom and bannertool.
See [building](docs/building.md).

```bash
git clone https://github.com/BreadChaser/PSX3D.git
cd PSX3D
make        # psx3d/PSX3D.3dsx
make cia    # psx3d/PSX3D.cia
```

## Credits and licence

- **BreadChaser**: PSX3D
- [PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed): notaz, Exophase and
  libretro contributors; based on PCSX, PCSX-Reloaded and P.E.Op.S.
- The PCSX-ReARMed 3DS port authors; devkitPro (libctru, citro2d, citro3d)
- [DSP1](https://github.com/zoogie/DSP1) by zoogie; [artwork, fonts and cheat sources](tools/art/CREDITS.md)

Not affiliated with Sony, Nintendo or libretro. GPL-2.0, see [LICENSE](LICENSE).
