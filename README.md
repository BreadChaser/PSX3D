# PSX3D

A standalone PlayStation 1 emulator for the **New Nintendo 3DS**, built on
[PCSX-ReARMed](https://github.com/libretro/pcsx_rearmed) but without RetroArch.

> **Status: early work in progress.** It builds, boots, and shows the game
> browser in the Azahar emulator. It has **not been tested on real hardware
> or with real games yet**. Expect bugs.

## What this is (and isn't)

- The emulation itself, including the CPU dynarec and the graphics and sound
  engines, is **PCSX-ReARMed**, unmodified, by notaz and the libretro
  contributors. PSX3D did not write the emulator.
- PSX3D is a new, small **3DS frontend** that hosts that core directly instead of
  going through RetroArch:
  - A game browser that reads one folder at a time and never scans the card for
    free space, aimed at large (e.g. 1TB) SD cards
  - Video copied to the screen by the 3DS GPU's DMA engine (no CPU pixel copying)
  - Audio through a single NDSP ring buffer with small automatic rate
    adjustment to stay in sync
  - New 3DS 804 MHz mode + L2 cache always on
  - An in-game menu: save/load states (10 slots), screen modes, smoothing, FPS
    counter, and all core options
- It is **not** affiliated with Sony, Nintendo, libretro, or the PCSX-ReARMed authors.

## Requirements

- New 3DS / New 2DS XL recommended. Old 3DS models will be too slow for most games.
- Custom firmware with **Luma3DS** (needed for the fast dynarec; without it,
  PSX3D falls back to a very slow interpreter)
- `sdmc:/3ds/dspfirm.cdc` for sound (dump it with DSP1)
- Your own game dumps (`.cue/.bin`, `.chd`, `.pbp`, `.iso`, `.m3u`, ...).
  None are provided.
- Optional: a PS1 BIOS (e.g. `scph1001.bin`) in `sdmc:/3ds/PSX3D/bios/`.
  Without one, the built-in HLE BIOS is used. BIOS files are not provided.

## Usage

Copy `PSX3D.3dsx` to `sdmc:/3ds/` and start it from the Homebrew Launcher.

| Where | Controls |
|---|---|
| Game list | A open, B up a folder, L/R page, START quit |
| In game | 3DS buttons map 1:1 to PS1 buttons (A=○, B=✕, X=△, Y=□), ZL/ZR = L2/R2, circle pad = d-pad/left stick, C-stick = right stick |
| In game | **Tap the bottom screen** for the menu |

Files live in `sdmc:/3ds/PSX3D/`: `saves/` holds memory cards (`.srm`),
`states/` holds save states, and `config.ini` and `log.txt` are also there.

## Building

Needs [devkitPro](https://devkitpro.org/) with devkitARM, libctru, citro3d and citro2d.

```bash
git clone --recursive https://github.com/BreadChaser/PSX3D.git
cd PSX3D
git -C core submodule update --init deps/libchdr deps/libretro-common
make
```

The output is `psx3d/PSX3D.3dsx`.

## License

GPL v2, the same as PCSX-ReARMed (see `LICENSE`). The core is included as a
git submodule pointing at the upstream libretro repository.

## Credits

- PCSX-ReARMed: notaz, Exophase, senquack, and the libretro contributors
- PCSX / PCSX-Reloaded and P.E.Op.S. plugin authors
- 3DS port work inside PCSX-ReARMed: Justin Weiss and others
- devkitPro, libctru, citro3d, citro2d
