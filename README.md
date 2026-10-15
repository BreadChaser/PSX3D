# PSX3D

A PlayStation 1 emulator for the New 3DS that you install like any other app.
No RetroArch, no core menus, just your games.

I wanted a PS1 emulator that actually worked on my 3DS. RetroArch's PS1 core kept
crashing on my 1TB SD card, and when it did run, it never felt like it belonged on
the system. PSX3D takes PCSX-ReARMed, which already runs PS1 games really well on
ARM, and gives it a proper 3DS home.

## Features

- **Game library**: your games show up with cover art and clean titles. Multi-disc
  games are grouped into one entry, so Final Fantasy VII is one game, not three.
- **Full speed on New 3DS**: uses the 804MHz mode, the L2 cache and the dynarec.
  Most games run at a steady 60.
- **Big SD cards**: never scans the whole card, so 1TB cards are fine.
- **Dark bottom screen**: goes dark while you play. Tap it to open the menu.
- **Save states**: 10 slots, plus normal memory card saves.
- **Screen modes**: 4:3, stretched, or widescreen (the game renders more of the
  scene instead of stretching, in games where it works).
- **Disc swapping**: switch discs from the menu.
- **HOME and sleep**: work at any time, including while covers are downloading.

## What you need

- A **New 3DS / New 3DS XL / New 2DS XL** with custom firmware (Luma3DS). An Old
  3DS will start it and has on-screen buttons, but heavy 3D games will be slow.
- `sdmc:/3ds/dspfirm.cdc` for sound. If you've never made it, run
  [DSP1](https://github.com/zoogie/DSP1) once.
- Your own PS1 game dumps: `.cue/.bin`, `.chd`, `.pbp`, `.iso`, `.m3u`.
- Optional: a PS1 BIOS such as `scph1001.bin` in `sdmc:/3ds/PSX3D/bios/`. Without
  one, a built-in replacement BIOS is used, which works for most games.

## Installing

Grab `PSX3D.cia` from the [releases page](https://github.com/BreadChaser/PSX3D/releases)
and install it with FBI.

Put your games in `sdmc:/roms/psx/` (or `sdmc:/roms/`). PSX3D looks there first.
If they're somewhere else, press Y in the library to pick the folder.

## Controls

| Where | Button | Does |
|---|---|---|
| Library | A | Play |
| Library | Y | Choose the games folder |
| Library | SELECT | Rescan the folder |
| Library | START | Quit |
| In game | A / B / X / Y | ○ / ✕ / △ / □ |
| In game | L / R, ZL / ZR | L1 / R1, L2 / R2 |
| In game | Circle pad / C-stick | D-pad or left stick / right stick |
| In game | Tap the bottom screen | Menu: states, discs, screen, options, exit |

## Cover art

Covers come from the [libretro thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
project the first time a game shows up, so you'll need Wi-Fi for that. After
that they're cached on your SD card. The 3DS's network is slow, so each new
cover takes a few seconds. To use your own cover, put a PNG in
`sdmc:/3ds/PSX3D/covers/` with the same name as the downloaded one.

## Files

Everything lives in `sdmc:/3ds/PSX3D/`:

- `saves/`: memory cards
- `states/`: save states
- `covers/`: cover art cache
- `config.ini`: settings
- `log.txt`: what happened last run, handy for bug reports

## Known issues

- Wi-Fi traffic (like streaming the screen with NTR) can cause small clicks in
  the audio. That comes from the 3DS itself, not the emulator.
- A few games still need a real BIOS. If a game hangs on boot, try one.

Found a bug? Open an issue and attach `log.txt` if you can.

## Building

You'll need [devkitPro](https://devkitpro.org/) with devkitARM, libctru,
citro2d and citro3d.

```bash
git clone --recursive https://github.com/BreadChaser/PSX3D.git
cd PSX3D
git -C core submodule update --init deps/libchdr deps/libretro-common
make
```

That gives you `psx3d/PSX3D.3dsx`. `make cia` builds the CIA (needs `makerom`
and `bannertool` on your PATH).

## Credits

PSX3D is a frontend. The emulator itself is
[PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed), by notaz, Exophase,
senquack and the libretro contributors, built on PCSX, PCSX-Reloaded and the
P.E.Op.S. plugins. Thanks as well to the people who did the 3DS port of PCSX-ReARMed,
and to devkitPro for libctru, citro2d and citro3d.

Fonts and artwork are credited in `tools/art/CREDITS.md`.

PSX3D isn't affiliated with Sony, Nintendo, libretro or the PCSX-ReARMed authors.

## License

GPL v2, same as PCSX-ReARMed. See `LICENSE`.
