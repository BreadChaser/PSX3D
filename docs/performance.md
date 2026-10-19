# Performance

<picture><source media="(prefers-color-scheme: light)" srcset="performance-light.svg"><img src="performance-dark.svg" alt="PSX3D speed on Old 3DS and New 3DS" width="880"></picture>

The v0.4.0 GPU-only candidate was measured on 2026-10-19 in six saved scenes.
Each run used clock 57, a 10-second warmup and a 15-second sample, Old slot 2
or New slot 0. The consoles use HLE BIOS; these runs load existing states.
NTR was not explicitly unloaded; the host observer reported no recent packets.

Exact values are in the [README table](../README.md#performance).

Full speed is 60 emulated frames per second for these scenes. Automatic
frameskip reduces displayed pictures to improve game speed; 100% does not mean
60 distinct pictures per second. CTR and GT2 remain below full speed on Old 3DS.
The auto chart also shows pictures displayed per second out of the pictures the
game draws. For example, “14 of 30” means 14 pictures are shown from 30 game
pictures each second. These are scene measurements, not whole-game compatibility
guarantees.

## Main improvements

- The 3DS GPU draws the game while the CPU runs PS1 emulation. A shared texture
  atlas and retained render-to-texture regions avoid repeated texture decoding.
- Double-buffered command recording overlaps CPU and GPU work while protecting
  textures, vertices and readbacks. Cache flushes cover written buffers instead
  of the entire linear heap.
- Batched DMA commands reduce renderer overhead. Unchanged game pictures and
  bottom-screen content can be retained between updates.
- ARM dynarec improvements reduce indirect lookup and NCLIP overhead, and a
  guarded fast path handles the relocated PS1 scratchpad used on 3DS.
- Safe wait-loop skipping avoids emulating known idle loops. Sound mixing and
  disc read-ahead use system-core time when available.
- Code layout and the split renderer build reduce instruction-cache pressure
  on Old 3DS. The removed software renderer also reduces executable size.

For historical context, the v0.3.1 Old 3DS Crash scene measured 49.1 FPS with
GPU rendering versus 22.8 with software rendering. Those older measurements
are not a matched comparison with v0.4.0 and its changed executable layout.

## Checking changes

Use identical scenes, settings, warmup and network conditions, then measure a
baseline/candidate/baseline bracket. Compare against the mean of both baseline
runs and report their drift. A simulator can establish matching output for
specific inputs; its cycle estimates cannot predict Old 3DS instruction-cache
layout effects. Hardware is the performance gate.

Per-second near-black probes and occasional screenshots can reveal problems
but cannot prove an absence of flicker. The GPU renderer still has drawing
errors and missing effects. See [building and testing](building.md) for identity
checks and [remote testing](dev-control.md) for reproducible hardware batches.
