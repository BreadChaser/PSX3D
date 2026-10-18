# Renderer footprint/layout experiment

Base: `t3/o3ds60` at `b27a139`. Implementation: `t3/pica-layout` in `/home/tony/.t3/worktrees/PS1_3ds/t3-82d2c88c`.
No console contact or push. All hardware performance judgments remain pending.

## What changed

- `PICA_LAYOUT=baseline|split|section|os` are separate builds. The default is baseline; all existing runtime switches retain their values and meanings.
- Split: outline diagnostic timer sampling, line/polyline parsing, clipping, texture allocation, fine RTT tracking, rare vertex-buffer refill and state transitions. Give the command loop, polygon decode/pack, texture lookup and common state/dirty-region gates explicit call boundaries. Cold helpers use `cold,noinline`; the shared helpers reduce repeated inlined code.
- Section: identical split code, with only marked gpu_pica functions and the two gpulib command-walk boundaries collected in `.text.pica_hot`, in call order. Original SDK script is copied and gets that one section after ordinary `.text`; other input text order is preserved. ELF entry remains `0x100000`; the new section shares the executable LOAD segment. Absolute addresses elsewhere necessarily shift as code sizes change.
- Os: baseline layout/source decisions with `-Os` appended for gpu_pica.c only. The gpulib object is byte-identical to baseline.
- The build tracks variant changes and invalidates gpu_pica, gpulib, devcontrol and the final link. `tools/pica-layout/build.sh` serializes builds and archives CIA/3DSX/ELF/map/objects/SHA256SUMS.

## Footprint: do not confuse the selected spine with all code executed

| Build | gpu_pica text, bytes | Selected production spine, bytes | Spine incl. gpulib, bytes | Crash touched instruction bytes (sim) | Crash touched 32B cache lines (sim bytes) |
|---|---:|---:|---:|---:|---:|
| baseline | 49,592 | 16,468 | 17,912 | 13,360 | 16,320 |
| split | 45,736 | 8,924 | 10,368 | 13,108 | 15,328 |
| section | 45,736 | 8,924 | 10,368 | 13,108 | 15,712 |
| os | 38,164 | 11,688 | 13,132 | 12,408 | 14,624 |

The split/section production spine is **8,924 bytes (8.71 KiB)**, or **10,368 bytes (10.13 KiB)** including gpulib. The full dynamically executed renderer footprint is still **13,108 instruction bytes** in the sim; the 8–10 KiB goal is met for the selected common spine, **not the full renderer working set**. Splitting alone changes executed instruction coverage only modestly (13,360→13,108 bytes). Its larger reduction is in function bodies, cold inlining, and line occupancy. Do not present the section size as the complete I-cache working set.

Sim ELF uses ARM GNU 14.2.1 and real citro3d 1.7.1; production uses devkitARM GCC 16.1. The compilers emit different code, so exact sim byte footprints are not production measurements. Renderer text sums executable gpu_pica object sections (not debug, constants, or BSS). Selected spine names/addresses/sizes are in `metrics.json` and final maps.

## Exact Crash path map

Window: two independent runs, 600−300 vblanks, Old-size target, frameskip off, default prototype switches. The instruction/cache model includes actual citro3d code rather than no-op SDK calls. Frame hashes use separate instrumented binaries; performance runs have no hash instrumentation.

For every variant:

- `crash-renderer-functions.tsv`: **every** renderer function, address, full byte size, instructions/vblank and I-misses/vblank (zero rows identify unexecuted functions).
- `o3auto-renderer-blocks.tsv` and `crash-renderer-block-sources.tsv`: every executed renderer QEMU basic block, execution frequency, instruction/interlock/cache costs and source start. QEMU blocks may split at translation boundaries; they are not compiler CFG blocks.
- `crash-touched-instructions.tsv`: every executed instruction PC and frequency.
- `crash-renderer-annotated.asm`: full renderer disassembly with source locations and executed-count annotation; zero-count ranges expose cold parts inside otherwise hot functions.
- `hotblk.txt`, `lineprof.txt`, `cprof.txt`, `hz.txt`: repository profilers, including source-line aggregation and hottest blocks. Source snapshots remain in the private sim tree.

