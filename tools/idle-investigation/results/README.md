# Measurement records

Base a63bece; production prototype e6372da; no hardware measurements.
The report is `docs/idle-investigation.md` at the repository root.

* `build-identity.json`: production source, toolchain and artifact/binary hashes.
* `baseline-summary.json`: exact unforced frames 301–600, original skips enabled,
  prototypes disabled. `idle` rows are `[guest_pc_hex, kind, cycles/frame,
  successful_batches/frame, omitted_iterations/frame]`. Kind 0 is generic RAM
  poll, 1 is specialized R4 VSync(1) (no PC recorded), 2 is upstream PsyQ hack;
  kind 3 in raw logs denotes the new countdown. These are guest cycles.
* `ctr-new-wait.json`: exact omitted guest cycles and iterations with pointer
  prototype enabled; cumulative totals at 300/600 and steady differences.
* `performance-summary.json`: final clean script-built fast layout; complete
  headless core, frames 301–600. `_cycles` is modeled host cycles/frame and
  `_instructions` is ARM instructions/frame. Both new switches change off→on
  in the same binary. Original skips remain enabled.
* `earlier-layout-performance.json`: previous separately built fast layout.
  Similar instruction saving, different cache results; not an extra trial of
  the final binary. Preserve this to show sensitivity of the small gain.
* `host-ranges.json`: approximate translated guest-PC ranges in final baseline
  host profile, using inclusive `lo`/`hi`. Native helper cost is excluded.
* `bios-host-summary.json`: exclusive native functions attributed to psxbios.c
  by ELF debug symbols, final baseline. Does not include callbacks/callees.
* `equality.json`: nine game/window pairs; baseline and candidate identical
  at all logged checkpoints. Final hashes plus checkpoint text included.
* `synthetic.json`: ten 600-frame program pairs; all compare full state,
  RAM/audio/cycles and draw hash. MMIO/store negative cases skip zero cycles.

Counter/checksum builds and cache-cost builds are separate. Cache model is
instructions + 70*I misses + 70*read misses + 4*write misses, without interlocks.
Audio and raster limitations, sample perturbation, overlapping ranges and
exact switch meanings are explained in the report. All hashes are FNV-style
64-bit unless explicitly labelled SHA256. Raw evidence/binaries are private
under the ignored worktree directory `devtools/idle-inv/`.
