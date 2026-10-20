# Performance

<picture><source media="(prefers-color-scheme: light)" srcset="performance-light.svg"><img src="performance-dark.svg" alt="PSX3D speed on Old 3DS and New 3DS" width="880"></picture>

The v0.4.1 release candidate was measured on 2026-10-19 in each game's
heaviest tested scene: save slot 5 (the heaviest scene per game), Silent Hill's
alley (slot 6) and café (slot 7). Racing games use their attract-mode races,
Symphony of the Night the prologue Dracula fight. Each run used a 3-second
warmup and a 12-second sample (Need for Speed: 1 + 5 seconds, its demo is
short), clock auto, the New 3DS with its GPU worker thread. The consoles use
the HLE BIOS; the runs load save states. The lighter scenes used up to v0.4.0
(Old slot 2, New slot 0) are still measured as a reference.

Exact values are in the [README table](../README.md#performance).

Game speed measures emulated time against real time: 100% means normal speed.
The FPS panel measures new pictures shown per second, like the in-game FPS
counter: drawn game pictures, capped by top-screen presentations
(`min(drawn_count, top_count)` in the benchmark). `top_count` alone also counts
repaints of an unchanged picture: with frameskip off a Final Fantasy VII battle
repaints its 15 pictures a second 60 times. Separate bars show frameskip off
and auto. Dashed markers show each game's own picture rate: 30 for most 3D
games, 60 for Symphony of the Night, Tekken 3, Klonoa and Need for Speed, 15 for
Final Fantasy VII battles. Busy scenes often draw
fewer pictures than that at full speed (Silent Hill's café: 22). Automatic
frameskip shows fewer pictures to improve game speed; 100% speed does not imply
60 pictures per second. The New 3DS draws Crash Team Racing, Silent Hill,
Ridge Racer Type 4 and Gran Turismo 2 with their widescreen fixes in these runs
(that console's Screen setting is Widescreen); the Old 3DS runs 4:3.
These are scene measurements, not whole-game compatibility guarantees.

## Main improvements

- The 3DS GPU draws the game while the CPU runs PS1 emulation. A shared texture
  atlas and retained render-to-texture regions avoid repeated texture decoding.
  Since v0.4.1 the atlas stores each texture at its exact size and reuses space
  across sizes, which stopped the texture thrashing in busy scenes (Ridge Racer
  Type 4 races, Need for Speed, Tekken 3, long Silent Hill sessions).
- Double-buffered command recording overlaps CPU and GPU work while protecting
  textures, vertices and readbacks. Cache flushes cover written buffers instead
  of the entire linear heap.
- Batched DMA commands reduce renderer overhead. Unchanged game pictures and
  bottom-screen content can be retained between updates.
- ARM dynarec improvements reduce indirect lookup and NCLIP overhead, and a
  guarded fast path handles the relocated PS1 scratchpad used on 3DS. Since
  v0.4.1 the scratchpad sits at one ARM immediate, loads and stores through one
  base register share a single range check, and the ordering-table clear writes
  four entries per step.
- Safe wait-loop skipping avoids emulating known idle loops. Since v0.4.1 this
  includes the PsyQ library's `VSync(-1)` polling loop (Klonoa: 58% to 85% game
  speed on Old 3DS) and the older PsyQ `v_wait` loop (Crash Bandicoot: +3%);
  skipped iterations cost exactly the guest time they would have taken. The
  dynarec no longer emits Cortex-A8 branch-prediction padding, which the 3DS's
  ARM11 doesn't use. Sound mixing and disc read-ahead use system-core time when
  available.
- Code layout and the split renderer build reduce instruction-cache pressure
  on Old 3DS. The removed software renderer also reduces executable size.

For historical context, the v0.3.1 Old 3DS Crash scene measured 49.1 emulated
vblanks/s with GPU rendering versus 22.8 with software rendering. Those measurements
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
