# Texture hits and command-list investigation

> Historical report: submodule/patch instructions apply to its original commit.
> Current builds use the committed `core/` directory; see [vendor-core.md](vendor-core.md).

Branch: `t3/pica-inv-tex`, 2026-10-18. No console contact and no push.
Baseline is `4ed8026`, the release/v0.3.0-final tip when this work started:
`a63bece` plus the RTT activation/atlas-rebuild correction. That correction
is included in every baseline/candidate comparison below.

## Conclusions

* The texture slice can plausibly recover about **0.2–0.6 ms per Crash
  vblank** on Old 3DS. This is an estimate, not a hardware result. It cannot
  supply the requested 4.5 ms by itself.
* Crash makes about 786 texture requests per vblank in this saved scene.
  The retained memo hits 78.5% of them. Combined cuts remove about 67K ARM
  instructions per vblank; native pictures and simulator draw hashes match.
* **Silent Hill already drops all polygons/rectangles on skipped frames.**
  It still traverses DMA nodes, copies command words, computes GPU timing,
  and preserves state. None of the three tested ways of avoiding batch
  copies improved its skipped-frame cost. The reported 0.53M hardware gap
  has not been removed; it must not be described as leftover texture work.
* **GT2 has a stale frameskip permission inside a batch.** While skipping is
  blocked, PICA consumes an E3 draw-area change and continues parsing and
  drawing until the whole batch ends. gpulib checks the new drawing area
  only afterward. Returning at E3 allows safe skipping to resume earlier.
  The retained guard preserves a pending presentation and excludes async
  rendering. It removes 8,488 otherwise rendered primitive commands over
  300 steady vblanks, but leaves some guarded drawing. This is a partial
  reduction, not permission to discard every GT2 draw on a selected skip.

## Runtime A/B switches

These are named switches.txt keywords backed by separate globals. They do
**not** allocate gldbg bits. All default to `new` on this research branch.
Use clean game loads for comparisons, particularly texture-cache switches.
They are accepted in developer bench batches and reported in status;
the live `set` command's restricted allowlist was not expanded.

| Keyword | old | new |
|---|---|---|
| `texmask=old/new` | Enter cold decoder and calculate the row mask there | Calculate required tile mask in lookup and avoid decoder on a complete hit |
| `texmemo=old/new` | Full cache lookup per primitive | 32-entry memo of decoded requests, validated by VRAM generation and cache epoch |
| `skipgate=old/new` | Check display overlap after the whole batch | Return at E3 during a blocked skip and recheck before the remaining commands |

Example switch lines (retain the same game/state/renderer and other options):

```text
noskip texmask=old texmemo=old skipgate=old
noskip texmask=new texmemo=old skipgate=old
noskip texmask=old texmemo=new skipgate=old
noskip texmask=new texmemo=new skipgate=old
fs3 texmask=new texmemo=new skipgate=old
fs3 texmask=new texmemo=new skipgate=new
```

The ARM investigation runner also accepts `TEXMASK=0/1`, `TEXMEMO=0/1`,
`SKIPGATE=0/1`; native replay accepts the two texture environment switches.
Do not reuse the intermediate commits' gldbg encodings: `66ab5b5` replaces
those encodings with named globals.

## What the existing paths actually do

On the direct Old 3DS path, `do_cmd_buffer_` handles transfer boundaries and
chooses a renderer or `do_cmd_list_skip`. PICA walks the ordinary primitive
stream once. gpulib does **not** first fully parse every primitive and then
make PICA parse it again. The async path is different: it pre-parses/copies
commands and the worker parses them later; this experiment excludes that
path from short returns.

DMA chain headers are traversed and node payloads are gathered into a
1,024-word buffer. This is a copy, but it amortizes dispatch and function
entry overhead. Skipped frames still traverse that chain and use the same
batching. `do_cmd_list_skip` computes timing, tracks texture-page changes,
preserves E1–E7 state, scans polyline terminators, defers appropriate fills,
and retains transfers/large clears. It does not decode polygon vertices,
look up their textures, or decode atlas texels.

`do_vram_io` copies CPU transfers into linear software VRAM. PICA's upload
then converts those pixels into tiled staging. These are distinct required
layouts in the current design. The steady Crash window has no uploads or
readbacks, so eliminating this work cannot help that window. `push_to_target`
was inspected; no speculative upload rewrite was retained.