Crash default cold observations: no line/sprite path, texture allocation, fine RTT readback, CPU upload/push, stats/probe or timer sample body executes in the steady window. Texture decode **does execute** (~49k instructions/vblank in split), cache validation ~19k, and clipping is ~61k; these remain genuine parts of the working set even though out-of-line helpers keep callers small. `cold` is a compiler size/placement hint, not a claim that these paths never run. Draw-poly slow path remains necessary (~14.5k instructions/vblank). Error/incomplete-command handling is retained; exhaustive host prefix tests cover it.

## Simulator results

All values per emulated vblank, window 301–600. Cycles = instructions + 70×I-misses + 70×D-read misses + 4×D-write misses; interlocks are separately reported in metrics, not added to this established cprof model. No model value is a hardware speed claim. Cache misses are underestimated (~3× historically) and layout effects are not trustworthy.

| Game | Build | Whole-core instructions | Whole-core modeled cycles | gpu_pica instructions | gpu_pica modeled cycles |
|---|---|---:|---:|---:|---:|
| o3auto | baseline | 2,572,829 | 4,079,380 | 1,054,744 | 1,328,987 |
| o3auto | split | 2,574,075 | 3,956,965 | 1,058,588 | 1,268,962 |
| o3auto | section | 2,574,095 | 4,002,601 | 1,058,588 | 1,292,053 |
| o3auto | os | 2,751,221 | 4,206,615 | 1,233,936 | 1,472,924 |
| sh | baseline | 2,762,750 | 6,322,410 | 751,668 | 1,768,783 |
| sh | split | 2,774,514 | 5,860,489 | 765,363 | 1,473,064 |
| sh | section | 2,774,532 | 6,008,367 | 765,363 | 1,533,762 |
| sh | os | 2,868,468 | 6,148,623 | 855,593 | 1,678,372 |
| r4 | baseline | 1,819,638 | 2,899,219 | 444,767 | 570,639 |
| r4 | split | 1,833,561 | 2,874,551 | 463,567 | 558,325 |
| r4 | section | 1,833,258 | 2,869,686 | 463,567 | 562,254 |
| r4 | os | 1,879,308 | 2,950,788 | 505,900 | 629,602 |
| ctr | baseline | 2,815,236 | 5,127,807 | 893,382 | 1,274,156 |
| ctr | split | 2,839,774 | 5,041,687 | 923,753 | 1,230,732 |
| ctr | section | 2,839,770 | 5,072,147 | 923,753 | 1,242,978 |
| ctr | os | 2,951,427 | 5,203,154 | 1,031,059 | 1,364,284 |
| gt2 | baseline | 3,667,118 | 6,663,285 | 1,046,956 | 1,578,951 |
| gt2 | split | 3,684,187 | 6,333,857 | 1,069,130 | 1,358,303 |
| gt2 | section | 3,684,190 | 6,462,827 | 1,069,130 | 1,431,117 |
| gt2 | os | 3,822,893 | 6,621,462 | 1,203,240 | 1,603,661 |
| sotn | baseline | 1,567,761 | 3,136,478 | 652,347 | 981,610 |
| sotn | split | 1,592,878 | 3,118,345 | 684,014 | 981,235 |
| sotn | section | 1,592,853 | 3,111,219 | 684,014 | 977,760 |
| sotn | os | 1,665,793 | 3,346,840 | 753,022 | 1,204,284 |

Crash modeled whole-core split −3.00%, section −1.88%, Os +3.12%. Split/section **increase** renderer instructions by ~0.36%; modeled gains come from cache behavior. Os adds ~17% renderer instructions in Crash. Hardware may reverse this ordering, as the prior whole-app ordering regression demonstrated.

## Alignment experiment omitted

The baseline dynarec trace touches **all 128 I-cache sets** (see `dynarec-set-coverage.json`). A 4 KiB phase shift repeats the same set indices, and the selected 8.7 KiB renderer spine spans every set more than twice. No measured production dynamic-PC distribution establishes an offset that avoids competing hot lines. Therefore no speculative 4 KiB alias-avoidance CIA is supplied. Section alignment is only 32 bytes, to begin on a cache-line boundary.

