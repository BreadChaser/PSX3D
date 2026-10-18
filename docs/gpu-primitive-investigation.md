# Per-primitive renderer investigation

Branch `t3/pica-inv-prims`, based on `4ed8026`. The requested release ref had
advanced one commit from `a63bece` before checkout; retaining the newer RTT
allocation fix was explicitly agreed. No consoles contacted, no push.

Three opt-in experiments preserve the renderer's geometry, vertex layout,
interpolation and ordering. They reduce measured simulator work, but do **not**
come close to halving the approximately 6 ms hardware primitive residual.
Expected combined Crash saving is **roughly 0.3–0.6 ms per emulated vblank**,
conditional on hardware A/B; this is an estimate, not a hardware result.

## Hardware context and actual work

The coordinating worker's no-stream Old 3DS Crash measurement reports 8.70 ms
inside the GPU command list, 8.32 ms in polygon/rectangle handling, 2.00 ms
nested texture lookup (0.64 ms nested decode), 0.07 ms state, 0.34 ms draw/mask,
and zero readback. Those inclusive timers must not be added together. Primitive
vblanks measured 7.65M main-thread cycles versus idle 3.28M. See the independently
updated ignored `devtools/o3ds60/results.md`.

All results below use emulated vblanks, including the alternating light/idle
vblanks, rather than treating every vblank as a newly drawn game picture.
Instrumented baseline, frames 301–600, default RTT auto gate:

| State | Fast polygons/vblank | Slow polygons | Triangles clipped | Edge crossings | Sprite parts | Output vertices |
|---|---:|---:|---:|---:|---:|---:|
| Crash | 902.18 | 65.50 | 64.00 | 135.00 | 0 | 3018.53 |
| Silent Hill | 292.60 | 193.19 | 107.62 | 229.25 | 0 | 2062.97 |
| R4 | 102.19 | 50.67 | 106.50 | 233.23 | 35.50 | 1509.23 |
| CTR | 456.79 | 80.62 | 118.61 | 250.21 | 0 | 2375.06 |
| GT2 | 451.28 | 209.65 | 160.13 | 349.88 | 45.00 | 3281.28 |
| SotN fight | 31.76 | 0 | 153.95 | 307.91 | 295.16 | 2176.46 |

Clipped triangles include rectangles/lines and both quad halves; they are not
counts of polygon commands. Slow polygons include entirely rejected geometry.
Crash submits 181.77 untextured Gouraud triangles, 633.41 opaque textured
Gouraud triangles and 152.50 semi-transparent textured Gouraud triangles per
vblank, plus one fill. **No Crash quads, sprites or lines in this window.**

Baseline source/symbol attribution, Crash, instructions and cache-model cycles
per vblank (exclusive function bodies; inlined helpers stay with their caller):

| Work | Instructions | Modeled cycles |
|---|---:|---:|
| Command loop + inlined polygon fast path | 596,836 | 688,516 |
| `emit_tris`, including inlined clipping | 92,172 | 107,885 |
| Slow polygon setup | 13,454 | 29,639 |
| `lrintf` used by clipping | 34,039 | 34,667 |
| `mark_drawn_slow` | 76,035 | 93,666 |
| `texture_for` (separate slice) | 129,377 | 160,775 |
| `GPUdmaChain` (upstream submission) | 80,249 | 153,154 |
| `memcpy`, whole process, not all renderer | 62,413 | 94,760 |

The 596,836 command-loop instructions split approximately into 200,401 decode
and bounds, 151,943 vertex packing/output, 202,074 other polygon setup/inlined
state/allocation helpers, and 42,417 list dispatch/timing. Debug line attribution
is approximate, especially for scheduled instructions and inlined expressions.
Clipping plus its rounding consumes about 95k instructions including the
inlined part of `emit_tris`, under 15% of the primitive instruction budget.

## Primitive costs

Warm repeated 32×32 primitives, same packet each iteration, fake SDK, no initial
texture-decode cost: differential 2000 minus 1000 iterations. Includes the
command loop, periodic frame boundaries, dirty tracking and write traffic.
The deliberately clipped triangle intersects both top and left planes.
These are controlled instruction/cycle costs, **not actual per-type hardware
latencies**; the fake SDK cannot charge citro3d or GX work.

