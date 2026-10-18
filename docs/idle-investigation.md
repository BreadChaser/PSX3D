# PS1 waiting investigation

Research branch `t3/idle-inv`, requested base **a63bece**, 2026-10-18. The
release branch had advanced when work began, so this investigation deliberately
uses the supplied SHA. No console was contacted and nothing was pushed.

## Answer

We can skip the ARM work of simulating a PS1 wait, while still advancing the
PS1's clock and servicing its events. Much of that is already implemented.
The clock must continue: sound, CD/DMA, counters, interrupts and video all depend
on it. Removing elapsed PS1 time would change the game.

CTR does have a missed wait. A RAM pointer/flag polling loop accounts for
**64,389 PS1 cycles per emulated frame, 11.41%**, in frames 301–600 of the supplied
state. The opt-in pointer prototype removes those repeated iterations. It saves
about **84,000 ARM instructions per frame** and **2.14% modeled host cycles** (1.72% in an earlier separately built layout).
An additional stack-based counted delay accounts for approximately **1.62%** of
sampled guest cycles; it remains unaccelerated. Thus we identified about **13%**
of CTR's guest time as waiting in this scene. The other approximately 87% is a
remainder, not a proof that every instruction is useful. The dominant inspected
code performs geometry, culling, packet generation and data processing. Two
geometry ranges alone account for approximately 53% of sampled guest cycles.

The earlier report's “CTR 0% waits” was too strong. Nevertheless, this missed
wait does not explain the whole speed deficit. Going from 70% to 100% speed
requires **30% less host time per emulated frame**. Guest-cycle percentages
are not host-cost percentages: tight translated polling loops are cheap,
whereas GTE, memory handling, event handling and rendering can be expensive.
The simulator result is an experiment, not an Old 3DS FPS prediction. Its cache
model underestimates instruction-cache effects and does not run real PICA.

## Measurement and units

A frame here means one `retro_run`, approximately one NTSC 60 Hz video period,
not one new 30 Hz game picture. Normal elapsed time is **564,480 PS1 cycles per
frame**. Measured values differ by fractions of a cycle per frame because the
CPU stops at instruction/block boundaries. Skips do **not** reduce this number.

Three complementary measurements were used:

* Simulator-only counters in the actual generated skip paths record omitted
  **guest cycles, batches and iterations**. No forced sampling is enabled in
  these runs. A batch is a successful skip operation, not an entire wait or
  one detector compilation.
* Guest-PC samples (`IDLEPROF=257`, 600 frames, generic/specialized skip disabled)
  rank backward loops and their bodies. Upstream PsyQ compatibility skipping
  remains enabled. The sampler inserts extra scheduler checks and attributes
  intervals to a PC; its percentages are approximate and can perturb waits.
  Overlapping loop ranges must not be added. Exact skip counts take precedence
  where available. For example CTR's sampled pointer wait was 9.18%, but its
  unperturbed exact steady skip count was 11.41%.
* QEMU translated-block execution counts, `hotblk.py`, native symbol profiles
  and a host-to-guest instruction map measure **ARM work**. Steady performance
  is the difference of independent 600- and 300-frame runs, divided by 300.
  Modeled cycles = ARM instructions + 70 × instruction misses + 70 × data-read
  misses + 4 × write misses. Interlocks are reported separately. Cache costs
  spread over a QEMU block give approximate guest-PC attribution. Native
  helper time is separate from translated guest ranges.

Probe builds collect draw/RAM/audio/state checksums and counters. Fast builds
omit the draw/audio checksum work and skip/HLE counters. Performance A/B uses
one fast binary with only startup environment switches changing. Both keep
assertions enabled. The simulator uses the supplied `o3hw_off.ini`: GPU path,
frameskip disabled, stereo disabled, PS1 clock auto (dynarec multiplier 175),
single-thread SPU, simple interpolation, reverb/CDDA/XA decoding disabled.
Audio equality therefore covers the emitted PCM under those settings; it does
not validate disabled audio features or the hardware SPU thread.

## Existing waits and BIOS

Exact steady-window baseline (frames 301–600), original skip enabled, new
prototypes disabled:

