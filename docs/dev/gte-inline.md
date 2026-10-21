# GTE call overhead on ARM11

This experiment builds from `t3/core-first` (`6083ee9`). Keep the specialized
flagless NCCT path always on; drop inline NCLIP and merged MVMVA MAC-to-IR.
The final NCCT-only Old 3DS A/B/A passes: Crash gains 1.92% with frameskip off
and 0.54% with Auto; no scene loses more than 1%. Arithmetic, GTE stalls,
guest block boundaries and cycle costs stay unchanged. Two combined binaries
were rejected for a Silent Hill alley regression before selecting NCCT alone.

The ARM11 is ARMv6K and **has no NEON**. The ARM/NEON suggestion in the source
performance review is wrong. That review's gain and ceiling estimates are
hypotheses; the numbers below are new Old 3DS measurements.

## Independent measurements

One CIA (`gte-inline-2`), clean load with `gte_inline=0/1/2/4`, default off;
off/on/off/on, 3 s warm-up and 20 s sampling per scene, GPU, HLE, PMU 0,2,
all other player defaults. Values average two samples per mode. These longer
runs are compared only with each other, not with the 12-second full-scene gate.

| Sub-change | Scene | Off speed % | On speed % | Relative change | Selection |
|---|---|---:|---:|---:|---|
| Inline NCLIP | CTR | 82.572 | 83.162 | +0.71% | drop after whole-binary gate |
| Inline NCLIP | GT2 | 52.813 | 52.761 | -0.10% | drop; flat |
| Inline NCLIP | Tekken 3 | 66.260 | 66.606 | +0.52% | drop after whole-binary gate |
| Inline MVMVA MAC-to-IR | GT2 | 52.775 | 52.700 | -0.14% | drop |
| Inline MVMVA MAC-to-IR | CTR | 82.457 | 82.579 | +0.15% | drop |
| Specialized NCCT | Crash 1 | 85.791 | 88.192 | +2.80% | keep always on |
| Specialized NCCT | Crash 2 | 78.276 | 78.243 | -0.04% | keep; flat here |

MVMVA's CPU cycles did not fall either. Removing a call was insufficient to
justify its extra generated code. The final source has no development switch or
alternative MVMVA path.

## Implementation

`core/libpcsxcore/new_dynarec/assem_arm.c`:

- NCLIP (dropped): packed ARMv6 `SMUSDX` cross products for dead flags and
  `SMLSLDX` 64-bit determinant/overflow tests for live flags. Only r0-r3/r12 were
  scratched; outer saves/stalls were retained. This removed the far call, but
  increased generated code and failed the whole-binary gate. The final source
  uses the original NCLIP helpers.
- NCCT: for sf=1 with dead flags, an integer C helper computes the same three
  lighting/color pipelines with signed 64-bit accumulators, saturation, RGB
  CODE, MAC and IR writes. Signed-44 wrap multiples vanish modulo 32 bits after
  shift 12. IR writes preserve the high halfword, matching the original C
  handlers. Live flags and sf=0 use the original handler.
- MVMVA: original code retained. Its existing flagless LM1 `USAT #16` behavior
  differs from the live-flag `USAT #15`; this experiment does not change it.

No stalls, cycle costs, block cuts or event checks were removed. This avoids
per-block rounding and timing changes like those in the earlier dynarec rounds.
The final helper's disassembly uses integer ARM instructions, including
`SMLALBB` and `USAT`; it has no NEON instructions. Existing save/restore and
far-call machinery remain intact for this helper.

## Correctness

NCCT-only source `e47e20a`, identity commit `b983945`:

| Gate against unchanged core-first | Result |
|---|---|
| 13 heavy states x 600 frames | identical |
| 6 standard states x 600 frames | identical |
| 63 HLE cold boots x 1,200 frames | identical, all exit 0 |

Every-frame draw hashes, 60-frame RAM/VRAM/GPR/CP2/PC/cycle checkpoints,
IRQ/exception digests and complete serialized state are compared. Results:
`devtools/gte-inline/cmp-final5-{heavy,std,boot}.json`.

