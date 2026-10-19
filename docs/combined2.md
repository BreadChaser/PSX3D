# Combined2 GPU-only release candidate

**Old hardware gate: PASS.** Crash +1.64%, CTR +14.46%, GT2 +6.24%, Silent Hill
+7.92% versus the RC2 bracket mean. Exact RC2 restored and visually verified;
`lock.old` released. New console checks remain reserved.

Based on `t3/remove-sw` `c8bcd51`. Frozen executable source `047dded`, remote
identity `combined2-1-split`, version 0.3.1/title counter 3076, split PICA layout,
devkitARM GCC 16.1. Evidence/artifacts are in
`/home/tony/Development/PS1_3ds/devtools/combined2/`. No push or publication.

| Included | Final behavior |
| --- | --- |
| GPU-only base | No gpu_unai, dual dispatch or renderer selection |
| Lookup + NCLIP (`55dcec8`, `5f3d4e3`, `90a1c95`) | Always on; switch globals/parser/status and obsolete indirect-exit shims/table removed. Flagless ARMv6 NCLIP replaces the C determinant; needed flags retain their accurate handler. The general lookup miss resolver remains required. |
| Relocated scratchpad (`10c2afa`) | Always on; original 4 KB unsigned range guard, original-address slow stub on failure, reserved LR native address translation. No `dyn3_mem` switch, allocator or timing change. |
| G1 (`268198d`) | Always-on cold movie scanout synchronization; no `g1sync` switch |
| Shared borders (`52d68c6`) | Full output-height clear once per alternating buffer/range change; no setting |
| Developer clips | Opt-in recorder/replay, isolated clip files and RAM checkpoints; synchronous SPU/CD during clips |
| Nearest SPU interpolation | Only `hack_spunearest=on|off`, default OFF each clean remote load; reuses existing interpolation mode 0, preserves normal per-console defaults when off. Audio quality trade, eligible for later per-game/per-console curation. |

Rejected `dyn_gtecode`, `dyn_regpair`, `skipfast` and `hack_idleptr` implementation
changes are absent. The base's pre-existing idle investigation switches are
unchanged. The optional `screen=` diagnostic was dropped. No New console contact.

## Footprint and package

| Section | RC2 | remove-sw | Combined2 | vs RC2 | vs remove-sw |
| --- | ---: | ---: | ---: | ---: | ---: |
| `.text` | 2,146,052 | 1,789,684 | 1,801,964 | −344,088 (−16.03%) | +12,280 (+0.69%) |

CIA SHA256 `4ccad3b85a2b89b88880648db5562ac190987f6e27ee5aef9291b7fc66962a6e`.
Immutable CIA/3DSX/ELF, complete SHA256SUMS, compiler/source manifest and raw section
listings live in `artifacts/`. ELF/CIA sizes do not predict hardware speed.

## Offline gates

All cores/simulator builds are private copies from this worktree or the exact
remove-sw control. RAM and scratchpad relocate; scratchpad uses representative
native address `0x0fde0000` to exercise the 3DS guard/translation path. Production
translation cache capacity is retained. Shared simulator sources are untouched.

| Gate | Result / evidence |
| --- | --- |
| CIA/3DSX build, counter 3076 | PASS, `build-frozen.log` |
| Developer parser/reset/status/lifecycle/TCP/client/updater ASan/UBSan | PASS, `dev-tests-final.log` |
| Frontend host controls/config/presets/UI, SPU override and both-console defaults | PASS, `host-tests-final.log`, `host-pictures/` |
| Clip codec sanitizer | PASS, `codec-tests.log` |
| Real CPU output border sanitizer | PASS, zero dirty border rows RGB15/RGB24, `vout-tests.log` |
| Renderer/G1 actual-vout regression | PASS, `regress.log` |
| NCLIP arithmetic vs original C | PASS, 531,441 extreme + 1,000,000 random full-register cases, `oracles/nclip.log` |
| Interpreter differential | PASS, 2,000 original programs plus 6,000 RAM/scratchpad byte/half/word/LWL/LWR programs, `oracles/{micro,micro2}.log` |
| SMC and guard failures | PASS, 2,000 guest-SW invalidations plus cached base transitions and eight range boundaries, `oracles/micro2.log` |
| Six states × 600 frames vs remove-sw | PASS, all draw hashes, 60-frame RAM/VRAM/GPR/CP2/PC/cycle checkpoints and final serialized bytes |
| Non-dynarec six-state control | PASS, same complete identity |
| Dynarec integrated six-state control | PASS, same complete identity |
| 31 real-BIOS boots × 1,200 frames | PASS, full dynarec identity vs remove-sw and with movie fixes held constant; `boots-dyn-vs-remove.json`, `boots-dyn-integrated.json` |
| 31 HLE cold boots × 1,200 frames | PASS, both full dynarec controls; explicit HLE and empty BIOS directory verified in all 124 runs; `HLE-PASS.json`, `hle-dyn-vs-remove.json`, `hle-dyn-integrated.json` |
| Crash scripted capture + two replays | PASS, 600 frames/10 checks each, zero divergent checkpoints, `replay/results.json` |
| Driver dry-run / no-socket safety checks | PASS, `driver-tests.log` |