| Game | Elapsed PS1 cycles/frame | Already omitted cycles/frame | Omitted share | Successful batches/frame | Guest loop |
|---|---:|---:|---:|---:|---|
| CTR | 564,480.03 | 0 | 0% | 0 | New pointer wait is missed |
| GT2 | 564,479.76 | 45,776.31 | 8.11% | 88.02 | `8007d1d0` RAM-counter poll |
| Silent Hill | 564,479.94 | 89,996.00 | 15.94% | 224.99 | `800132ac` PsyQ v_wait |
| R4 | 564,480.07 | 340,304.27 | 60.29% | 556.46 | v_wait + VSync(1) caller |
| Crash (`o3auto`) | 564,480.06 | 141,624.00 | 25.09% | 354.06 | `8003e660` PsyQ v_wait |
| SotN | 564,479.97 | 305,396.00 | 54.10% | 763.49 | `80015478` PsyQ v_wait |

R4 splits into **141,802.67 cycles / 354.51 batches** in `8008b4d0`
and **198,501.60 cycles / 201.95 batches** in the specialized VSync(1)
accelerator. That accelerator recognizes the caller/callee across a function
call and respects the next hblank tick as well as the next event. The PsyQ
v_wait optimization is upstream's compatibility hack, independent of the
`psx3d_idle_skip` variable.

Elapsed cycles minus omitted cycles are approximately CTR 564,480, GT2
518,703, SH 474,484, R4 224,176, Crash 422,856 and SotN 259,084 cycles/frame
still handled through CPU/HLE paths. This subtraction describes accounting;
it is not an executed-instruction count or a host timing measurement.

All runs use **HLE BIOS**, with no external BIOS ROM present. There is no
measured BIOS ROM instruction execution. The following records inclusive guest
cycle deltas charged inside A0/B0/C0 HLE calls, excluding the wrapper's final
branch-test interrupt handling. Calls can invoke callbacks, so this is not an
exclusive CPU-time attribution for all BIOS services or interrupt stubs.

| Game | HLE guest cycles/frame | Share of elapsed guest time | WaitEvent calls, whole 600 frames |
|---|---:|---:|---:|
| CTR | 1,974.23 | 0.350% | 0 |
| GT2 | 394.57 | 0.070% | 0 |
| Silent Hill | 416.00 | 0.074% | 0 |
| R4 | 1,228.08 | 0.218% | 0 |
| Crash | 964.70 | 0.171% | 0 |
| SotN | 1,188.95 | 0.211% | 0 |

Native code attributed by debug symbols to `psxbios.c` (including exception /
event helpers and excluding their callees) has the following exclusive modeled
host cost. This complements the guest-cycle HLE accounting above; it is not
all inclusive BIOS wall time. Neither measure indicates a large unused wait.

| Game | Native BIOS ARM instructions/frame | Native BIOS modeled cycles/frame | Share of modeled total |
|---|---:|---:|---:|
| ctr | 17,921 | 91,217 | 1.91% |
| gt2 | 7,067 | 34,941 | 0.63% |
| sh | 16,653 | 98,424 | 2.29% |
| r4 | 6,588 | 33,988 | 1.22% |
| o3auto | 27,667 | 95,953 | 2.57% |
| sotn | 6,658 | 34,869 | 1.24% |

CTR calls TestEvent 2.25 times/frame in this steady window (~117 charged
cycles/frame); the 600-frame total is 3,538 calls, concentrated during startup.
The others make no TestEvent calls. `psxBios_WaitEvent` already moves
`psxRegs.cycle` to `next_interupt` and retries at that event when active and
unsignaled. There is no missing WaitEvent acceleration to add for these scenes.
TestEvent consumes an event and returns immediately; making it block could
suppress useful work in its caller. PsyQ VSync is game-linked library code,
not necessarily a BIOS HLE call.

## Hot-loop classification

Percentages below are approximate **600-frame guest samples**, while host
costs are **steady-window modeled cycles/frame**. Different windows/methods
explain differences from exact skip counts above. Ranges containing native
calls exclude those helpers' host cost. These are inspected hot loops/ranges,
not a complete partition of execution.

