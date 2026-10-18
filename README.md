# PSX3D

PSX3D is a standalone PlayStation emulator for Nintendo 3DS by **BreadChaser**,
built around our modified fork of PCSX-ReARMed. It runs without RetroArch.
This project is a work in progress.

**The first PS1 emulator for the 3DS that draws on the 3DS GPU.** On an Old 3DS
it runs games up to twice as fast as the PCSX-ReARMed software renderer used by
other 3DS PS1 emulators (Crash Bandicoot: 49.1 FPS on GPU vs 22.8 on Software),
and automatic frameskip brings games like Crash Bandicoot, Ridge Racer Type 4
and Symphony of the Night to full game speed on the original 3DS.

> **The GPU renderer is experimental and still has many issues.** Some games
> show drawing errors, missing effects or flicker, and it is not yet as
> accurate as the software renderer. The New 3DS uses the software renderer by
> default. If a game looks wrong on the Old 3DS, switch it to **Software** in
> the game's settings (Performance → Renderer), and please report it with the
> game name and `log.txt`.

<p align="center">
  <img src="docs/screenshots/library.png" alt="Old 3DS game library" width="300">
  <img src="docs/screenshots/menu.png" alt="In-game pause menu" width="300">
</p>
<p align="center">
  <img src="docs/screenshots/crash.png" alt="Crash Bandicoot gameplay" width="400">
  <img src="docs/screenshots/silent-hill.png" alt="Silent Hill gameplay" width="400">
</p>
<p align="center">
  <img src="docs/screenshots/settings-old.png" alt="Old 3DS settings" width="300">
  <img src="docs/screenshots/settings-new.png" alt="New 3DS settings" width="300">
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
- Ten save-state slots with picture previews, per-game memory cards, cheats and disc swapping.
- 4:3, stretched and widescreen display modes; stereo screen depth on New 3DS.
- Remappable controls, touch buttons and fast-forward/rewind hotkeys.
  **Old 3DS has no stereo 3D or rewind.**
- Smooth audio when slow, enabled by default on both consoles.
- Bottom-screen FPS readout; optional Detailed statistics.
- Global and per-game Reset to defaults.
- DSP firmware copied from the system on first start; DSP1 is not required.

## Performance

v0.3.1, measured on 2026-10-18 in the supplied benchmark scenes. Old 3DS had
BootNTR loaded with a low quality 30 / 128 KiB/s stream; New 3DS had no NTR.
Frameskip-off numbers are emulated frames per second. Full speed is 60 FPS,
or 100% game speed; automatic frameskip shows fewer pictures to improve speed.

| Game | Old GPU, off (FPS) | Old GPU, auto (game speed) | New Software, off (FPS) |
|---|---:|---:|---:|
| Ridge Racer Type 4 | 58.5 | 100.0% | 60.0 |
| Castlevania: Symphony of the Night | 54.7 | 100.0% | 60.0 |
| Crash Bandicoot | 49.1 | 100.0% | 60.0 |
| Silent Hill | 38.1 | 85.6% | 60.0 |
| Crash Team Racing | 31.0 | 65.9% | 60.0 |
| Gran Turismo 2 | 31.1 | 68.5% | 60.0 |

New 3DS Software also averaged 100% with automatic frameskip in all six scenes.
Symphony of the Night's default **Software** preset on Old 3DS measured 51.8 FPS
with frameskip off and 100.0% game speed with auto. Its New 3DS GPU spot check
averaged 60.0 FPS off and 100.0% with auto.

On the Old 3DS, automatic frameskip brings lighter games such as Crash
Bandicoot, Ridge Racer Type 4 and Symphony of the Night to full game speed by
showing fewer pictures. The heaviest 3D games still run below full speed.
Old 3DS audio underruns were zero with automatic frameskip in these samples;
forced frameskip off still underruns in some slow scenes.
The GPU renderer also looks sharper on the Old 3DS: it draws every line, where
the software renderer uses half-height 3D there to save time.

Details of how these were measured: [hardware check](docs/release-check-v0.3.1.md).
Changes: [v0.3.1 release notes](docs/release-notes-v0.3.1.md).

## Presets and settings

Games are recognised by their disc, and a built-in preset picks the best
settings for each game separately on Old and New 3DS. Games without a preset
show every option the console supports. Your own choices override the preset,
and **Reset to defaults** (global or per game) brings it back. In v0.3.1,
Castlevania: Symphony of the Night uses the software renderer on both consoles.

The library's **X** button opens the global settings; **Settings** in the pause
menu applies to the current game. Settings are one list grouped into Display,
Performance, Controls, Sound, General and Advanced.

The bottom screen shows only what a game needs: on the Old 3DS a right stick
and L2/R2/L3/R3, on the New 3DS stick-click buttons for games that use them
(left or right side). Every touch button can be remapped per game. With nothing
to show, the bottom screen turns off. **Show FPS** can display the frame rate in
the corner, or detailed statistics.

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

Hotkeys are remappable. Rewind needs a New 3DS and is turned on per game.

## Box art and files

Box art downloads from [libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
and is cached in `sdmc:/3ds/PSX3D/covers/`. Custom PNGs can use the matching
filename. Internet is needed only for initial downloads.

| Under `sdmc:/3ds/PSX3D/` | Contents |
|---|---|
| `saves/` | Memory cards |
| `states/` | Save states |
| `covers/` | Box art |
| `bios/` | Optional BIOS |
| `config.ini`, `games/` | Global settings and per-game overrides |
| `log.txt` | Most recent session log |

## Known issues

- **The GPU renderer still has many issues:** drawing errors, missing effects
  and flicker in some games; one mask-checking blend mode is approximate.
  Use the Software renderer for games that look wrong.
- Display centering and interlaced modes are not implemented.
- Demanding 3D games run below full speed on Old 3DS.
- Heavy Wi-Fi traffic and slow cover downloads can cause audio clicks.

When reporting a problem, include the affected session's `log.txt`.

## Building

Requires devkitPro/devkitARM, libctru, citro2d, citro3d, makerom and bannertool.
Both renderers and all core dependencies are included; no submodule, patch step
or renderer build flag is needed. Core changes are committed directly under
`core/`. See [core ownership and verification](docs/vendor-core.md).

```bash
git clone https://github.com/BreadChaser/PSX3D.git
cd PSX3D
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