The full build's movie VRAM intentionally differs from remove-sw: G1 settles
previously stale CPU VRAM on RGB24 scanouts and serialization. All 31 boot
RAM/GPR/CP2/PC/cycle timelines match. `compare-boots-movie-fixes.json` retains
unfiltered differences rather than claiming full byte equality. A source-only
control with all movie fixes and original dynarec isolates dynarec identity;
`compare-boots-dyn-final.json` passes all 31 complete timelines and serialized
states. A second source-only control with only dynarec changes directly checks
full identity against remove-sw. These controls are host evidence, not production
runtime switches. Boot logs verify real BIOS CRC32 37157331; six supplied states
restore HLE, so their runs are not claimed as real-BIOS gameplay tests.

HLE is the primary configuration, including on the Old console. The additional
cold-boot matrix uses `tools/dynarec-speed/run.py --mode boot --bios hle` with an
isolated empty BIOS directory; every log confirms HLE initialization. Both full
guest-state comparisons pass all 31 games. This proves preservation against the
baseline, not universal HLE compatibility. An initial duplicate-option trial
selected real BIOS because the runner takes the first matching key; it is
retained separately in `hle-first-match-diagnostic/` and is not counted. The
driver now replaces the BIOS entry and records the explicit selection.

The archived **console** Crash clip diverges at RAM checkpoint 60 when replayed
in the Linux simulator, repeated twice. Console-to-simulator portability is not
promised by the format. The exact archived clip subsequently passed twice on the
Old console: 900 frames, 15 checks, zero divergent checkpoints in each replay.
The simulator diagnostic is retained in `replay/{archived1,archived2}/`, not
counted as passing determinism evidence.

## Old 3DS acceptance

**PASS: accept combined2 for the requested Old hardware gate.** All four games
exceed the RC2 bracket mean; none breaches the −1% threshold. The complete
single-driver session held `lock.old` from 04:06 to 04:31 UTC on 2026-10-19.
No New console contact occurred.

Sequence: exact RC2 A1 → remove-sw → combined2 defaults/auto/SotN/nearest-SPU
brackets/archived replay/pictures → exact RC2 A2. Every ordinary run used GPU,
slot2, clock57, warmup10/sample15, PMU0,2; auto rows were explicitly selected.
Recording was off during performance tests. A1/A2 settings matched, excluding
free-memory counters. There were 30 ordinary rows plus two replays, 490 sampled
intervals and zero black **benchmark samples**. Raw requests, status/stats
histories, TSVs, screenshots and analysis are in `hardware/old-session-1/`.

| Game | RC2 A1 FPS | RC2 A2 FPS | RC2 mean FPS | Combined2 FPS | Change | Verdict |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Crash | 50.380 | 50.346 | 50.363 | 51.188 | +1.64% | PASS |
| CTR | 31.880 | 31.819 | 31.849 | 36.455 | +14.46% | PASS |
| GT2 | 31.906 | 31.927 | 31.917 | 33.907 | +6.24% | PASS |
| Silent Hill | 38.897 | 38.840 | 38.869 | 41.946 | +7.92% | PASS |

Closing A2/A1 drift: Crash -0.066%, CTR -0.191%, GT2 +0.068%, Silent Hill -0.147%.
All are below the predeclared 2% drift flag.

The remove-sw-only removal/layout effect is measured separately:

| Game | remove-sw FPS | vs RC2 mean | Combined2 vs remove-sw |
| --- | ---: | ---: | ---: |
| Crash | 51.418 | +2.09% | -0.45% |
| CTR | 31.879 | +0.09% | +14.36% |
| GT2 | 31.830 | -0.27% | +6.53% |
| Silent Hill | 38.813 | -0.14% | +8.07% |

The combination adds speed on CTR/GT2/SH beyond software removal. Crash gives
back 0.45% versus remove-sw while remaining 1.64% above RC2. These measurements
include the final code/layout cost of the recorder and all integrated features.

### Frameskip and SotN

FPS here means emulation rate; with auto frameskip, game speed is the relevant
quantity. It does not mean every emulated frame was drawn.

| Game | Frameskip | Emulation FPS | Game speed |
| --- | --- | ---: | ---: |
| CTR | auto | 49.057 | 81.76% |
| GT2 | auto | 46.711 | 77.85% |
| Silent Hill | auto | 59.999 | 100.00% |
| SotN | off | 57.345 | 95.58% |
| SotN | auto | 60.011 | 100.02% |