Fine RTT tracking remains automatically gated. Crash/SH/R4 stay on the
coarse tracker in the tested steady scenes. CTR and SotN exercise feedback
and fine tracking. The memo neither bypasses the activation transition nor
changes region-cache eviction or readback ordering.

## Texture observations

Counters are baseline frames 301–600, divided by 300 emulated vblanks, with
frameskip disabled. "Same generation" means cache validation can immediately
return; "full square" describes the entry, not the requested footprint.
Actual decode calls exclude entering the decoder and discovering no work.

| State | Requests | Previous entry matches | Same generation | Full square | Actual decode calls | 32×32 tiles decoded |
|---|---:|---:|---:|---:|---:|---:|
| Crash | 785.910 | 402.190 | 751.910 | 415.410 | 0 | 0 |
| SH | 349.783 | 199.387 | 306.100 | 318.220 | 0.003 | 0.003 |
| R4 | 150.903 | 112.557 | 150.903 | 36.503 | 0 | 0 |
| CTR | 435.930 | 310.440 | 401.113 | 304.027 | 1.750 | 1.750 |
| GT2 | 575.527 | 348.850 | 575.527 | 131.647 | 0 | 0 |
| SotN | 324.757 | 207.077 | 214.077 | 295.977 | 3.163 | 4.263 |

Warmup matters: to frame 600, Crash decodes 179 tiles, SH 129, R4 773 and
GT2 863. The steady zeroes do not contradict hardware decode time in a
different sampling window. They show why optimizing conversion alone misses
a substantial cost.

For Crash, pristine `texture_for` plus the empty decoder checks execute
175,515 instructions and 227,024 modeled cycles per vblank: about **223
instructions / 289 modeled cycles per request**, or 1.08 us at 268 MHz.
The supplied hardware phase measurement is 2.00 ms inclusive, of which
0.64 ms is decode. At the observed request rate this is 2.54 us inclusive
and approximately **1.73 us / 464 hardware cycles of hit bookkeeping per
request**. These last figures combine this state’s request count with the
supplied hardware window; they are an estimate, not an instrumented
per-call latency distribution.

### Retained texture cuts

`f54f1c8`: form the requested tile mask before calling the decoder. Repeat
an 8-bit column mask across a 64-bit tile mask using a 32-bit multiply and
row-prefix masks. Complete hits avoid the palette decoder's roughly 1 KB
stack frame and register saves. Keep an old path in the same executable.

`730c179`, refined by `66ab5b5`: memoize page/depth/CLUT/window plus decoded
UV bounds in a small noinline front end. Match both the current VRAM
generation and a cache-allocation epoch before dereferencing the entry.
Update the same LRU and last-texture state on hits. Allocation retirement,
state replacement and game startup invalidate the epoch. RTT activation
changes generation; a pending atlas rebuild cannot populate the memo.
A fully decoded square can cover any request inside that square.

The old and new CPU decode paths share one cold conversion body. This
removes duplicated decode code and offsets the new fast path's code size.
Actual conversion stays unchanged: no speculative pixel-format changes or
reduced palette validation.

| State | Memo hit rate | Requests reaching slow lookup per vblank |
|---|---:|---:|
| Crash | 78.5% | 169.247 |
| SH | 77.2% | 79.706 |
| R4 | 59.8% | 60.600 |
| CTR | 90.4% | 41.817 |
| GT2 | 78.7% | 122.420 |
| SotN | 42.4% | 186.917 |

The memo is weakest in SotN: generation changes and disjoint region bounds
are common. Its total instruction count slightly increases there. Crash
still pays a small lookup, bounds comparisons, LRU and last-entry updates;
this is considerably cheaper but is not literally a zero-cost hit.

## Simulator numbers

All cycle figures below are **millions per emulated vblank**, frames 301–600.
Model: instructions + 70×I misses + 70×D-read misses + 4×store misses.
Static interlock estimates are retained in raw profiles but are not added.
Old 3DS has a 16 KB I-cache; this model underestimates real I-cache misses
and does not establish hardware performance. No framebuffer/GPU execution
cost is inferred from the no-op ARM SDK. Raster and ordering identity is
checked separately with native replay.

First, same executable (`runner_final`) with both switches off, each cut
alone, and both on. A pristine release executable is included because
retaining A/B paths changes the old path's prologue and code placement.
The large same-binary deltas must not be represented as release speedups.

### Frameskip off

