# PSX3D

PSX3D is a standalone PlayStation emulator for the New Nintendo 3DS. It runs the
[PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed) emulation core directly,
without RetroArch, behind a frontend written for the 3DS.

## Experimental GPU renderer (this branch)

The `gpu-renderer` branch draws PS1 graphics with the 3DS GPU instead of
the CPU (`core/plugins/gpu_pica`, a new renderer written for the PICA200 on
top of gpulib). PS1 VRAM is a 1024x512 RGBA5551 render target, texture pages
are decoded into a shared atlas on demand, and polygons are clipped to the
PS1 drawing area on the CPU before they reach the GPU.

Status: Crash Bandicoot renders correctly on New and Old 3DS. On an Old 3DS
it needs about 40% less CPU time per frame than the software renderer. Not
done yet: mask bit, texture windows, dithering, 24-bit movies through the
GPU, real 3D, and broad game testing. Expect glitches; the release builds on
`main` still use the software renderer.

Each build includes both renderers and starts with GPU rendering by default.
With `debug=1` in `sdmc:/3ds/PSX3D/config.ini`, open **Settings → Core options →
Renderer** and choose **GPU** or **Software**. Close and reopen the game to apply
the change. The library setting changes the default; the in-game setting is
remembered for that game. `renderer=sw` or `renderer=gpu` in `switches.txt`
overrides both settings for testing. See [the implementation report](docs/dual-renderer.md).

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

The former root `GPU=pica` / `GPU=unai` build modes have been replaced by the
runtime setting. After updating from a single-renderer build, run `make clean`
once. Core-only `CTR_GPU=pica` and `CTR_GPU=unai` builds remain available for
diagnostics. Host configuration regression: `tests/renderer_config.sh`.

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