| Game | Guest address/range | Evidence and classification | Sample share | Modeled host cost/frame |
|---|---|---|---:|---:|
| CTR | `80037894..800378c8` | Repeated RAM pointer/flags; no stores, calls or carried computation. Missed wait, accelerated by prototype | 9.18% sampled; 11.41% exact steady skip | ~85,398 before |
| CTR | `8007b140..8007b178` | Stack counter up to 60 plus `x *= 13`; stores on every pass. Counted delay, left unchanged | 1.62% (~9,153 guest cycles/frame) | ~12,122 |
| CTR | `8006a52c..8006a618` | GTE inputs/results, vertex-list advancement, culling and packet helpers; useful work | 9.48% | Part of geometry range below |
| CTR | `80069c00..8006b300` | Geometry/packet code, overlapping local loops | 27.99% | ~381,980 |
| CTR | `800a3b00..800a7d00` | Additional geometry/packet code | 24.87% | ~582,012 |
| CTR | `8001cae4..8001cb18` | Advance through signed halfwords and find maximum absolute value; read-only work, not a wait | 3.02% | Data scan |
| CTR | `8006d810..8006d830` | Loop calls a data-processing helper and advances a pointer | 0.43% | Not a pure getter |
| GT2 | `8007d1d0..8007d1dc` | RAM counter compared with target; 9 guest cycles/iteration; already accelerated | 4.49% | ~701 residual |
| GT2 | `80023568..80023628` | GTE results, sign flags, halfword/result stores, advancing pointers | 3.93% | ~62,084 |
| GT2 | `8002810c..80028250` | GTE/clip math, result records and arithmetic calls | 3.42% | ~30,078 |
| GT2 | `80081248..8008127c` | Counted digit-by-digit square-root arithmetic with accumulated result; not a delay | 1.01% | Real arithmetic |
| SH | `800132ac` | PsyQ v_wait with stack timeout and Vcount; already accelerated | 15.42% | ~3,346 residual |
| SH | `800cc41c..800cc580` | Walk 32-byte records, update byte counters/state, call processing routines | 4.13% | ~34,198 |
| SH | `80057cdc..80057e3c` | GTE result/output stores | 3.11% | ~61,935 |
| SH | `800cc630..800cc668` | Array index++, pointer += 32, processing calls | 2.01% | Real work |
| SH | `800558a0..80055960` | Interpolation/division, accumulating index and storing bytes | 1.21% | Real work |
| R4 | `8008b330..8008b4b0` | VSync(1) counter-reading callee; already specialized | ~39.72% | ~74,282 residual |
| R4 | `8008b4d0` | PsyQ v_wait; already accelerated | 25.19% | ~3,780 residual |
| R4 | `8005f564..8005f6dc` | GTE transforms, packet stores | 4.23% | ~33,376 |
| R4 | `800657b0..80065918` | Geometry/GTE and output stores | 3.26% | ~39,320 |
| R4 | `80098a70..80098aec` | Pointer-based halfword/status polling with timeout exits; probable hardware wait, no broad MMIO skip | 0.39% (~2,195 guest cycles/frame) | Small remaining candidate |
| Crash | `8003e660` | PsyQ v_wait; already accelerated | 25.62% | ~4,842 residual |
| Crash | `80034000..80035a00` | Large geometry range, native GTE calls and output generation | ~59.7% | ~580,059 |
| SotN | `80015478` | PsyQ v_wait; already accelerated | 54.75% | ~8,369 residual |
| SotN | `800e386c..800e38bc` | Scan/relink 512 ordering-table entries; pointer += 4, stores links | 1.92% | Real work |
| SotN | `800f3508..800f35d8` | Per-record coordinate/entity arithmetic with stores | 1.66% | Real work |

The overlapping CTR `8006a57c..8006a6b0` loop samples at 10.84%; do not add
it to `8006a52c..8006a618` or the enclosing geometry range. Static store/call
counts alone do not establish idleness: read-only scans and counted math are
common. No additional high-cost tiny side-effect-free getter call loop was
established in these captures beyond the existing R4 case. Compiled CTR
CD/DMA-related candidate polls (`8007c5dc`, `8007c624`, `8007d068`, etc.) did
not produce skips; dynamic MMIO addresses fail closed. There is no evidence
from these hot profiles that CD/SPU/DMA polling hides a large CTR saving.

