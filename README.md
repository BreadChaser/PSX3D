# PSX3D

PSX3D is a standalone PlayStation emulator for the New Nintendo 3DS. It runs the
[PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed) emulation core directly,
without RetroArch, behind a frontend written for the 3DS.

## Features

- Dynarec CPU core at 804 MHz with the L2 cache enabled; most titles run at full speed on New 3DS hardware
- Game library with box art, cleaned-up titles and multi-disc grouping
- Library loading that does not scan the whole SD card, so large cards (1 TB and up) work
- Save states (10 slots) and per-game memory cards
- Display modes: 4:3, stretched, and widescreen (renders a wider field of view in supported titles)
- Disc swapping for multi-disc games
- Bottom screen backlight turned off during play; touch the bottom screen to open the menu
- Safe handling of HOME, sleep and power-off at any point
- On-screen buttons for Old 3DS models (untested)
- No DSP1 needed: the DSP firmware is copied from the system on first start

## Requirements

- New 3DS, New 3DS XL or New 2DS XL running Luma3DS. Old 3DS, 3DS XL and 2DS
  support is included but untested, and demanding titles will not run at full speed.
- Game images in `.cue/.bin`, `.chd`, `.pbp`, `.iso` or `.m3u` format. No games are included.
- Optional: a PlayStation BIOS image (for example `scph1001.bin`) in
  `sdmc:/3ds/PSX3D/bios/`. Without one, the core's HLE BIOS is used.

## Installation

1. Download `PSX3D.cia` from [Releases](https://github.com/BreadChaser/PSX3D/releases).
2. Install it with FBI or another CIA installer.
3. Place your games in `sdmc:/roms/psx/`. A different folder can be selected from
   the library with Y.

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
| In game | Touch bottom screen | Open menu |

## Box art

Box art is downloaded from [libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
the first time a game is displayed and cached in `sdmc:/3ds/PSX3D/covers/`. An internet
connection is only required for the initial download. Custom art can be added as a PNG
in the same folder using the matching file name.

## File locations

| Path | Contents |
|---|---|
| `sdmc:/3ds/PSX3D/saves/` | Memory cards |
| `sdmc:/3ds/PSX3D/states/` | Save states |
| `sdmc:/3ds/PSX3D/covers/` | Box art cache |
| `sdmc:/3ds/PSX3D/bios/` | Optional BIOS |
| `sdmc:/3ds/PSX3D/config.ini` | Settings |
| `sdmc:/3ds/PSX3D/log.txt` | Log of the most recent session |

## Known issues

- Box art downloads are slow because of the 3DS network stack.
- Heavy Wi-Fi activity (for example NTR screen streaming) can cause audio clicks.
- Some titles require a real BIOS to boot.

When reporting a bug, please include `log.txt` from the session where it happened.

## Building

Requires [devkitPro](https://devkitpro.org/) with devkitARM, libctru, citro2d and citro3d.

```bash
git clone --recursive https://github.com/BreadChaser/PSX3D.git
cd PSX3D
git -C core submodule update --init deps/libchdr deps/libretro-common
make        # psx3d/PSX3D.3dsx
make cia    # psx3d/PSX3D.cia (requires makerom and bannertool)
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