## Picture and parser checks

- Each candidate matches baseline on 6×600 per-frame cumulative draw hashes, in both default and full named-prototype-stack modes: **21,600 frame comparisons** overall.
- Baseline final 600-frame draw hashes: `o3auto c15806a28d454f0d`, `sh 2ec86777a402338c`, `r4 97b524f3c526c8cd`, `ctr c97e59920c262b7a`, `gt2 42140bec85154fa8`, `sotn 221910a7e8aeb7a1`. Full-stack hashes are identical in these scenes.
- Sanitized polygon size/edge, RTT activation/reset, presentation/held-picture lifetime suite passes.
- Sanitized line test exercises all 32 GP0 line/polyline opcodes and every prefix length, including incomplete sequences after prior timed primitives; command consumption, accumulated timing, last opcode, draw hash and final pixel checksum exactly match baseline.
- Patch reverse-apply check and whitespace checks pass. ELF executable LOAD covers `.text.pica_hot`; CIA and 3DSX packaging succeeds. Split/section retain identical ordering of all 6,193 non-renderer function symbols (`non-renderer-order.txt`).

Draw hashes are simulator evidence, not a hardware screenshot/precision guarantee. Full native VRAM replay was not rerun for this layout-only experiment; existing edge/presentation and new line-prefix fixtures provide additional behavioral coverage.

## Hardware handoff (performance worker only)

Artifacts:

- `/home/tony/Development/PS1_3ds/devtools/pica-layout/baseline/PSX3D.cia`; remote build `pica-layout-baseline`; SHA256 `950bd724aeba37df9d431417993661c7bd56f4ebd1984f9e186513989425d92b`.
- `/home/tony/Development/PS1_3ds/devtools/pica-layout/split/PSX3D.cia`; remote build `pica-layout-split`; SHA256 `56a9950c26e65591d58047b463bb388bbf0859c4379ab14c79d994f66fc6adee`.
- `/home/tony/Development/PS1_3ds/devtools/pica-layout/section/PSX3D.cia`; remote build `pica-layout-section`; SHA256 `792a509cca139552785afdfdbec1b1ba1b074bb059b38b75d6b7f160badd9f79`.
- `/home/tony/Development/PS1_3ds/devtools/pica-layout/os/PSX3D.cia`; remote build `pica-layout-os`; SHA256 `750586be2588afeffde6b879f83da0700b9fd3dcff7f968e160fb09a3b12eed9`.

`bench.txt`: Crash, SH, R4, CTR, GT2, SotN fight; protected Old state 2; GPU; frameskip off; RTT auto; prototype switches explicitly old; pmurender on. Each game has four 20s measurements after 5s warmup: `pmu=0,b`, `1,2`, `8,d`, then same-session repeat `0,b`. `crash.txt` is the first game only. `bench-stack.txt` / `crash-stack.txt` repeat this with the named submission/primitive/texture prototypes enabled (skipgate stays old; frameskip is off).

Use the existing performance-worker driver inside its existing queue/lock, keeping the same low-NTR condition across every CIA. Do not start an independent scheduler. Example exact commands (prepared only, **not executed here**):

```bash
D=/home/tony/Development/PS1_3ds/devtools
python3 "$D/o3ds60/runbench.py"
  "$D/pica-layout/bench.txt" "$D/pica-layout/hw-baseline-1"
  --cia "$D/pica-layout/baseline/PSX3D.cia" --build pica-layout-baseline
python3 "$D/o3ds60/runbench.py"
  "$D/pica-layout/bench.txt" "$D/pica-layout/hw-split-1"
  --cia "$D/pica-layout/split/PSX3D.cia" --build pica-layout-split
python3 "$D/o3ds60/runbench.py"
  "$D/pica-layout/bench.txt" "$D/pica-layout/hw-baseline-2"
  --cia "$D/pica-layout/baseline/PSX3D.cia" --build pica-layout-baseline
```

