# PICA state and submission investigation

> Historical report: submodule/patch instructions apply to its original commit.
> Current builds use the committed `core/` directory; see [vendor-core.md](vendor-core.md).

Local branch `t3/pica-inv-submit`, based on the requested **a63bece**. No
console access or push. The release branch had advanced to 4ed8026 when work
started; the experiments were subsequently rebased onto a63bece and both
functional and cycle runs repeated there. Early 4ed8026 results are exploratory.

## Decision

**Do not pursue submission as the Crash 60 Hz solution.** The supplied Old 3DS
hardware profile measures only 0.072 ms of apply_state and 0.336 ms of batch
submission/mask work per emulated vblank, inside an inclusive 8.701 ms list.
Even deleting this slice cannot provide the requested 4.5 ms. Prioritize the
~6 ms primitive residual and ~2 ms texture lookup path.

Silent Hill and GT2 warrant a separate hardware phase check: their draw counts
are approximately 15x and 8x Crash's in the simulator. SotN fight does not show
the same submission pressure. No new hardware measurements were taken here.

The three opt-in experiments below preserve output in the host model. They
remain experiments, **not size-qualified or hardware-accepted changes**: the
combined named-switch renderer object is 48,452 bytes of text versus 48,260
for a63bece (+192 bytes, +4 bytes BSS). This misses the requested neutral/smaller
code-size gate. Parser strings/code add further frontend bytes. Do not integrate
these as an accepted Old 3DS performance improvement on simulator evidence.

## Switches and implementation

All default to `old`. Set in `switches.txt` before launching; these are dedicated
switches, with **no gldbg bit allocation**:

| Switch | new behavior | Prototype commit |
|---|---|---|
| `submitstate=old\|new` | After STP mask drawing, invalidate only blend/TEV0/alpha state with the impossible blend sentinel; retain scissor, atlas and texture scale | 4e45b4c |
| `submitclip=old\|new` | Keep apply_base's full-target viewport/projection; only GPU column changes require a scissor update. CPU row clipping remains active | 925d9a4 |
| `submittex=old\|new` | For an already bound atlas whose storage was rewritten, clear the GPU texel cache without copying an alias, rebinding the texture or re-uploading scale | 72f922b |

Commit a3987e2 moves their original experimental selectors to named keywords.
The independent exported `pica_submit` mask uses bits 1/2/4 for the native/ARM
harness environment `PICA_SUBMIT=0..7`; it does not share `pica_debug` storage.
For a combined A/B use all three `old`, then all three `new`. Reload/relaunch
between tests. Existing gldbg profiler bits retain their original meaning.

The third cut rarely triggers after the first: most apparent rebinds were
caused by wholesale STP invalidation. Do not add the isolated gains together.
Atlas texel-cache invalidation, upload/readback ordering, vertex lifetimes,
double buffering and queue waits are preserved.

## Draw and dirty-state counts

These are per **emulated vblank**, frames 301–600, not per drawn picture.
Old→new means all three named switches. Draw counts, effects and TEV0 updates
are unchanged.

| State | Draws | Effect / TEV0 blocks | Viewport old→new | Scissor old→new | Texture0 binds old→new | Uniform vectors old→new | Command words old→new |
|---|---:|---:|---:|---:|---:|---:|---:|
| Crash | 13.50 | 13.00 | 5.50→0.50 | 5.50→1.00 | 5.00→0.50 | 17.00→2.50 | 1115→898 |
| Silent Hill | 205.65 | 205.65 | 69.22→0.50 | 69.22→1.00 | 68.72→0.50 | 208.15→2.50 | 15979→12831 |
| R4 | 2.52 | 2.00 | 1.00→0.50 | 1.00→0.50 | 1.00→0.50 | 4.00→2.50 | 237→214 |
| CTR | 23.87 | 21.61 | 9.28→2.25 | 9.28→2.75 | 8.78→2.25 | 31.83→11.25 | 2061→1750 |
| GT2 | 111.22 | 109.67 | 37.01→0.50 | 37.01→0.50 | 37.02→0.50 | 112.04→2.50 | 8592→6913 |
| SotN fight | 14.90 | 12.89 | 8.66→3.58 | 8.66→5.58 | 7.77→4.58 | 29.09→15.75 | 1467→1283 |

