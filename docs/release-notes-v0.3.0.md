# PSX3D v0.3.0

PSX3D by **BreadChaser** now includes both renderers. **Old 3DS defaults to GPU
(experimental); New 3DS defaults to Software.** Global/per-game settings and
per-console presets can select a different renderer.

## What's new

- Automatic RTT detection keeps ordinary games on the original fast path.
  Precise render-to-texture dependencies and retained texture regions reduce
  avoidable GPU synchronization and decoding. SotN title/menu white-block
  corruption is fixed in the tested scenes.
- Full-screen Old 3DS touch layout, remappable buttons and hotkeys, and per-game
  control presets. New 3DS side buttons can be flipped left/right.
- Bottom-screen FPS readout; Off / FPS / Detailed modes.
- Scrolling settings grouped as Display, Performance, Controls, Sound, General,
  Advanced. Renderer and Show FPS are in Performance; Core options are in Advanced.
- Smooth audio when slow is on by default on both consoles.
- Global/per-game Reset to defaults. Uncurated games show all supported options.
- Rewind and stereo 3D are available only on New 3DS. Fast-forward availability
  follows presets; default hotkeys are SELECT+R / SELECT+L.
- An opt-in LAN developer test/update loop, inactive in ordinary use and requiring
  `remote token=...` in the SD switches file for network access and self-update.

Castlevania: Symphony of the Night uses **Software on both consoles** for v0.3.0.
The updated GPU now runs close to Software in the tested fight, but Software retains
the proven picture. The preset covers verified USA, Europe, Japan/Asia and Japanese reissue serials:
`SLUS00067`, `SLES00524`, `SCPS45196`, `SLPM86023`, `SLPM86073`, `SLPM87328`.
Serial identities were checked against the Redump PlayStation DAT in
libretro-database; hardware measurements use the USA edition.

## Install

Use **FBI → Remote Install → Scan QR Code**:

![Install PSX3D with FBI](https://raw.githubusercontent.com/BreadChaser/PSX3D/main/docs/qr-install.png)

The [QR PNG](qr-install.png) encodes the stable URL
https://github.com/BreadChaser/PSX3D/releases/latest/download/PSX3D.cia .
The release CIA asset is named **PSX3D.cia**. Alternatively install the downloaded
CIA with FBI; the 3DSX runs through Homebrew Launcher. No games or BIOS included.

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

See the [performance guide](performance.md) for measurement methodology,
RC02 screening flags, save/load checks and picture limits.

## Known issues and upgrading

GPU rendering remains experimental, with some drawing differences and an
approximate mask-checking blend mode. Display centering/interlace support is
unfinished. Demanding Old 3DS games remain below full speed. State thumbnails
can be blank/stale, and heavy Wi-Fi traffic can cause audio clicks.

Existing settings use `show_fps=0/1/2`, `map_TOUCH_*`, `touch_slots`, `hotkey_ff`,
`hotkey_rewind` and `touch_click_left`. Older keys may not retain those settings;
use the reset controls if necessary. Global reset returns Old GPU / New Software;
per-game reset returns the preset/global choices. Back up saves/states before
upgrading; compatibility across builds is not guaranteed.

Core credits: PCSX-ReARMed (notaz, Exophase, senquack and libretro contributors),
PCSX/PCSX-Reloaded/P.E.Op.S., and the 3DS port authors. Frontend: BreadChaser.
Thanks to devkitPro/libctru/citro2d/citro3d and DSP1/zoogie. GPL-2.0; artwork/font
credits are in the repository. The frontend remains a work in progress.