| State | Pristine | Both old | Mask only | Memo only | Both new |
|---|---:|---:|---:|---:|---:|
| Crash | 3.7565 | 4.1741 | 3.8362 | 3.7853 | 3.7084 |
| SH | 4.2550 | 4.4997 | 4.3417 | 4.3237 | 4.2853 |
| R4 | 2.7909 | 2.8873 | 2.8036 | 2.8092 | 2.7767 |
| CTR | 4.7497 | 5.1717 | 4.9030 | 4.8125 | 4.7873 |
| GT2 | 5.7000 | 5.9937 | 5.7432 | 5.6305 | 5.5788 |
| SotN | 2.7981 | 2.8637 | 2.8092 | 2.8311 | 2.8196 |

### Fixed interval 1 (one selected skip per drawn game frame)

| State | Pristine | Both old | Mask only | Memo only | Both new |
|---|---:|---:|---:|---:|---:|
| Crash | 3.1632 | 3.3787 | 3.2100 | 3.1853 | 3.1459 |
| SH | 3.7028 | 3.8208 | 3.7391 | 3.7482 | 3.7187 |
| R4 | 2.4733 | 2.5039 | 2.4710 | 2.4703 | 2.4581 |
| CTR | 4.2125 | 4.4597 | 4.3252 | 4.2800 | 4.2672 |
| GT2 | 5.0294 | 5.1276 | 5.0365 | 4.9939 | 4.9773 |
| SotN | 2.3569 | 2.4893 | 2.3822 | 2.4495 | 2.3859 |

### Instructions, pristine → both texture cuts

Millions per vblank, with gate changes excluded.

| State | Off pristine | Off new | Skip-1 pristine | Skip-1 new |
|---|---:|---:|---:|---:|
| Crash | 2.4275 | 2.3605 | 1.9305 | 1.8972 |
| SH | 2.0053 | 1.9746 | 1.5926 | 1.5802 |
| R4 | 1.7776 | 1.7718 | 1.5074 | 1.5045 |
| CTR | 2.6639 | 2.6171 | 2.2284 | 2.2049 |
| GT2 | 3.2042 | 3.1556 | 2.6670 | 2.6393 |
| SotN | 1.4583 | 1.4600 | 1.1096 | 1.1104 |

## Skipped-frame investigation

The hardware evidence is `devtools/o3ds60/nostream-09-ff-four/old/results.tsv`.
SH draw/skip/idle is 10.44/4.15/3.62M cycles per vblank; CTR
10.24/5.27/5.26M. GT2 records 6.29M for draw-category vblanks and 6.15M
for idle ones, with no skip-category samples despite selecting skips.
Categories alone do not say that every selected skip draws a complete
scene, or that the entire skip-minus-idle gap is renderer cost: the game
also generates geometry and DMA lists on those vblanks.

Private instrumentation follows both gpulib and PICA, using fixed interval
3. The six states are untouched; counter windows use frames 301–600.
Counts below are submitted primitive commands, before PICA clipping/culling.

| State, original gate | Idle vblanks | Skip-only vblanks | Vblanks with primitive commands rendered | Primitives submitted to PICA during selected skips |
|---|---:|---:|---:|---:|
| SH | 150 | 112 | 38 | **0** |
| CTR | 150 | 113 | 37 | **0** |
| GT2 | 150 | 0 | 150 | **12,675** |
| GT2, new guarded gate | 150 | 75 | 75 | **4,187** |

SH's remaining PICA list contains E1–E6 state updates, about one per
skipped game frame. It has no texture lookup, polygon, rectangle or line
work in that path. Its 108,812 skipped primitive commands still require
length/timing/state handling. Transfers and copy/fill semantics remain
under the existing frameskip policy; this work does not relax them.

An instrumented per-vblank cache profile of SH attributes approximately
69K modeled cycles to `do_cmd_list_skip`, 192K to `GPUdmaChain`, 89K to
`memcpy` (all callers), 8K to `do_cmd_buffer_`, and 4K to PICA list/state
handling on an average skip-only vblank. Total of these functions is
roughly 0.36M. This model run's total skip/idle cost is 4.06/2.45M; it is
not the hardware 4.15/3.62M pair. Instrumentation changes layout; Linux
simulation uses its own CPU/SPU and threading setup. Use these numbers to
locate work, not to subtract 0.36M from hardware or claim the full 0.53M
has a single cause.

### GT2 E3 boundary prototype

Commit `aa2fc0d` returns from the synchronous PICA parser just after E3 when
`frameskip.active && !frameskip.allow`, provided `frame_ready` is false.
gpulib recomputes overlap before dispatching the remaining words. Normal
emulated command timing and state are retained. Software and asynchronous
PICA do not take this early-return path.