Base/attribute/buffer restores average 0.50/vblank in Crash, SH, R4 and GT2,
2.25 in CTR and 2.00 in SotN. There are no stage1 texture binds in this steady
window: dithering is disabled by o3hw_off.ini. Dither-enabled traces are checked
separately for output equality.

The isolated texture-cache-only cut saves **zero** steady-state command words
in Crash/SH/R4/CTR/GT2 and ~2.5/vblank in SotN. It is not a useful standalone
speed candidate. Preserving STP state supplies nearly all of the command-word
reduction; viewport-only independently removes repeated projection uploads, but
its savings overlap the STP fix.

## What citro3d actually emits

Inspected citro3d **1.7.1**, tag commit
9f21cf7b380ce6f9e01a0420f19f0763e5443ca7. Setters generally mark dirty even when
values are identical. Each DrawArrays calls C3Di_UpdateContext:

| Dirty state / operation | Emission or CPU work |
|---|---|
| effect (any alpha/blend/depth/cull setter) | 14 GPUCMD calls, **34 words**, including depth, stencil, early-depth and shadow settings even when only alpha/blend changed |
| one TEV stage | Entire 20-byte C3D_TexEnv, **6 command words** |
| viewport | 4 consecutive registers + XY, **8 words**; SetViewport also dirties/disables scissor |
| scissor | 3 registers, **4 words** |
| texture 0 | 5-register block + type, **8 words**, then texture status/cache-clear/shadow commands (**6 more words**) |
| buffer info | base + all **12** buffer descriptors, **40 words**, despite one active buffer |
| float uniform upload | config write + four words per dirty vector, packet headers and alignment |
| every draw, even with clean uniforms | scans **96 float + 4 integer** dirty flags for **each** shader stage, plus bool flags |
| DrawArrays after context update | 11 command-writer calls, **22 words**, including primitive restart, index mode, vertex count/offset, array mode, trigger, post-vertex cache clear |

The effect and texture paths explain why avoiding a setter is better than
making that setter cheaper. apply_state already compares sent state; the main
redundancy is the STP pass setting sent_valid=false, which also forces unrelated
scissor/texture/uniform state to be resent. Depth, attributes and buffer setup
are frame/base-restoration costs, not costs paid on every ordinary batch.

Precomputed raw command blocks could reduce the 11 DrawArrays writer calls and
replace whole-effect updates with blend/alpha writes. They would still need
correct dirty-context ownership and restoration before citro2d or RTT rendering.
A direct STP pass is the narrowest plausible entry: uniforms, buffers and the
framebuffer are already valid after its RGB draw. This was **not implemented**
because Crash's hardware ceiling is tiny and the small cuts already fail the
size gate. Do not claim that the register-command route is picture-verified.

## Queue, frame and cache costs

The frontend already uses two recording banks and protects vertices, atlas
rewrites and staging uploads until the previous queue completes. FrameBegin
selects a free bank; it does not submit the queue. FrameSplit only finalizes and
queues a bounded command list. FrameEnd waits for the preceding queue where
needed, flushes written ranges, finalizes commands, queues display transfers and
starts GX. Starting it earlier was previously rejected on hardware.

Targeted command/vertex/texture cache flushes already avoid gameplay whole-heap
flushes; menus keep the conservative heap flush. gpu_cache.c's indexed vertex
range tracking is for citro2d DrawElements; pica_flush_vertices handles PICA's
DrawArrays. Removing or bypassing these cache/lifetime barriers is not justified.
The simulator backend below deliberately does **not** measure kernel cache
maintenance, queue synchronization, transfers, shader upload or GPU execution.
Use existing hardware queue/heap timings for those fixed costs.

## Measurement and reproducibility

Private evidence is under
`/home/tony/Development/PS1_3ds/devtools/inv-submit/`. It includes SDK source,
backend/build scripts, runner binaries, raw cache profiles, per-frame checks,
compiler-size evidence and CIA/3DSX build logs. No ROMs, states or traces are
committed. Temporary simulator work is `/tmp/inv-submit-sim` and is removed after
archiving its small scripts/binaries.