`hotblk.py` on CTR's native `gteRTPT_sf1lm0_nf_arm` finds approximately
502 executions/frame and 129k instructions+interlock cycles/frame. The hottest
126-/104-instruction blocks contribute 63.3k/52.3k instructions/frame.
This is actual transformation work. The private evidence includes native
profiles, translated-block maps, sampled RAM disassembly and range totals.

## Prototypes and timing argument

Both new switches default **off**, read before compiling guest blocks:

| Startup keyword | Default | Effect |
|---|---|---|
| `idle=on/off` | on | Existing generic RAM polls and specialized R4 VSync(1); upstream PsyQ compatibility hack remains independent |
| `idleptr=on/off` | off | Extend generic detector to runtime-guarded RAM pointer/flag loops |
| `idledelay=on/off` | off | Pure three-instruction register countdown accelerator, independent of `idle` |

Change switches only with a clean application relaunch. They are deliberately
not live remote settings: changing them without flushing generated blocks
would give mixed behavior. No gldbg bit is used.

The CTR wait is structurally:

```text
head: p = RAM[gp+832]
      if (RAM[p+7392] >= 7) exit
      if (byte RAM[p+7472] != 0) goto head
      if (RAM[gp+840] > 0) goto head
```

The old detector rejects the pointer loaded inside the loop, the fourth load
guard and the alternate earlier back-edge. The extension accepts these only
under the existing no-carried-register/no-side-effect rules. All dynamic load
addresses must be RAM. A base rewritten after its load is rejected. When the
register allocator discarded a pointer, the guard can reconstruct it through
previously checked aligned RAM word loads, rooted in an available register or
a register invariant throughout the compiled block. Unknown roots fail closed.
Up to six guards and an earlier branch back to the same head are accepted;
branches into the middle remain rejected. Stores and calls remain rejected.

At a taken edge the guard first checks that `last_count` has not changed:
an intervening emulation event might have changed RAM after an earlier load.
With no event, RAM values and branch outcomes repeat. The generated path adds
only whole iterations in batches of 64/8/1 while the **original next cycle
check remains strictly before its scheduled event**. It leaves the final
iteration/check to the original generated code. Thus it never skips an event
or changes the original instruction-boundary interrupt timing. It does not
round directly to vblank or invent a new event schedule.

In the steady CTR window it omits **2,220.31 iterations/frame**, in **47.6
batches/frame**, leaving about six real loop iterations/frame. Whole-run
600-frame omission is 38,041,330 guest cycles (11.23%); over 3,600 frames it is
305,494,207 (~15.03%). Waiting changes as the scene evolves.

The countdown prototype matches **only** `addiu r,r,-1; bnez r,head; nop`,
with an internal branch and supported out-of-order delay-slot emission. It
subtracts the same number of iterations from the register as it adds to the
cycle counter, leaves the last nonzero iteration, and stops before the next
original cycle check would fire. Unsigned guards preserve zero/wrap behavior.
It does not match CTR's stack delay because that loop writes RAM and changes
another value. No countdown matches occurred in the six game captures; it is
a tested mechanism with **no demonstrated game speed benefit** here.

There is no new HLE WaitEvent or generic call-loop patch: the former is already
implemented and unused in these scenes; the latter requires proving callee
and caller state, overlays, timing-dependent reads and memory writes safe.

## Performance

| Game | Modeled host cycles/frame off → on | ARM instructions/frame off → on | Host-cycle change |
|---|---:|---:|---:|
| ctr | 4,781,995 → 4,679,515 | 2,681,842 → 2,597,777 | -2.143% |
| gt2 | 5,583,735 → 5,583,839 | 3,210,303 → 3,210,235 | +0.002% |
| sh | 4,297,842 → 4,298,639 | 2,014,829 → 2,014,840 | +0.019% |
| r4 | 2,781,776 → 2,782,032 | 1,781,920 → 1,782,087 | +0.009% |
| o3auto | 3,732,846 → 3,731,281 | 2,428,233 → 2,428,245 | -0.042% |
| sotn | 2,815,744 → 2,814,830 | 1,466,901 → 1,466,828 | -0.032% |