The experimental microtest executes 12,000 random/edge guest programs through
all eight codegen combinations, with independent live/dead FLAG choices per
opcode, random initial GTE busy cycles, all MVMVA control fields and signed
coordinate/matrix/overflow/saturation edges. It compares whole GPR/CP2,
cycle/subcycle and GTE/muldiv busy-cycle state with unchanged codegen. Some
NCCT programs omit MFC reads so the IR high halfwords remain observable.

`microtest-final2.log` passes all combinations. NCLIP, NCCT and live-FLAG MVMVA
also match the interpreter. There are 487 **pre-existing dead-FLAG MVMVA**
interpreter differences, deliberately preserved for the required core-first
identity. Interpreter cycle identity is not claimed. The final binary is also
checked against 12,000 snapshots recorded from the unchanged codegen; all
records match (`microtest-5-production.log`). The production build ignores
the experimental bitmask, so this comparison uses the saved external oracle.

The unchanged reference uses production assertions off in `psxbios.o`; the
NCCT-only simulator is built with production assertions off throughout:
unchanged Crash 2 asserts at frame 575 with assertions on. The complete final
600-frame run is included. Earlier incomplete host launches caused by relinking
an active simulator runner were preserved and rerun; final gates have no failed
or truncated runs. Microtest builds now leave the scene runner alone.

## Layout, artifacts and hardware

Final `NDRC_LAYOUT_PAD=1908`, versus 2460 in core-first, compensates for 552
bytes of dynarec object growth. `layout-5.txt` confirms that cleanup and all
subsequent core/library text and the frontend text keep baseline addresses.
Internal dynarec functions still move. All compared core constants, tables,
strings, data and BSS keep baseline addresses (`layout-compare-5.txt`). The
strict baseline comparison fails only for changed dynarec code, as expected.
`tools/layout/check.sh` passes frontend growth and extra-libm tests.

The final code selects the NCCT handler in an outlined compiler-only wrapper.
This preserves the original opcode switch and its jump table, so all compared
core tables retain their addresses. It does not add a generated guest call.
Internal dynarec functions still move by 520/552 bytes.

Earlier build 3 widened c2op_assemble's jump table from 62 to 126 bytes and
moved 63 core table symbols by 64 bytes. Outlining the NCLIP emitters fixed the
table layout in build 4, but its Old alley regression remained. Build 3 was
never installed; its blocked waiter was stopped by PID before taking the lock.

Recompute the pad after integration, as described in `dynarec-r4.md`.

Immutable final CIA/3DSX/ELF: `devtools/gte-inline/artifacts/`, source `b983945`,
identity `gte-inline-5`, counter 4100. CIA SHA256:

```
655a1f8c7e37d3044ea158f5c2e63b6c1d9d5de5f348841f1faca3c04b0c659c
```

The exact CIA reached the library and Crash gameplay in Azahar before any
hardware installation. Screenshots `azahar/exact-{library,game}-5.png` were
reviewed. Azahar proves startup; emitted ARM is exercised by the ARMv6 simulator.

Old measurements use all 13 heavy scenes with frameskip off + Auto, HLE,
GPU, PMU 0,2 and per-load overrides only. Warm-up/sample is 3/12 seconds except
NFS at 1/5 seconds, matching core-first's recorded batch. Speed deltas compare
B with the arithmetic mean of the A1/A2 speeds; drift is A2/A1 minus one.
The established core-first 13-scene batches derive from the heavy-scene set,
use the Old console's GT2 title, and omit Mega Man X4. Exact copies are saved
as `batches/final5-heavy-{off,auto}.txt` in the evidence directory.

- Build 2 (all experiments and frontend switches) failed: SH alley -1.30% off /
  -1.49% Auto, drift -0.06% / -0.27%. Independent switch gains above remain valid.
- Build 4 (NCLIP+NCCT always on, all compared tables pinned) also failed:
  SH alley -0.98% off / -1.46% Auto, drift -0.14% / -0.33%. Crash 1 off gained
  +1.95%; Tekken gained +0.88% off / +1.16% Auto. All 78 runs succeeded,
  zero near-black probes. RC4 was restored at 2026-10-21 03:40:20 UTC;
  status and library screenshot were reviewed. This rules out moved core
  tables as the sole cause of the alley loss; no stronger root cause is claimed.
