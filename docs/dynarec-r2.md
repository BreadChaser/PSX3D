# Old 3DS dynarec, round 2

Living report. Branch `t3/dynarec-r2`, based on `release/v0.4.0-candidate`
(1c7e734, release-v040-final2). Evidence and artifacts live outside Git in
`/home/tony/Development/PS1_3ds/devtools/dynarec-r2/`. No push.

Goal: make PS1 CPU emulation on the Old 3DS substantially cheaper, focused on
CTR and GT2 (Old frameskip-off ~62% / ~57% game speed; emulation alone is over
the 60 Hz budget there).

**Status:** four prototypes behind clean-load switches (one more tried and
removed), all offline gates pass, one pre-existing core bug fixed. Hardware
A/B/A package ready but not completed (see section 6). No hardware speed claim
yet; the console is the only judge.

## 1. What the simulator was missing this time

The tools (`tools/dynarec-r2/`) build a private ARMv6/VFP simulator from this
worktree's `core/` (8 MB translation cache, `-O2`, split renderer, Old options)
and, new this round, with the console's memory map: RAM at 0x13000000 (as
`pl_3ds_mmap` aliases it) and the scratchpad at its console address.

The console's own log (`psxMap: tried to map @1f800000, got 0xfddf000`) shows
the scratchpad at **0x0fddf000**. The dynarec translates a guarded scratchpad
address with `emit_addimm_ptr(psxH)`; 0x0fddf000 is not an ARM immediate, so
every scratchpad access on the console costs **two adds**, against one in the
earlier simulator (0x76000000). CTR makes ~40k scratchpad accesses per vblank.

Other 3DS-only paths checked and ruled out as costs in these scenes:

- Code-cache flushing (`NDRC_CACHE_FLUSH_ALL`, whole I/D clean per
  invalidation): a handful of flushes per 100 frames in all six states
  (`stats-run/`), arena sizes constant between frames 300 and 600.
- C hash lookups: ~150–215 per frame. Interrupt/HLE exception handler: small.

## 2. Where CTR and GT2 spend generated code (v0.4.0, console-cost model)

Emission-site attribution (`sites.py`, `mix.py`, frames 301–600 per vblank):
memory guards (`emit_fastpath_cmp_jump`) are the largest single site — CTR
233k instructions / 95 KB executed footprint, GT2 162k / 90 KB — ahead of
address generation (76k / 63k), SMC checks (51k / 46k), branch code and guest
register traffic (load_regs + wb_valid + wb_invalidate ≈ 87k / 95k).
CTR: ~67k guest memory ops/vblank at ~6.4 ARM instructions each.

`guest-mem.py` decodes the hot guest code: about **half** of all executed
loads/stores (CTR 51%, GT2 49%) use a base register that nothing modified
since an earlier access within 32 bytes in the same straight-line run.
Their range guard repeats work the first access already did.

GT2's generated code touched per frame is ~400 KB (12.4k distinct 32-byte
lines executed at least once per frame; CTR ~226 KB), so on a 16 KB I-cache
every removed instruction also removes fetch traffic.

## 3. Prototypes (switches, clean load only)

All in `psx3d_dynr2` (core) with names `dynr2_spad`, `dynr2_spsmc`,
`dynr2_group`, `dynr2_otc` (`=on|off`, startup switches.txt and remote load/batch; status
settings report each; live `set` is refused). Default off.

1. **dynr2_spad** — the frontend now aliases the scratchpad at 0x13400000
   (MEMOP_MAP, like RAM at 0x13000000), a single ARM immediate. On: one add
   per guarded access. Off: the old two-add cost, for same-binary A/B.
2. **dynr2_spsmc** — a store that passed the scratchpad guard skips its SMC
   check (code is only ever compiled from RAM and BIOS; nothing within 4 MB of
   the window is code). The slow stub still checks, after restoring
   registers.
