# PSX3D v0.4.0

PSX3D now uses the experimental 3DS GPU renderer on both Old and New 3DS.
Software rendering and its settings have been removed. Drawing errors and
missing effects remain; PSX3D is still a work in progress.

- Faster emulation through scratchpad, indirect-block lookup and NCLIP improvements.
- Settings categories with clear per-game/global scope, individual override
  clearing and reset confirmations.
- Box art loads automatically on Old 3DS and during remote library sessions.
  Smaller PNG allocations and download cancellation leave memory for games.
- Movie synchronization and alternating-buffer border clearing fixes.
- Included PCSX-ReARMed core: no submodules or patch step when building.
- Opt-in developer input recording and deterministic replay tools.

Measured saved scenes on Old 3DS: Crash 53.0 FPS, CTR 37.4, GT2 34.4,
Silent Hill 42.0, R4 60.0 and Symphony of the Night 57.8 with frameskip off.
Auto reaches full game speed in Crash, Silent Hill, R4 and Symphony of the Night;
CTR reaches 83.2% and GT2 79.0%. All six New 3DS scenes hold about 60 FPS.
These are scene measurements, not whole-game guarantees. See
[performance and limitations](performance.md).

The built-in HLE BIOS needs no separate BIOS file. A real BIOS remains optional
and may help compatibility. Per-object 3D currently falls back to depth-mesh
screen 3D. Old 3DS has no stereoscopic display or rewind. Heavy games still run
below full speed on Old 3DS, including with automatic frameskip. A thin blue
line can remain along the top edge of Crash Bandicoot’s game picture.

Install with **FBI → Remote Install → Scan QR Code**, or download and install
**PSX3D.cia**. Existing settings and saves are retained; obsolete renderer
settings are ignored.

![FBI remote install](qr-install.png)

https://github.com/BreadChaser/PSX3D/releases/latest/download/PSX3D.cia

Credits and GPL-2.0 licensing: [README](../README.md) and [LICENSE](../LICENSE).