| Primitive | GP0 | Instructions | Modeled cycles |
|---|---|---:|---:|
| Flat triangle | `0x20` | 680 | 720 |
| Flat quad | `0x28` | 784 | 860 |
| Gouraud triangle | `0x30` | 680 | 720 |
| Gouraud quad | `0x38` | 784 | 860 |
| Textured flat triangle | `0x24` | 958 | 1,002 |
| Textured flat quad | `0x2c` | 1,090 | 1,170 |
| Textured Gouraud triangle | `0x34` | 958 | 1,002 |
| Textured Gouraud quad | `0x3c` | 1,090 | 1,170 |
| Variable-size rectangle | `0x60` | 959 | 999 |
| Textured rectangle | `0x64` | 1,147 | 1,191 |
| Flat line | `0x40` | 919 | 959 |
| Gouraud line | `0x50` | 921 | 961 |
| VRAM fill | `0x02` | 1,153 | 1,205 |
| Clipped textured Gouraud triangle | `clip-0x34` | 3,789 | 4,835 |

Flat and Gouraud repeated packets have identical colours; the generic parser
still executes its Gouraud checks. Microbenchmarks are dominated by a warm
working set and cannot predict I-cache contention against the PS1 dynarec.
`primitive-bench.c` and `primitive-profile.py` preserve the probes and exact
cache accounting; private ARM binaries and snapshots are archived as evidence.

## What was redundant, and what was already efficient

- **Float versus fixed:** ordinary vertices already use signed 16-bit integer
  x/y and atlas u/v. Float division and rounding appear only at clip crossings;
  the hot unclipped polygon path is integer. A fixed-point clipper would need
  to reproduce the current single-precision multiplication and ties-to-even
  results, so integer division is not automatically an identity-preserving cut.
- **Quarter pixels:** the fast polygon loop computes x×4+2 and y×4+2, clamping
  the far target edge; clipped polygons get the equivalent two-step conversion.
  These operations preserve established PICA edge/sample behavior.
- **Vertex traffic:** `pvtx` is already 12 bytes: four signed shorts and RGBA8.
  A triangle writes 36 bytes, a quad 72. Crash writes about 36.2 KB/vblank,
  2.17 MB/s at 60 vblanks/s. This is not a high bandwidth requirement; store
  issue cost and instruction/cache behavior matter. The buffer is written
  once and never read back during primitive assembly. Existing double-buffer
  flush/synchronization rules remain intact.
- **UV/colour repetition:** fast polygons already convert each unique vertex
  once before expanding quad indices. Untextured dummy UVs and raw/alpha
  choices are repeated per output vertex in the slow path. Crash has triangles,
  so eliminating duplicated quad vertices provides no benefit in its scene.
- **Bounds/clipping:** common polygons compute bounds once and bypass generic
  clipping. Slow primitives scan vertices for containment, compute per-triangle
  bounds, and scan again for dirty bounds. Skipping containing clip planes
  removes copies/modulo/branches without changing intersections.
- **Calls:** `draw_poly`, `draw_rect`, `to_gpu`, common state/allocation helpers
  and `clip_area` are already inlined by ARM GCC. There is no draw_poly call
  overhead to remove. Slow helpers and texture lookup remain out of line.
- **Parsing:** `gpulib` handles DMA/transfer boundaries, then passes an entire
  list tail to `renderer_do_cmd_list`; it does not decode every normal drawn
  polygon a second time. Rectangle dimensions are decoded again for emulated
  GPU timing, but Crash has no rectangle commands. Skip/async parsers are
  different execution modes, not duplicate passes on the same normal path.

## Prototypes and controls

The final build uses a separate exported `pica_prims` variable. **No experiment
uses a `gldbg` bit.** Numeric controls in the first three intermediate commits
were superseded by `662d92f`; integrate the complete commit stack.