A concrete trace at vblank 301 begins with screen `(0,240)`, draw area
`(0,240)-(320,480)`, so `allow=0`. The list changes E3 to `(0,0)`, then E4
and E5 to the other 320×240 buffer, and emits backgrounds and textured
quads. Original PICA processes the whole batch despite the changed area.
This is an outdated host-side overlap decision, not a PS1 draw-to-display
mode bit being cleared. The steady GT2 trace has **zero CPU read-transfer
commands**. Warmup has two PICA texture-feedback readbacks in either mode;
the retained optimization does not remove or suppress them.

The first unguarded experiment saved 0.096M modeled cycles/vblank but
changed the first post-load picture. Returning before a pending
presentation had completed exposed a different held framebuffer. That
version is rejected. Requiring `!frame_ready` retains that batch; all
presented pictures then match. This intentionally retains one of the
three selected-skip game frames in GT2. The remaining guard is also useful
for games that actually draw into the display area: overlap is still
checked, never forced off.

Same-binary gate A/B (`runner_gatesafe`), both texture cuts enabled:

| State | Off old | Off new | Fixed skip 3 old | Fixed skip 3 new |
|---|---:|---:|---:|---:|
| Crash | 3.7337 | 3.7335 | 2.8690 | 2.8700 |
| SH | 4.2488 | 4.2500 | 3.4727 | 3.4742 |
| R4 | 2.7779 | 2.7776 | 2.3050 | 2.3058 |
| CTR | 4.7610 | 4.7613 | 3.9030 | 3.9039 |
| GT2 | 5.5295 | 5.5304 | 4.6790 | 4.6153 |
| SotN | 2.8200 | 2.8203 | 2.1085 | 2.1104 |

GT2 loses 48,408 instructions and 0.0637M modeled cycles per vblank with
skip 3, approximately 0.24 modeled ms. PICA's post-cull primitive counter
falls 190.08→170.79 per vblank. Expect a small hardware benefit, perhaps
0.2–0.5 ms/vblank, rather than recovery from 70.9% to full speed. GT2's
measured idle category alone costs 6.15M cycles, above the 4.47M cycles
available at 60 Hz on 268 MHz. The emulator has a separate substantial
emulation cost even if these unnecessary draws are removed.

### Rejected command-copy experiments

* Replace batch `memcpy` with a scalar word loop: more instructions and
  stores; intermediate Crash 3.69→3.72M modeled cycles. Removed.
* Disable DMA batching: SH skip-only 4.055→4.105M in one instrumented binary.
  Copies disappear, but per-node `do_cmd_buffer_` and skip-parser call
  overhead increases. Drawn frames also regress. Removed.
* Parse suitable skipped primitive nodes directly in RAM, retaining batches
  for state/transfers/partial commands: SH skip-only 4.136→4.140M in the
  same instrumented binary. Copy cost falls about 89K→32K, but parser and
  traversal cost absorbs the reduction. This also enlarges `GPUdmaChain`
  and worsens the off path's layout. Removed; no `skipwalk` switch ships.

These are negative results. There is no retained SH command-walk speedup,
and the user’s skipped-as-cheap-as-idle target remains unachieved.

## Code size and hardware estimate

Real devkitARM `-O2` object sizes (text includes read-only tables):

| Object | Pristine text | Final text | Delta |
|---|---:|---:|---:|
| gpu_pica | 48,420 | 47,640 | −780 B |
| gpulib/gpu | 12,339 | 12,431 | +92 B |
| Combined | 60,759 | 60,071 | **−688 B** |

Combined data increases 10 B and BSS 1,152 B. The first mask-only prototype
by itself grew text 440 B; retain it with the decoder de-duplication from
the second cut. Final combined executable code meets the size constraint.

Crash's supplied phase budget is 8.70 ms GPU list, 8.32 ms polygon/rect,
2.00 ms inclusive texture lookup (0.64 decode), 0.07 state and 0.34 submit.
The phase nesting means those values cannot be added. Readback is zero.
The retained cuts leave tile decode counts unchanged and target the
approximately 1.36 ms hit bookkeeping budget.