SotN picture review passed the requested sampled check: title after a cold HLE
boot, menu from load-only slot8 and Dracula fight from load-only slot2 were
coherent, without obvious white blocks or foreign border rows. Reviewed all six
menu captures and 40 cold-boot captures; the first two cold-boot captures were
black before the intro movie, with subsequent movie/logo/title pictures visible.
See `sotn-title-menu-fight.png`, both contact sheets and `picture-review.json`.
Sparse captures do not establish continuous flicker absence.

### Nearest SPU interpolation

Same binary, same game, frameskip off, default → nearest → default. Changes use
the arithmetic mean of the two adjacent default runs.

| Game | Default 1 FPS | Nearest FPS | Default 2 FPS | Nearest change |
| --- | ---: | ---: | ---: | ---: |
| Crash | 51.049 | 51.154 | 51.028 | +0.23% |
| CTR | 36.588 | 36.853 | 36.504 | +0.84% |
| GT2 | 33.966 | 33.894 | 33.884 | -0.09% |

**Keep `hack_spunearest` default OFF.** These small Old gains do not establish
a general preset recommendation. Old SPU mixing normally runs on the syscore;
CTR SPU time fell from a neighbor mean of 1.467 to 1.157 ms, yielding only
0.84% FPS. Crash/GT2 SPU time was essentially unchanged. Simulator savings are
not substituted for hardware measurements. Listening/quality acceptance remains
pending; nearest is an explicit audio-quality trade. Occasional underruns were
counted in both RC2 and candidate logs.

### Recorder and handoff

The SD neutral Crash clip was byte-identical to the archived combined1 clip.
Both hardware replays passed 900frames/15checks with zero divergence. No new
gameplay input or protected-slot write was sent.

Exact RC2 CIA SHA256
`5fa3d650a11a8f2b0c9d247e3d0c1fe1c51af7818a4fef07c2befcec57b07660`
was installed, returned a changed session `6ac68174f2`, and completed A2. Final
status verified `release-v031-rc2-split`, library mode and inactive batch;
`restored-final.png` was visually reviewed **before** the driver released the
lock. PSX3D was left remote-idle in the library. `complete.json` and
`FINAL_REVIEWED` record the handoff. There were no install failures or retries.

The passive observer recorded zero Old UDP packets throughout all batches.
No NTR stream request or configuration change was issued; the inherited NTR
condition is preserved in `session-plan.json` and per-batch observer snapshots.

The earlier preflight hold at 03:59 UTC is retained in `HARDWARE-BLOCKED.md` as
history. The maintainer subsequently cleared the marker and confirmed idle
RC2; this completed session supersedes that blocked verdict. New hardware checks
remain pending under the explicit reservation.

Reproduce local analysis without console contact:

```sh
python3 tools/combined2/analyze.py /home/tony/Development/PS1_3ds/devtools/combined2/hardware/old-session-1
```

## Developer clip use

`record=on record_seconds=15` enables the pause-menu **Record test clip** action,
or use remote `record start` after loading a game. Recording remains disabled in
normal play. Files go to `recordings/<safe_game>/<id>.state|inp`, never save
slots. Fetch using `tools/input-replay/fetch.py`. A batch uses `replay=<id>`
instead of `state=`; use matching interpolation/idle settings. Clips force
synchronous sound/CD, so their speed is not normal threaded gameplay speed.
The hardware plan replays existing archived neutral Crash clip1 without writing
any protected slot or sending gameplay input.

## Commit list

All commits are local on `t3/combined2`, based on `c8bcd51`:

| Commit | Purpose |
| --- | --- |
| `53e5498` | Private simulator harness |
| `45d95fc` | Accepted shared ARM indirect lookup |
| `ba5e113` | Accepted ARMv6 flagless NCLIP/oracle |
| `56828df` | Shared lookup register shims |
| `ecd2546` | Isolated saves/interpreter oracle |
| `a56ed9e` | G1 movie synchronization |
| `d15d0df` | Shared alternating-output-buffer clear |
| `4aea0f0` | Clip codec/core boundary |
| `ce04363` | Console recorder/control integration |
| `83d0675` | Clip host/simulator tools |
| `2d23527` | Clip policy/serial validation |
| `047dded` | Final always-on cleanup, scratchpad fast path, nearest-SPU-only switch and integration tests; frozen executable source |
| `1d6000d` | Exclusive hardware driver/safety tests and stronger scratchpad/SMC oracle |
| `a75bf04` | Initial offline report and frozen hardware package |
| `55edf4c` | Explicit HLE cold-boot selection and local hardware analysis |

The final hardware report/analysis commit follows. Intermediate cherry-pick commits preserve provenance;
only the final tree is the requested production surface. Rejected algorithm code
was never imported. Host-only temporary source controls do not ship as switches.