Fast screening: `screen-crash.txt` contains two same-session Crash runs with 3s warmup and 10s sampling each, `pmu=0,b pmurender=on`. Insert baseline → split → baseline into the existing queue: 78s measurement/warmup total plus installs. No source merge is needed. Section is the next optional screen; Os is lower priority because it adds ~17% renderer instructions.

Repeat baseline/section/baseline and baseline/os/baseline, preferably screening `crash.txt` first. Keep the existing same low-NTR condition; these follow-up numbers must not be labeled stream-free. Compare renderer cycles, instructions, I/D misses, starved/hazard events and whole-app FPS/cycles together; retain requests/session IDs. Every CIA preserves the original PMU wrappers. No performance acceptance or integration recommendation until that test.

## Reproduce

From this branch, after the ordinary submodule/patch setup:

```bash
tools/pica-layout/build.sh
tools/pica-layout/check.sh
# Or one variant; root make invalidates affected objects automatically:
source /home/tony/Development/PS1_3ds/devtools/env.sh
make cia PICA_LAYOUT=split BANNERTOOL=... MAKEROM=...
```

Private sim build/run sources, logs, cachesim dumps, profiler outputs, per-frame hashes, annotated assembly, manifest and metrics are under `/home/tony/Development/PS1_3ds/devtools/pica-layout/`. `sim/matrix.sh` builds/profiles/hashes all four variants without contacting hardware. `analyze.py` and `write-report.py` regenerate this report from the evidence.

## Short follow-up and source integration

The performance worker confirmed Crash A/B/A under fixed low NTR: baseline1
42.061, split44.092, baseline2 42.205fps. Split is +4.65% versus the mean
baseline, with0.34% baseline drift. Renderer cycles2.734→2.453M and I-cache
misses17.50→12.44k. This accepts the Crash screen; six-game/full-stack
interaction is still required before choosing a default.

Ignored `short-default-{1,2,3}.txt` and `short-stack-{1,2,3}.txt` each contain
four rows: two repeats each for Crash/SH, R4/CTR, or GT2/SotN, respectively.
Every row uses3s warmup,10s sample, GPU, frameskipoff, RTTauto,
`pmu=0,b pmurender=on`, protected state2, skipgateold. For each pair of games,
use baseline-default→split-default→baseline-stack→split-stack through the
worker's existing serial queue, releasing its lock after each four-run batch.
This is48 runs,624s warm/sample plus installs/protocol overhead. Do not start
another console scheduler. Highskip/build18 remains canceled.

A direct root merge conflicts in the serialized core patch and build identity.
`tools/pica-layout/merge-source.py` resolves this through actual sources:
reconstruct base/current/candidate core files, run a three-way source merge,
regenerate patch0002 against upstream, and prepare a normal two-parent merge
commit on `t3/pica-layout-on-o3ds60`. It never changes either worktree. Other
root files use Git's normal merge result; the target build number and status
fields are retained, with `-pica-<layout>` appended to the identity. Only core
Makefile, gpu_pica.c, gpulib/gpu.c and the new pica_layout.h change. Idle, class,
frontend, PMU and frameskip source remain the worker's versions.

After the worker commits its intended pending source, prepare a fresh merge:

```bash
python3 /home/tony/.t3/worktrees/PS1_3ds/t3-82d2c88c/tools/pica-layout/merge-source.py \
  --target t3/o3ds60
# Review integration/identity.txt and core-source.delta under devtools/pica-layout.
# In the performance worktree, after acceptance:
git merge --ff-only t3/pica-layout-on-o3ds60
```

The patch-backed submodule must be refreshed after merging: in the worker,
reverse-apply the PRE-MERGE patch0002 to its actual core, then apply the merged
patch0002 (patch0001 stays applied). Preserve the old patch before merging;
check its reverse application first. Do not run root make with an old patched
core and a new patch0002. The script's isolated source reconstruction makes no
assumption about the state of an active worker core.

Build the integrated split with `make cia PICA_LAYOUT=split`; baseline remains
the build default until the maintainer explicitly adopts a winner. The new
link address layout needs a short hardware confirmation even after the old-base
matrix wins. Full-stack runtime defaults are a separate maintainer decision;
the merge leaves all prototype settings unchanged.
