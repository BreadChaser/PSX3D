# PSX3D v0.4.0 — draft release notes

Local candidate only. No tag or release has been published.

PSX3D now uses the experimental 3DS GPU renderer on both Old and New 3DS.
Software rendering and its settings have been removed. GPU drawing errors and
missing effects remain; this is still a work in progress.

- Faster emulation through the relocated scratchpad fast path, indirect-block
  lookup and NCLIP improvements. Final console measurements: see
  [candidate acceptance](v040-candidate.md).
- Shared category pages for settings, explicit per-game/global scope, individual
  override clearing and clear reset confirmations.
- Box art loads automatically on Old 3DS and during remote library sessions.
  Smaller PNG allocations and clean download cancellation leave memory for games.
- Movie scanout synchronization and alternating-buffer border clearing fixes.
- Vendored, pruned PCSX-ReARMed core; no submodule or patch regeneration step.
- Opt-in developer input recording and deterministic replay tools.

The built-in HLE BIOS is the normal configuration; no separate BIOS file is
required. A real BIOS remains optional and may help compatibility. Real per-object
3D currently falls back to depth-mesh screen 3D until GPU stereo lands. Old 3DS
has no stereoscopic display or rewind. Heavy games still run below full speed on
Old 3DS, including with automatic frameskip.

The unfinished widescreen, GPU-stereo and HLE-compatibility branches are excluded.
The existing widescreen behavior is unchanged; no new curated widescreen bundles
are claimed. The experimental nearest-SPU switch was removed after negligible
hardware gains; normal audio interpolation defaults remain.

Install the eventual release with FBI using the stable CIA URL below. The asset
must be named **PSX3D.cia**. The QR image is unchanged.

![FBI remote install](qr-install.png)

https://github.com/BreadChaser/PSX3D/releases/latest/download/PSX3D.cia

Credits and GPL-2.0 licensing remain in the README and LICENSE.