Baseline includes all existing skips. Candidate enables both new switches;
only CTR obtains extra skip cycles. A clean rebuild through the committed setup
script gives the table above; an earlier independently built fast layout gave
CTR 4,747,778 → 4,666,145 cycles/frame (−1.72%) with essentially the same
instruction saving. Retain this variation when judging a small cache-sensitive
win: the final measured saving is ~102k host cycles, while the removed loop
itself costs ~85k and the rest reflects cache effects elsewhere. Small changes in other games are layout /
cache-model effects. These are complete headless-core GPU-path runs with
frameskip off, not isolated FF emulation or real GPU timings. The result
cannot predict an FF FPS value. An ideal 2.14% host saving would turn 70% into
roughly 71.5%, but the hardware saving and FF workload have not been measured.
Even the entire remaining CTR counted delay has only ~12k modeled host cycles
per frame (~0.26% of this run); it is not a route to the missing 30%.

## Correctness and limits

With assertions enabled, baseline/candidate match elapsed cycles, 2 MiB RAM
checksum and accumulated PCM checksum at 300/600 checkpoints, final draw hash,
and the full serialized state. The full state includes CPU registers, cycle
counter, RAM, VRAM, SPU and timers. Six games passed at 600 frames; CTR/GT2/R4
also passed at 3,600 frames. These are deterministic no-input continuations
from the supplied states, not title-wide compatibility tests.

| Game, 600 frames | Draw hash | RAM hash | Audio hash | Full-state hash |
|---|---|---|---|---|
| CTR | `c97e59920c262b7a` | `1caa176013076cf1` | `459820ef244aa4a2` | `952f54d4023eecd8` |
| GT2 | `edbf1bbfbcb02a57` | `794ee219b12c9a21` | `90c5734ecf3200d2` | `98a4711483548f53` |
| SH | `2ec86777a402338c` | `61a47e0168b59ca0` | `8330a590784ce27f` | `94039f5848e5757a` |
| R4 | `64757f5bbb43ac73` | `0e1d347561c704fe` | `fc8c7f309f12aef2` | `644b3018296f756c` |
| Crash | `c15806a28d454f0d` | `d5c9a3f5583f813f` | `c7be424a785a76af` | `e6d2085359147e48` |
| SotN | `b5fa9f6b073bdb22` | `b419273e83501adc` | `976020bfcc502a1b` | `0a97f04c31e2d708` |

Each 600-frame run produces 441,000 stereo sample frames. The 3,600-frame
results and exact counters are in `tools/idle-investigation/results/`.
Synthetic tests compare the same outputs/state for countdown starts
1, 2, 9, 65, 10,000, 0 and -1; periodic timer events; a pointer poll; an MMIO
pointer negative case; and a store-in-loop negative case. Ten pairs pass.
Positive countdowns and the RAM pointer test exercise real omissions; the
MMIO and store tests omit zero cycles. Counts 1/2 appropriately omit none.

The simulator's fake citro3d SDK does not rasterize PICA commands. The draw
hash checks the instrumented draw stream, not a physical screen capture;
GPU-rendered VRAM/readbacks and hardware audio are not independently validated.
State and draw/audio equality are strong regression evidence within this
harness, not proof for every game, interrupt phase or hardware configuration.
The inherited `LO_unused2` last-event marker is not serialized. The inherited
R4 callee template is checked at compilation, with the existing overlay risk.
New pointer reconstruction adds guards/code size; Old 3DS instruction-cache
regressions are possible. Keep both prototypes opt-in until hardware review.

3DSX and CIA builds with devkitARM GCC 16.1 pass. Patch 0002 reverse-checks
against the working core, and clean application of the patch series reproduces
both modified dynarec files. The gitlink remains unchanged.

## Reproduction and private evidence

Committed scripts are in `tools/idle-investigation/`. They require the
user-supplied private `devtools/sim`, ROMs/states, ARM Linux toolchain, QEMU
cache plugin and `llvm-mc`/`llvm-objcopy`. No game/state/RAM dump is committed.
Setup copies the core from this worktree, not a different active checkout.