3. **dynr2_group** — loads/stores through one base register that nothing
   writes in a straight run (no branch target, jump, COP0, syscall/HLE/INTCALL,
   ≤32 instructions, offset span ≤64) share one range guard on the first
   access, which checks the whole offset range (scratchpad: one window check;
   RAM: both ends with `cmp`+`cmpvs`). Members skip their guard; scratchpad
   members translate with one `sub`. If the head guard fails, a cold copy of
   the run, emitted with the normal per-access guards (replaying the main
   loop's emission, speculation and loaded-constant state), takes over and
   jumps back after the run. Groups starting inside a copied run are covered
   by it. RAM groups are formed only when they save instructions.
4. **dynr2_otc** — the OT-clear DMA (`psxDma6`) writes four entries per
   iteration (GT2 spends ~42k instructions/vblank there at ~5 per entry).
   Same entries and stopping point as the original loop.

Bug found by the gates and fixed (`7dd0bca`), pre-existing: a load/store
whose base came from a constant BIOS ROM word is treated as a constant
address while its address register is the guest register; the inline stub
converted that register into the host address in place. With 0x13400000 this
crashed every real-BIOS boot; on the console's 0x0fddf000 the conversion is a
full constant load that hid the stale constant flag, but it still overwrote the
guest register (real-BIOS users only). Now converted into the temporary.

## 4. Simulator results (whole runner incl. renderer/sound; not hardware)

Same simulator binary (`final-sim`, core of `36470cf`), `DYNR2=0` (the
console's cost today) vs `15` (all four on), instructions / modeled cycles per
vblank, frames 301–600:

| State | Instructions | Modeled cycles |
|---|---:|---:|
| CTR | 2,588,381 → 2,463,382 (−4.8%) | 5,769,114 → 5,303,966 (−8.1%) |
| GT2 | 3,048,740 → 2,956,999 (−3.0%) | 6,598,882 → 6,432,032 (−2.5%) |
| R4 | 1,715,811 → 1,635,604 (−4.7%) | 3,318,708 → 3,154,236 (−5.0%) |
| SH | 1,922,926 → 1,845,401 (−4.0%) | 4,952,858 → 4,804,254 (−3.0%) |
| SotN | 1,373,158 → 1,328,772 (−3.2%) | 3,271,611 → 3,188,077 (−2.6%) |
| Crash | 2,307,759 → 2,296,596 (−0.5%) | 4,398,894 → 4,345,626 (−1.2%) |

P1+P2 alone (`3`): CTR −2.8% instructions. CTR memory-access code falls by
~120k instructions/vblank (~12% of generated instructions). The cold copies
grow the translation arena (CTR 2.39→2.81 MB, GT2 3.73→4.50 MB of 8 MB);
they are not executed in steady state. The model underestimates I-cache
misses ~3× and cannot predict layout; hardware decides.

## 5. Correctness gates

Baseline for every gate: the pristine v0.4.0 core at the console's current
scratchpad mapping (0x0fddf000). Candidate: all on (`7`) and all on with every
group guard forced into its cold copy (`0x107`, test-only bit).

- Interpreter differential (`microtest.c`), variants off / on / forced-cold:
  6,000 random ALU/RAM/scratchpad byte/half/word/LWL/LWR programs; 2,000 SMC
  invalidations and guard transitions; 72 group runs compiled with scratchpad
  or RAM speculation then executed with bases crossing the window end, I/O,
  the end of RAM and mirrors; 500 code overwrites by stores compiled for the
  scratchpad. PASS (`microtest/microtest-efad2fd.log`, rerun after `08fabf0`).
- Six states × 600 frames, per-frame draw hashes, 60-frame RAM/VRAM/GPR/CP2/
  PC/cycle checkpoints, final serialized state: see `gate600/compare-*.json`.
- Six states × 3,600 frames: `gate3600/compare-*.json`.
- 63 HLE cold boots (shipping configuration) × 1,200 frames, and 63 real-BIOS
  boots: `boot-hle/`, `boot-bios/`.
- SMC/IRQ/save-state round trips: covered by the checkpoint timelines and the
  SMC microtests; the round-trip/IRQ-trace runner from dynarec-rewrite is
  still to be rerun on this branch.

Results (core of `36470cf`, sim built from this worktree; `0x10f` = all on with
every group forced into its cold copy):

| Gate | Configurations | Result |
|---|---|---|
| 6 states × 600 frames | off (0), 7, 15, 0x107, 0x10f | all PASS |
| 6 states × 3,600 frames | 7, 15, 0x107 | all PASS |
| 63 HLE cold boots × 1,200 frames | 7, 15, 0x107 | 63/63 PASS each |
| 63 real-BIOS boots × 1,200 frames | 0, 7, 15 | 63/63 PASS each |
| Interpreter differential microtests | 0, 7, 0x107 | PASS |

Each PASS means every per-frame draw hash, every 60-frame checkpoint and the
final serialized state equal the pristine core. Two real bugs were caught on
the way: P2's stub clobbering INVCP (MediEvil HLE boot, fixed `efad2fd`) and
the pre-existing constant-ROM-base register overwrite (all real-BIOS boots,
fixed `7dd0bca`). The OT-clear loop (`dynr2_otc`) has its own host test over
130k start/length cases (`otc-test.c`).

### Rejected: respeculation of failing guesses

Slow stubs still fire GT2 ~5.8k / CTR ~2.9k times per vblank, all direct
memory (RAM/scratchpad guessed wrong; `slowpaths.py`). A prototype counted
slow-stub hits per guest pc, recorded the observed type after 8 and
invalidated the block for recompilation. Steady state got worse in every
state (instructions CTR +2.6%, GT2 +4.6%, SH +10.8% vs 15): recompiling
mid-function changes block boundaries and the other guesses. Removed.

## 6. Hardware

Package: `devtools/dynarec-r2/PENDING-HARDWARE.md`, `artifacts-2/` (identity
dynarec-r2-2, counter 4099, CIA SHA256 9874ff29…), driver
`tools/dynarec-r2/hardware.py` (A1 baseline → candidate off/on/partial → A2
baseline, exact restore of the installed baseline), batches in
`devtools/dynarec-r2/batches/`.

Not completed yet. After "Old queue released" the first attempt stopped at
A1 run 2 (wrong GT2 library title, fixed); the console stayed on
release-v040-final2. Since then the Old has been busy with two other workers'
FTP game uploads and is left in ftpd until the user reopens PSX3D. The New was
unreachable. The only hardware number so far: A1 CTR baseline 37.05 fps,
6.043 M cycles/frame (v0.4.0 final2, slot 2, frameskip off).

## 7. Ideas not done yet

- Wider groups (span 256 bytes, runs of 64): CTR −0.3%, GT2 −0.1% more
  instructions in the simulator. Not worth it; limits stay 64/32.
- Group members for LWC2/SWC2 (GTE loads through one base): ~9k/vblank in
  CTR, ~4.7k in GT2, so at most ~0.4%.
- Merge SMC checks of RAM group stores into one range check.
- Faster OT-clear DMA loop (GT2 `psxDma6` ~42k instructions/vblank, 5 per word).
- Cross-block register retention (state traffic ~90k/vblank): previous worker
  found allocation changes alter GTE stall timing; needs that coupling fixed
  first.