The standard fake citro3d header compiles most submission work away. To avoid
that blind spot this investigation links real citro3d state, effects, uniforms,
attribute/buffer binding, DrawArrays and libctru GPUCMD CPU command writers into
the ARM Linux runner. Allocation, shader setup, frame/queue and OS/GPU calls are
stubbed. GPUCMD writes real CPU command memory. An optional draw wrapper counts
dirty blocks; it is disabled for cycle runs. Hashing is also disabled for cycle
runs and enabled in separate 600-frame equality runs.

Cache-model windows subtract 300 frames from 600, with SMALL=1 and
`o3hw_off.ini`, on the same binary for old/new. The emulator portion comes from
the established simulator core; absolute modeled total cycles are not a fresh
release hardware forecast. The model is instructions + 70*I misses +
70*D-read misses + 4*store misses. It omits ARM11 interlocks and underestimates
hardware instruction-cache/layout effects. SDK subtotals exclude shared memcpy
and inlined state setters, and exclude the frontend's real fixed costs.

Native picture verification uses the current integer-edge rasterizer for BOTH
baseline and candidate, full 1024x512 RGBA5551 per-frame hashes (including masks),
draw hashes and selected 10/30/60 scanout images. SotN uses the explicitly named
`sotn-fight` trace; the 600-frame ARM run uses the protected-state copy `sotn.st0`.
A matched model image is not PICA precision or hardware cache-coherence evidence.

## Final named-switch cycle results

Real citro3d CPU backend, both modes in the **same** binary, frames 301–600.
These are modeled CPU costs, **not hardware timings**. `old` means all named
switches old, `new` means all three new. The SDK column sums citro3d, GPUCMD and
its numeric-conversion/binding routines; shared memcpy and inline setup remain
outside that subtotal.

| State | Total M cycles old→new | Total delta | SDK model ms old→new |
|---|---:|---:|---:|
| Crash | 3.8213→3.7852 | -0.94% | 0.299→0.244 |
| Silent Hill | 5.8375→5.4063 | -7.39% | 3.449→2.760 |
| R4 | 3.3285→3.3243 | -0.13% | 0.092→0.083 |
| CTR | 4.9605→4.9151 | -0.92% | 0.520→0.446 |
| GT2 | 6.2549→6.0964 | -2.53% | 1.650→1.382 |
| SotN fight | 2.9910→2.9722 | -0.63% | 0.450→0.417 |

The largest gains are SH and GT2. Crash/R4/CTR/SotN do not justify spending
more time here for the Crash target. Hardware is needed to decide whether SH’s
modeled ~0.7 ms SDK reduction survives its instruction-cache effects.

## Practical estimate for Crash

The combined cut removes about 0.055 ms from the modeled SDK subtotal. Scaling
that by the measured 0.336 ms hardware draw/mask slice suggests roughly
**0.05–0.15 ms/vblank** total hardware savings, with ~0.1 ms as an order-of-magnitude
estimate. It is not a measured speedup; instruction-cache layout could erase or
reverse it. The hard ceiling from the supplied phase measurements is only
0.408 ms even if all measured state and submission work vanished. This cannot
close a 4.5 ms gap.

SH and GT2's baseline modeled uniform scans alone cost about 1.1 and 0.6 ms per
vblank. They remain after these three cuts because draw counts do not change.
If hardware confirms those games have material draw cost, reducing ordered
passes/batches or a narrowly scoped raw STP submission would have more headroom
than additional setter caching. That is a separate investigation; no such
change was made here.

## Validation completed

- Named-switch old/new draw hashes match for all six **600-frame** ARM runs.
- Native a63bece baseline versus combined named switches: **360/360** full-VRAM
  hashes match; **18/18** selected scanout images match; all six draw hashes match.
- All five existing batching fixtures and all five RTT fixtures pass with
  PICA_SUBMIT=7, including complete VRAM, scanout and delayed-reader guards.
- Earlier isolated-selector runs also matched every frame/image/draw hash for
  all three cuts, but were on exploratory 4ed8026 and are not the final baseline.
- Normal dual-renderer 3DSX and CIA builds pass; patch0002 reverse-apply check
  and whitespace checks pass. Existing executable-stack linker warning remains.
- No hardware contacts, installs, pushes or public changes.