```bash
python3 tools/idle-investigation/setup.py --mode probe
MODE=probe tools/idle-investigation/build.sh
RUNNER=runner_probe python3 tools/idle-investigation/run.py ctr 600 final_off_ctr
RUNNER=runner_probe IDLEPTR=1 IDLEDELAY=1 python3 tools/idle-investigation/run.py ctr 600 final_on_ctr
python3 tools/idle-investigation/synthetic.py --frames 600

# Sample separately: extra scheduler checks perturb timing, so never use this
# run for exact skip or host-cost figures.
NOIDLE=1 IDLEPROF=257 IDLEPROF_OUT=/tmp/idle-inv-sim/ip_ctr.bin \
  python3 tools/idle-investigation/run.py ctr 600 sample_ctr
python3 tools/idle-investigation/loops.py /tmp/idle-inv-sim/ip_ctr.bin

python3 tools/idle-investigation/setup.py --mode fast
MODE=fast tools/idle-investigation/build.sh
# Repeat for n=300 and n=600; use the same runner for both settings.
CACHE=1 NOMETRICS=1 RUNNER=runner_fast \
  python3 tools/idle-investigation/run.py ctr 300 cache_off_ctr
CACHE=1 NOMETRICS=1 RUNNER=runner_fast IDLEPTR=1 IDLEDELAY=1 \
  python3 tools/idle-investigation/run.py ctr 300 cache_on_ctr
# after the matching 600-frame runs:
python3 tools/idle-investigation/costs.py /tmp/idle-inv-sim cache_off_ctr
```

Unset `IDLEPTR`, `IDLEDELAY`, `NOIDLE` and `IDLEPROF` for baseline runs: these
environment switches use presence, so `IDLEPTR=0` still enables the prototype.
Setup removes intermediate core objects when changing modes. The probe binary
can be retained and run after preparing the fast build. `run.py` propagates
failure to its caller. Full-state equality can be checked with `python3 tools/idle-investigation/compare.py
/tmp/idle-inv-sim ctr 600 final`; it compares the `METRIC`, `draw hash`,
`state hash` lines and checks the completion marker. Check process success too.
`bios-costs.py ELF CACHE_300 CACHE_600` reproduces native BIOS attribution.

Private evidence and CIA/3DSX/ELF are archived in this worktree's ignored
`devtools/idle-inv/`. The archive retains measured binaries, options, sampled
profiles/disassembly, cache/block maps, final equality logs, synthetic sources
and outputs, build logs and SHA256 manifests. Intermediate failed prototype
runs are excluded. `/tmp/idle-inv-sim` is removed after archiving.

## Hardware owner A/B (not executed)

Use the archived CIA, the same Old 3DS and protected slot 2. Install once,
then compare startup keywords in `sdmc:/3ds/PSX3D/switches.txt`, with a clean
application relaunch before every run:

```text
stats renderer=gpu game=CTR_-_Crash_Team_Racing state=2 idle=on idleptr=off idledelay=off
stats renderer=gpu game=CTR_-_Crash_Team_Racing state=2 idle=on idleptr=on idledelay=off
```

Use the library's exact title selector for CTR, Gran Turismo 2 and Silent Hill
(the displayed example may need the local title spelling). Repeat A/B/A,
5 seconds warmup then at least 20–30 seconds measurement per run. Record
**FF game speed percentage**, with the actual FF control engaged, and a
separate **frameskip-off FPS** run (`noskip`, FF released). Do not confuse
frontend FPS, emulated 60 Hz frames, new game pictures and game speed.
Compare GT2/SH as controls; their inspected waits already skip and should not
gain materially. Test `idledelay` separately if desired; none of these captures
matches it, so no benefit is expected.

Keep PS1 clock default/57, SPU thread/audio, renderer/RTT, frameskip policy,
CPU speed and remote/NTR overhead identical. Use the owner's existing
stream-free workflow and console locks. Optional `pmu=0,2` and `acap` can
corroborate CPU work/audio consistency, with the same options in both runs.
Watch picture, sound and cadence during normal play as well as the timed
state continuation. Never overwrite protected slots 0/2. Lowering the PS1
clock is a different experiment: prior clock sweeps reduced actual game
updates and cannot stand in for safe waiting skips. No Old 3DS gain is accepted
until this A/B has been performed.
