# PSX3D

PSX3D is a standalone PlayStation emulator for the Nintendo 3DS. It runs the
[PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed) emulation core directly,
without RetroArch, behind a frontend written for the 3DS.

<p align="center">
  <img src="docs/screenshots/library.png" alt="Game library" width="300">
  <img src="docs/screenshots/menu.png" alt="In-game menu" width="300">
</p>
<p align="center">
  <img src="docs/screenshots/crash.png" alt="Crash Bandicoot" width="400">
  <img src="docs/screenshots/silent-hill.png" alt="Silent Hill" width="400">
</p>

## Features

- Dynarec CPU core at 804 MHz with the L2 cache enabled; most titles run at full speed on New 3DS hardware
- Game library with box art, cleaned-up titles and multi-disc grouping
- Library loading that does not scan the whole SD card, so large cards (1 TB and up) work
- Save states (10 slots) and per-game memory cards
- Display modes: 4:3, stretched, and widescreen (renders a wider field of view in supported titles)
- Disc swapping for multi-disc games
- Bottom screen backlight turned off during play; touch the bottom screen to open the menu
- Safe handling of HOME, sleep and power-off at any point
- Old 3DS support: faster default settings and on-screen L2/R2 buttons
- No DSP1 needed: the DSP firmware is copied from the system on first start

## Requirements

- Any 3DS or 2DS model running Luma3DS. On a New 3DS, New 3DS XL or New 2DS XL
  most titles run at full speed. On an Old 3DS, 3DS XL or 2DS, 2D games run at
  full speed and 3D games run slower.
- Game images in `.cue/.bin`, `.chd`, `.pbp`, `.iso` or `.m3u` format. No games are included.
- Optional: a PlayStation BIOS image (for example `scph1001.bin`) in
  `sdmc:/3ds/PSX3D/bios/`. Without one, the core's HLE BIOS is used.

## Old 3DS

The Old 3DS, 3DS XL and 2DS have a 268 MHz CPU (the New 3DS runs at 804 MHz
with an extra cache), so expect the following:

- Demanding 3D games do not reach full speed, and some may not be playable.
- Defaults are tuned for speed and differ from the New 3DS:
  - Automatic frameskip is on, so motion is less smooth when the game falls behind.
  - 3D scenes are drawn at half vertical resolution.
  - Sound uses simple interpolation and no reverb.
  - GTE speed hacks are on, which can cause rare graphical glitches.

  All of these can be changed in Settings → Core options.
- Rewind is not available.
- There are no ZL/ZR buttons; L2 and R2 are on-screen buttons on the touch screen.

## Renderer choice

Software is the default on both Old and New 3DS. **Settings → Core options →
Renderer** offers **Software** and **GPU (experimental)** in the library and
in-game menu. The library choice is global; the in-game choice is saved for
that game. Close and reopen the game to apply it. `renderer=sw|gpu` in
`switches.txt` overrides the choice for testing.

The experimental GPU renderer can be faster, but still draws some games
incorrectly. It is included for opt-in testing. No curated game profile enables
it yet. Software keeps Old 3DS half-height 3D and New 3DS threaded rendering.
See [the release integration report](docs/release-v0.3.0.md) for validation,
known limits, and the hardware checklist.

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

When a game runs slowly, **Smooth audio when slow** keeps sound flowing by
slowing it down with the game, including its pitch. It is on by default on both
3DS models and can be changed in the library or in-game Settings menu. At full
speed it leaves sound unchanged apart from the usual small clock-drift correction.
`audio_stretch=50` in `config.ini` allows up to 50% slowdown; `0` disables it,
and values from 1 to 50 set a smaller limit. The menu's on setting selects 50.
Long stalls and games running below half speed can still exhaust the queue;
the pitch changes gradually, so allow several seconds to hear it settle.

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
- Experimental GPU rendering can produce incorrect pictures; its save-state thumbnails can be blank or stale.
- On an Old 3DS, the Continue screen can show an empty frame instead of a screenshot.

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