| Switch keyword | Change | Commit |
|---|---|---|
| `pica_prims_copy` | Replace order-table indexed word loop with constant-size triangle copies | `5bd2390` |
| `pica_prims_clip` | Use already computed bounds to skip clip planes containing the original triangle | `a345756` |
| `pica_prims_round` | Bounded float→int via ARM11 VFP `VCVTR`, preserving FPSCR rounding | `fa0fe39` |
| `pica_prims_fast` | Enable all three | `662d92f` names/integration |
| `pica_prims_old` | Disable all three | same |

Individual enable keywords accumulate; reset with `pica_prims_old` before
isolating another experiment. No keyword means old paths. These are diagnostic
switches, not user settings. Both startup `switches.txt` and remote batch/load
accept them. Remote `set` accepts them at the synchronized between-vblank
boundary; unlike RTT cache mode, they need no reload. Remote status reports
`settings.pica_prims` as 0–7. Native/simulator harnesses use `PICA_PRIMS=0..7`.

### Instruction footprint

ARMv6K devkitARM GCC 16.1, actual production compiler options. GPU renderer
object text+rodata bytes (data/bss reported separately by archived size output):

| Variant | Bytes | Versus pristine |
|---|---:|---:|
| Pristine baseline | 48,420 | — |
| All runtime A/B paths | 48,836 | +416 |
| Fixed old selection | 48,376 | −44 |
| Fixed copies only | 48,366 | −54 |
| Fixed clip only | 48,608 | +188 |
| Fixed VFP only | 48,312 | −108 |
| Fixed all | 48,474 | +54 |

`PICA_PRIMS_FIXED=0..7` exists only to measure stripped experiment footprint.
The hardware A/B artifact uses runtime selection. Copy and rounding are smaller
when selected permanently; **plane skipping is a size-budget exception**, +188
bytes alone. It is retained as a separately selectable research candidate, not
a size-neutral shipping recommendation. Even +416 runtime bytes can change
16 KB I-cache layout, hence the same-binary tests below and hardware requirement.

### Rejected experiments

A direct decode-and-pack pass preserved all six draw hashes but increased
instructions and grew the command function from 5836 to 6324 bytes in the ARM
Linux build. It was discarded; its private source is archived. A fused slow-path
conversion preserved hashes too but was superseded by the smaller VFP cut.
No per-type cloned polygon decoder or indexed-buffer redesign was retained:
those would add code/submission paths and repeat known hardware risks.

## Six-state cycle results

Frames 301–600, `SMALL=1`, `o3hw_off.ini`, no hash instrumentation. All selected
modes use the same `runner-named` ELF. Cycles = instructions +70×I misses +70×D
read misses +4×store misses, the existing `cbench/cprof` model. Interlocks are
also retained in JSON; cache model figures below do not include them.

| Mode | Crash | Silent Hill | R4 | CTR | GT2 | SotN fight |
|---|---:|---:|---:|---:|---:|---:|
| off | 3,717,423 | 4,269,724 | 2,777,105 | 4,722,703 | 5,658,085 | 2,826,875 |
| copy | 3,714,429 | 4,266,036 | 2,778,438 | 4,731,220 | 5,659,526 | 2,826,474 |
| clip | 3,672,827 | 4,193,209 | 2,701,326 | 4,638,876 | 5,547,172 | 2,722,229 |
| round | 3,682,230 | 4,191,532 | 2,721,352 | 4,660,592 | 5,573,045 | 2,753,484 |
| all | 3,634,853 | 4,110,845 | 2,646,635 | 4,585,808 | 5,463,794 | 2,648,225 |
| All versus off | -2.22% | -3.72% | -4.70% | -2.90% | -3.43% | -6.32% |

Crash all-enabled removes **91,266 instructions**, **82,570 cache-model cycles**
and **24,135 modeled interlock cycles** per vblank. The cache-model change
is **-2.22%** of the whole simulated frame; at 268 MHz it is 0.308 ms,
or 0.398 ms if the separately modeled interlocks are added.