- Build 5 removes NCLIP and keeps NCCT only. Its full A/B/A passed all 78 runs,
  with zero near-black probes across 894 samples. Worst off delta is NFS
  -0.32%; worst Auto is Crash 2 -0.93%. The mean of the 13 scene deltas is
  +0.14% off / +0.08% Auto, so this is a targeted Crash improvement.

## Final Old 3DS A/B/A: NCCT only

Core-first-2 A1 → gte-inline-5 B → core-first-2 A2, 2026-10-21
06:15:57–06:48:39 UTC. Values below are relative speed changes, not fps.

| Scene | Off delta | Auto delta | Off drift | Auto drift |
|---|---:|---:|---:|---:|
| Crash / 5 | +1.92% | +0.54% | -0.16% | -0.12% |
| CTR / 5 | +0.14% | +0.16% | -0.12% | -0.09% |
| R4 / 5 | -0.17% | -0.06% | -0.17% | -0.15% |
| SotN / 5 | -0.25% | +0.06% | -0.27% | -0.02% |
| Silent Hill garage / 5 | +0.05% | +0.06% | +0.03% | -0.27% |
| Silent Hill alley / 6 | -0.13% | -0.05% | +0.05% | -0.30% |
| Silent Hill cafe / 7 | +0.06% | +0.20% | +0.14% | +0.52% |
| GT2 / 5 | +0.08% | -0.08% | -0.07% | -0.02% |
| Tekken 3 / 5 | +0.25% | +0.58% | +0.14% | +0.06% |
| FFVII / 5 | -0.01% | +0.05% | +0.00% | -0.13% |
| Klonoa / 5 | -0.02% | +0.64% | +0.02% | -0.07% |
| Crash 2 / 5 | +0.20% | -0.93% | -0.62% | +1.23% |
| NFS / 5 | -0.32% | -0.19% | +0.04% | +0.53% |

Crash off speed is 86.419 / 88.008 / 86.278% for A1/B/A2;
CPU cycles fall from the bracket mean 4.578M to 4.458M per frame.
Alley no longer shows the combined binaries' regression. Crash 2 Auto is
the least stable row: B is -0.32% versus A1 and -1.53% versus A2; its
-0.93% bracket result passes the approximately 1% gate, but the +1.23%
baseline drift limits conclusions about that sub-percent change.

The audit is complete with no issues. Candidate off/Auto contact sheets,
baseline contact sheets and the restored library screenshot were visually
reviewed. Exact RC4 was restored at 06:48:39 UTC, session `c1e3f49e4d4`,
library, bench inactive, no error; the shared lock was then released.

Complete tables and saved status/screenshots are in `hw/old-1/` and
`hw/old-final4/` for the rejected builds, and `hw/old-final5-safe/` for the
accepted build, with `summary.{md,json}` and `audit.json`. The final session
log is `hw-old-final5-safe.out`. Sparse probes/screenshots are not a flicker audit.

All Old access uses `fastloop/lock.old` and requires absent OLD_UNAVAILABLE,
idle RC4 handoff and no active bench. Each candidate gets one install attempt
and no retry; no return writes OLD_UNAVAILABLE and stops. Sessions restore exact
release-v041-rc4. No New 3DS contact, protected-slot writes, HOME, app exit,
reboot, FTP, InputRedirection, svcBackdoor or CP15 writes were used. Remote
`quit` in the existing fastbench only unloads a game to the library and keeps
PSX3D and its server running. Existing BootNTR conditions were left alone;
these results are not claimed to be stream-free.

## Reproduction and integration

Private tooling and raw evidence are under `devtools/gte-inline/`; see its
`REPORT.md` for the final acceptance status. Reusable scripts there are
`tools/prepare.py`, `tools/build.sh`, `tools/build-gte.sh`, `tools/run.py`,
`tools/compare.py`, `hw.py` (switch experiment), `hw5.py` (final NCCT-only),
`summarize.py`, `tools/audit-hw.py` and the exact batch files.

Code commits: `8f8aaf9`, `111f812`, `a5fb4d2`, `3a054d8`, `e47e20a`. Skip identity commits
`cbab323`, `cb58b90`, `92f3d20`, `fa48c61`, `b983945` when integrating. Apply
`e47e20a` after the earlier code commits: the net production change is only
the ARMv6-compatible integer NCCT path and the recomputed dynarec pad. No push or
publication was performed.
