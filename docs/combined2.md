# Combined2 GPU-only release candidate

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

The archived **console** Crash clip diverges at RAM checkpoint 60 when replayed
in the Linux simulator, repeated twice. Console-to-simulator portability is not
promised by the format; the required same-platform recorder/replay passes. The
exact archived clip must still pass on Old hardware. This diagnostic is retained
in `replay/{archived1,archived2}/`, not counted as passing determinism evidence.

## Old 3DS acceptance

**BLOCKED, not hardware accepted.** Before opening a console connection,
preflight found a newly created `fastloop/OLD_UNAVAILABLE` marker dated
2026-10-19 03:59:52 UTC. It reports a crash during the separate gpu-stereo
candidate batch and explicitly prohibits contact until maintainer clearance.
This worker made **zero console connections, installs, loads, screenshots or
inputs**, and never acquired the console lock. It did not create, clear or alter
the shared marker. Exact RC2 restoration and current library state are therefore
**not verified by this task**; they belong to recovery before the next session.
The local marker snapshot is `HARDWARE-BLOCKED.md` in the evidence package.

| Required hardware result | Status |
| --- | --- |
| RC2 A1 / remove-sw / combined2 / exact RC2 A2 four-game FPS gate | PENDING |
| remove-sw-only layout effect | UNMEASURED |
| CTR/GT2/SH auto game speed | PENDING |
| SotN GPU off/auto and title/menu/fight picture quality | PENDING |
| Nearest SPU default/on/default Crash/CTR/GT2 gain | UNMEASURED; no simulator gain presented as hardware |
| Archived console Crash clip replay twice | PENDING |
| Exact RC2 restored, remote-idle library and reviewed shot both | NOT VERIFIED; no console contact |

**Verdict: offline-ready candidate; do not promote until the Old hardware gate
passes.** New checks remain pending under the explicit reservation. Nearest SPU
is default OFF, and listening/quality acceptance is also pending. Old ordinarily
mixes SPU on the time-sliced syscore, so modeled synchronous-SPU savings are not
assumed to improve main-core speed.

The exact predeclared sequence/acceptance criteria are in
`PENDING-HARDWARE.md`; batches in `batches/`, one lock-holding driver in
[tools/combined2/hardware.py](../tools/combined2/hardware.py). After recovery,
require idle RC2 under the lock, then RC2 A1 → remove-sw four → combined2
four/auto/SotN/SPU brackets/archived clip/SotN pictures → exact RC2 → A2.
Every ordinary row uses GPU/slot2/clock57, warmup10/sample15, PMU0,2.
Acceptance requires every default game ≥99% of the arithmetic RC2 bracket mean;
report closing drift and remove-sw layout deltas separately. No install retries.
The driver holds the lock until the final screenshot is locally reviewed.

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

The report commit follows. Intermediate cherry-pick commits preserve provenance;
only the final tree is the requested production surface. Rejected algorithm code
was never imported. Host-only temporary source controls do not ship as switches.