Pristine→texture-candidate lookup plus empty-decode instructions fall
175.5K→108.5K (38.2%); modeled cycles fall 227.0K→162.8K (28.3%). Applying
those ratios only to the 1.36 ms bookkeeping budget suggests about
0.38–0.52 ms. Allowing for state/window differences and I-cache effects,
use **0.2–0.6 ms** as a hardware hypothesis. The whole-program simulator
only improves Crash 3.7565→3.7084M (1.3%); SH/CTR/SotN show slight modeled
regressions against pristine even while same-binary A/B improves. Hardware
could erase the estimate. Do not promise a new FPS or claim 4.5 ms saved.

## Correctness and limits

* Texture candidates, pristine and all four switch combinations: **600/600
  cumulative draw hashes identical** for each of six states, frameskip off
  and fixed skip 1 (60 runs, 36,000 compared frame hashes).
* Native texture replay: **360/360 full-VRAM hashes identical** and selected
  frames 10/30/60 byte-identical for all six games.
* Guarded gate, off: 600 draw hashes per state identical old/new. Skipping
  intentionally changes the draw stream, so equality of that hash is not
  the acceptance condition for the gate.
* Gate replay captures commands emitted by the actual ARM PICA/gpulib
  pair, then replays them in the native raster oracle. Over 600 vblanks,
  **all 532 presented scanouts match** old/new: Crash 76, SH 76, R4 77,
  CTR 76, GT2 76, SotN 151. This includes the first post-load picture.
  Full VRAM is allowed to differ in discarded, unpresented buffers.
* RTT direct/copy/staging/palette/region fixtures: four existing RTT modes,
  full VRAM and scanout equality, staging and atlas lifetime guards pass.
* New `tools/gpu-compare/texmemo.py` exercises colliding CLUT keys, atlas
  eviction (241 evictions), square growth, texture windows, same-address
  uploads and repeated state replacement. All four A/B combinations give
  identical complete VRAM and scanout with lifetime guards enabled.
* ASan/UBSan regressions include E3 early-return and pending-presentation
  retention, polygon edges/limits, presentation handoff and held frames.
  Frontend host and developer transport/updater suites pass. Final 3DSX
  builds and generated core patch reverse-apply checks pass.

No hardware precision, CPU cache timing or GPU scheduling claim follows
from these checks. Memo correctness depends on every relevant write
advancing VRAM generation and every allocation retirement advancing the
allocation epoch; existing source/CLUT/feedback tests and the new
reset/eviction fixture cover these paths, not every possible game.
Generation and epoch are 32-bit, as is existing generation tracking.
GT2 safe skipping retains the existing overlap/frameskip policy and has
been checked only in the supplied scenes; it does not establish that an
arbitrary game’s feedback draw can be dropped.

## Reproduction and handoff

Build locally under the repository’s submodule-plus-patch workflow:

```bash
source /home/tony/Development/PS1_3ds/devtools/env.sh
# Core changes are already represented in patch 0002 on this branch.
make -j8
psx3d/tests/dev/run.sh
psx3d/tests/host/run.sh
BUILD_DIR=/tmp/inv-tex-check tools/gpu-compare/regress.sh
REPLAY_ONLY=1 BUILD_DIR=/tmp/inv-tex-check tools/gpu-compare/build.sh
python tools/gpu-compare/texmemo.py --replay /tmp/inv-tex-check/replay-pica --output /tmp/inv-tex-check/memo
python tools/gpu-compare/rtt.py --replay /tmp/inv-tex-check/replay-pica --output /tmp/inv-tex-check/rtt
```

The Python fixtures require the harness’s Pillow/numpy environment.
Private measurements, source instrumentation, runner/plugin sources,
immutable ELFs, numeric per-function JSON, traces and comparison logs are
archived under main checkout’s ignored **`devtools/inv-tex/`**. They use
original states `o3auto`, `sh`, `r4`, `ctr`, `gt2`, `sotn` and the existing
ROM paths; no game assets or state data are committed. Cache matrices run
to 300 and 600 vblanks separately and subtract the two accumulated
profiles. `final-matrix.py`, `gatesafe-matrix.py`, `gate600-capture.py`,
`gatesafe-hashes.py` and `summarize.py` preserve exact invocations.

Commits: `f54f1c8` mask cut; `730c179` memo and shared decoder; `66ab5b5`
named switches/refinement; `814f370` eviction/reset fixture; `aa2fc0d`
guarded E3 skip boundary. The next hardware comparison should use the named
switches from clean identical loads, with no stream. Prioritize SH's
command/DMA phase measurement and GT2 old/new gate with fixed skip 3;
separately measure Crash texture old/new and verify pictures. Hardware
execution was explicitly excluded from this investigation.