| Crash selection | Instructions removed/vblank | Cache-model cycles removed | Interlocks removed |
|---|---:|---:|---:|
| copy | +13,495 | +2,994 | +14,429 |
| clip | +44,740 | +44,596 | +11,167 |
| round | +32,603 | +35,193 | -1,475 |
| all | +91,266 | +82,570 | +24,135 |

Negative removal means added work. Copy alone has small cycle regressions in
R4, CTR and GT2 despite fewer instructions; do not select it from instruction
counts alone. The pristine separately linked baseline models Crash at 3,735,196
cycles/vblank versus runtime-off 3,717,423. That layout-sensitive difference
is why acceptance should compare modes inside the same binary.

The model understates actual Old 3DS I-cache misses and cannot model its
write-buffer/cache-coherency or citro3d call costs. Do not turn these into claimed
hardware FPS gains. The 0.3–0.6 ms estimate above uses the modeled work removed
and the independently measured approximately 6 ms primitive residual; hardware
layout could erase or reverse a gain. These three cuts do not supply the desired
4.5 ms/vblank reduction. The next large lever remains the common decode/bounds/
packing loop, for which the measured single-pass rewrite was not a win.

## Identity and validation

- Six 600-vblank ARM draw hashes equal pristine baseline, each prototype
  independently and all together; final named-switch all-enabled build also
  matches. This validates ARM VFP rounding in actual clipping, not just x86.
- Native reference-harness baseline/off/all: **360/360** complete native VRAM
  frame hashes and **18/18** selected scanout PPMs byte-identical. Each state
  replays 60 frames; selected pictures are frames 10,30,60. Same integer-edge
  rasterizer is compiled for baseline and candidate. SotN uses the original
  hardware fight state, not a title/menu trace.
- ARM rounding test: **4,065,540** finite bounded values, half-integer ties,
  positive/negative values, all four FPSCR rounding modes, equal `lrintf`.
- ASan/UBSan polygon/size-limit/presentation/held-frame regressions, default
  and fixed-all selection, pass. Remote control/transport/updater host suites
  pass; invalid primitive keywords are rejected and status/control accept them.
- 3DSX and CIA builds pass; patch 0002 regenerated from this isolated core,
  reverse-apply checked. Uncommitted modified core is normal for this repository.

No new picture correctness claim versus the original PS1 is implied: these are
identity comparisons against the release renderer. Native fake GPU tests cannot
validate PICA precision or instruction-cache speed on a console.

## Hardware handoff

Final diagnostic source integration: `662d92f`; later report/tool-only commits
need no console rebuild. Artifacts are in the main checkout's ignored
`devtools/inv-prims/artifacts/`:

- CIA SHA256 `642c0954c0a98f212c8005104ef043d0db27b54b17ebbc1a24510868de527d33`
- 3DSX SHA256 `4b6aeda6132dfebcf05f5eea4313486f928c4802efdf77f063d1187a0461ee4a`
- ELF SHA256 `14923ae467230b620a37fa962ac85734e3caea97114a4c31243d1d7ed07de564`

Let the coordinating console worker deploy through its existing serialized loop.
Example remote batch lines, preserving protected Old slot 2:

```text
game=Crash_Bandicoot state=2 renderer=gpu frameskip=off pica_prims_old warmup=5 seconds=20 pmu=0,2 shot=1
game=Crash_Bandicoot state=2 renderer=gpu frameskip=off pica_prims_fast warmup=5 seconds=20 pmu=0,2 shot=1
game=Crash_Bandicoot state=2 renderer=gpu frameskip=off pica_prims_old warmup=5 seconds=20 pmu=0,2 shot=1
```

For live same-session A/B, `set pica_prims_old`, then `set pica_prims_copy`
(or clip/round/fast) through `fastloop/ctl.py`. Do not include numeric `gldbg`
controls from early prototype logs. Compare FPS, main CPU cycles and I-cache
misses, then repeat all six scenes and New sanity if hardware accepts a cut.
Keep original clock/audio/thread/frameskip settings; use the existing verified
no-stream condition and screenshot protocol. No test console was touched here.

Private evidence, scripts, instrumented source and snapshots are archived under
ignored `devtools/inv-prims/`; simulator working copy was `/tmp/inv-prims-sim`.
