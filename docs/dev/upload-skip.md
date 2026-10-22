# Uploads that change nothing (upload-skip)

Some games send the same pixels to PS1 VRAM again and again. Worms World Party rewrites
its small text/HUD images about 35 times a frame, and 95% of those uploads change nothing.
For the GPU renderer every upload meant "these pixels changed": `renderer_update_caches`
bumped the VRAM generations (every texture over the rectangle was decoded again) and
`push_to_target` copied the rectangle into the render target. On the Old 3DS that was
~4–5 ms of a 16.7 ms frame in Worms (`devtools/issue3-games/REPORT.md`).

`psx3d/source/uploadskip.c` stops such an upload at software VRAM. Evidence, simulator
builds and the hardware package are outside Git in
`/home/tony/Development/PS1_3ds/devtools/upload-skip/` (`REPORT.md`).

## How it works

- gpulib calls `pica_settle_rect` when a VRAM transfer starts and `renderer_update_caches`
  when it ends. Both calls go to PSX3D through `--wrap` in the core's partial link
  (`UPLOAD_WRAP_SYMS` in the top Makefile), like the Real 3D hooks; the frontend calls the
  originals by their own names.
- Start of a CPU-to-VRAM transfer (after the real call settled pending fills): copy the
  destination rectangle (up to 4096 pixels) unless GPU drawing that software VRAM hasn't
  seen yet lies in it.
- End: if the rectangle equals the copy and still has no such GPU drawing, return without
  calling the renderer. Software VRAM, the render target and every decoded texture already
  hold these pixels, so nothing the renderer would do changes anything.
- "GPU drawing software VRAM hasn't seen" is gpu_pica's own tracking: the 8x8 tile bits
  (`fine_drawn`, RTT new) or the 64x16 block bits (`drawn`, RTT old). gpu_pica exports
  read-only aliases of them (`pica_drawn_tiles`, `pica_drawn_blocks`, `pica_rtt_detected`):
  no code or storage of their own, so core code keeps its addresses. Deferred fills are
  drawn on the GPU and marked too, and a readback that clears bits settles the fills first.
- Only without the GPU thread (`gpu.async`, `pica_thread_call`), i.e. the Old 3DS: with the
  thread those tables belong to the renderer's thread. VRAM reads, mask-bit uploads (any
  result is compared, so they are fine), state loads (`state_changed`) and VRAM copies are
  not affected: copies and state loads never start a transfer, so they always go through.
- A big rectangle (1024+ pixels) whose last two uploads both changed (movie strips) isn't
  copied for its next 15 uploads, so movies don't pay for the copy.
- `gldbg=268435456` turns it off (same-binary A/B).

## Results

Simulator with the Old 3DS cache model, frames 301–600 (not a hardware claim):

| Scene | Before | After |
|---|---:|---:|
| Worms World Party, match turn | 6.59M cycles/frame (24.6 ms) | 5.14M (19.2 ms), −22% |
| Mega Man X5 stage 1 | 2.75M | 2.74M |
| Capcom vs. SNK Pro attract fight | 4.11M | 4.12M |

Worms: tile decodes 4.1 → 0.3 ms per frame, the render-target pushes are gone; the copy
and compare cost ~0.5 ms. Kept uploads in 600 frames: Worms 20,895 of 21,990; in the 68
HLE boots Alundra 778, Metal Gear Solid 79, RE2 12, Klonoa 6, Chrono Cross 1; SotN 8; the
13 heavy scenes upload almost nothing (no change there except code layout).

Correctness (all against origin/main):
- Full render-target VRAM including mask bits, every frame: 19 gate traces, Worms, X6 and
  CvS (600–12,000 frames), native replays with the real wrapper linked in; default RTT,
  forced old and forced new tracking.
- Guest checkpoints, IRQ trace and the serialized state identical: 13 heavy + 6 std states
  and 68 HLE boots × 600 frames, plus 5 issue-#3 scenes. Draw hashes identical everywhere
  except Worms (fewer texture decodes there, by design).
- Real citro3d 1.7.1 register write sequence, one binary with the skip off vs on, 19 states
  × 600 frames: identical in 17; SotN (both scenes) sends 1,168 fewer command words, the
  pushes of its 8 kept uploads, and is otherwise the same.
- Core code, core data page offsets and library code keep rc4's addresses
  (`tools/layout/compare.py`, `tools/layout/check.sh` PASS); `uploadskip.o` links after
  every other frontend object.

The Old 3DS A/B/A (rc4 → upload-skip-1 → rc4, 13 heavy scenes off + Auto, Worms and X6)
is queued (`devtools/fastloop/OLD-QUEUE.md` entry 13).

## Not done / leads

- VRAM copies (`do_vram_copy`) whose result equals the destination also re-decode; they
  have no transfer start to hook. The trace replays treat copies as uploads and keep 607
  of them in SotN: a pre-copy hook would be needed (core change).
- Comparing inside gpulib's `do_vram_io` (no copy, compare while writing) was the first
  prototype: cheaper per upload, but it grows core code ~240 B and moved the hot renderer
  by ~840 B. The frontend copy costs ~0.5 ms in Worms.
- New 3DS (GPU thread): would need the check on the renderer thread; the New already runs
  these games at full speed.
