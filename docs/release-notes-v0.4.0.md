# PSX3D v0.4.0

## Highlights

- **GPU rendering on both Old and New 3DS**, with faster emulation and automatic frameskip.
- **A clearer settings menu** with categories, per-game overrides and reset confirmations.
- **Automatic box art** on both console models, with lower memory use.
- Built-in HLE BIOS: install, add your games and play without a separate BIOS file.

PSX3D remains a work in progress. The GPU renderer is experimental and still
has drawing errors and missing effects.

## Performance

All six tested New 3DS scenes run at full game speed with frameskip off or auto.
Most draw 30 new pictures per second; Symphony of the Night draws 60.
Old 3DS benefits from automatic frameskip, though demanding games still run
below full game speed. See the [Old/New performance chart and exact numbers](../README.md#performance)
for measured scenes and [methodology](performance.md).

## What changed

- GPU rendering is now the only renderer. Software rendering and its settings have been removed.
- Faster scratchpad access, indirect-block lookup and geometry calculations.
- Settings grouped into Display, Performance, Sound, Controls, Saves & general,
  and Advanced. Rows identify their scope; individual game overrides can be cleared.
- Box art loads automatically on Old 3DS. Smaller image allocations and cancelling
  downloads before gameplay leave more memory for games.
- Movie synchronization and alternating-buffer border clearing fixes.
- The included PCSX-ReARMed core builds directly from source without a separate patch step.
- Optional developer tools for input recording and deterministic replay.
- The FPS display reports new pictures shown per second alongside game speed.
  Detailed statistics label shown pictures, emulated vblanks and host loops separately.

## Known limits

- GPU rendering can show incorrect effects, flicker and other drawing errors.
  A thin blue line at the top of Crash Bandicoot's picture still reproduces.
- Stereoscopic 3D is temporarily unavailable in v0.4.0 (the 3D slider shows a flat picture); GPU 3D is in development.
- Rewind requires New 3DS.
- Widescreen behaviour is unchanged in this release; improvements are planned for v0.4.1.
- Display centering and interlaced modes are not implemented.
- Performance and compatibility vary by game. A real BIOS may help compatibility.
- Automatic frameskip can skip more pictures than necessary, reducing visible
  smoothness even when game speed reaches 100%.
- Heavy Wi-Fi traffic and slow cover downloads can cause audio clicks.

## Install

Scan this code in **FBI → Remote Install → Scan QR Code**, or download
[**PSX3D.cia**](https://github.com/BreadChaser/PSX3D/releases/latest/download/PSX3D.cia)
and install it with FBI. A 3DSX is available for Homebrew Launcher.

![FBI remote install QR code](https://raw.githubusercontent.com/BreadChaser/PSX3D/main/docs/qr-install.png)

Put your games in `sdmc:/roms/psx/`, or choose a folder with **Y** in the library.
Existing settings and saves are retained; obsolete renderer settings are ignored.

Credits and GPL-2.0 licensing: [README](../README.md#credits-and-licence) and [LICENSE](../LICENSE).
