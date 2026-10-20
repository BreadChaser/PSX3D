# Old 3DS dynarec, round 4: exact load delays before a branch

> Research report. Its `tools/dynarec-r4/` scripts were moved out of the repository into the
> local tooling archive (`devtools/repo-tools-archive/dynarec-r4/`) after the round finished.

Branch `t3/dynarec-r4`, from `release/v0.4.1-candidate` RC3 (`aa3eeff`).
Evidence and artifacts live outside Git in
`/home/tony/Development/PS1_3ds/devtools/dynarec-r4/` (abbreviated `E/` below).
No push.

Goal: Tekken 3's polygon loop ran through the interpreter on every pass
(speed-trades: `nolddelay` + `nostalls` gave +18% / +34% on the Old 3DS, but
`nolddelay` is inaccurate). Compile that code exactly instead, and find out
which other games fall back to the interpreter.

## Summary

- **Tekken 3 heavy scene on the Old 3DS: +16.4% game speed with frameskip
  off (54.7% → 63.7%, 33.5 → 38.6 pictures/s) and +29.9% with Auto
  (69.2% → 89.8%)**, final build `dynarec-r4-2` against RC3 A/B/A. Every
  other heavy scene is within ±0.5% (NFS's 5-second run within its noise).
  `nolddelay` + `nostalls` gave +18% / +34%, with an accuracy cost; this is
  exact. The Old cache model drops from 5.75M to 4.88M cycles per vblank
  (−15.1%); the interpreter functions (`execI`, `doBranch`, `irq_test`,
  `psxBranchTest`, `fetchNoCache`) disappear from Tekken's profile.
- Results are the interpreter's: a 4,000-program differential microtest
  against the interpreter passes, a self-check build that traps when the
  saved old value is lost never trapped, and the first 202 frames of the
  Tekken heavy scene draw exactly what RC3 draws. Later frames differ in
  timing only (§4).
- **Everything else is bit-identical to RC3**: 12 of 13 heavy scenes and all 6
  standard states (600 frames) and all 63 HLE cold boots (1,200 frames).
- **Who else falls back to the interpreter** (RC3, all 82 runs): only Tekken 3
  (~643 calls per vblank), Gran Turismo 2 (~38) and Crash 2 (~24). None of the
  63 cold boots does. GT2's and Crash 2's are other shapes and worth well under
  1%; they are left as they are (§2).

## 1. The pattern

Tekken 3, `80037490` (and the same code again at `800376bc`):

```
800374ec  and   t0,t3,t8
800374f8  beq   t0,zero,80037510
800374fc  mfc2  t0,$24          ; delay slot: t0 = MAC0 (NCLIP result), delayed
80037500  bgtz  t0,80037518     ; not taken side: tests the OLD t0 (the flag)
80037504  sra   t1,t3,25        ; sees the new t0
80037508  bgez  zero,80037518
8003750c  sub   t1,zero,t1
80037510  sra   t1,t3,25        ; taken side: first instruction does not read t0
80037514  blez  t0,80037584     ; tests the new t0 (backface cull)
```

On the R3000 a load (also MFC2) is not visible to the next instruction. The
game relies on it: on the not-taken side, `bgtz` tests the flag that `beq`
already tested, not the NCLIP result. The dynarec applies loads immediately,
so its decoder detects this and makes `beq` an interpreter call. In RC3 every
pass through `beq`, taken or not, ran `beq`, `mfc2`, `bgtz` and `bgtz`'s delay
slot in the interpreter, then looked up the next block by hash (RC3: 385,756
calls in 600 frames, about 643 per vblank).

## 2. Which games fall back to the interpreter

A simulator build of the RC3 core that counts every interpreter call by guest
pc and reason (`E/cnt/`, built from `E/dbg/`), over the 13 heavy scenes and 6
standard states (600 frames) and the 63 HLE cold boots (1,200 frames):

| Game | pc | calls / 600 frames | per vblank | Reason |
|---|---|---:|---:|---|
| Tekken 3 (heavy) | `800374f8` | 271,946 | 453 | load in a delay slot, old value read on the not-taken side |
| Tekken 3 (heavy) | `80037724` | 113,810 | 190 | same |
| Gran Turismo 2 (heavy) | `80025eb4` | 22,771 | 38 | `lw a1` then `lwr a1` |
| Gran Turismo 2 (standard) | `80025eb4` | 219 | 0.4 | same |
| Crash 2 (heavy) | `8003f94c` | 14,425 | 24 | `b 8003f980` with `lw ra` in the delay slot; the target is `jr ra`, which reads the old `ra` |
| all 63 cold boots | | 0 | 0 | |

- **GT2** is a false alarm: LWL/LWR merge with the value of a load still in
  flight (the interpreter forwards it), which is exactly what the compiled
  `lw` then `lwr` computes. Relaxing the decoder's check for LOAD followed by
  LWL/LWR into the same register (with a different base) would be exact, but
  it is ~38 calls per vblank (well under 1%), so it is left alone here.
- **Crash 2**'s target is a shared `jr ra; nop` stub, also entered from
  elsewhere, so the old `ra` cannot be renamed there. ~24 calls per vblank.
- **Decoder quirk (upstream, unchanged):** the decoder recognises
  MFC0/MFC2 as delay loads only into `r1`–`r15`, and never CFC2 (mask
  `0xf3d00000` in `pass1a`). Those other forms are compiled with the value
  visible immediately, unlike the interpreter. `E/mqrun/` counts such sites
  followed by a reader: none in any of the 82 runs (§4.4).

## 3. What was built (`ea1bc94`)

Instead of the interpreter, the MFC2 keeps a copy of the old value:

- **Decoder** (`ld_delay_rename`): for `mfc2 r` (or `cfc2`) followed by a
  conditional branch (BEQ/BNE/BLEZ/BGTZ) that reads `r`, the MFC2 also gets
  `r` as a source and LDTMP as a second destination, and the branch reads
  LDTMP instead of `r`. If the MFC2 is in a delay slot, only for a BEQ/BNE/BLEZ/
  BGTZ that reads `r` itself (Tekken's shape), so that branch is assembled in
  order and the slot runs after its test on both paths. Everything after the
  renamed branch, including its own delay slot, reads `r` = the new value, as
  in the interpreter.
- **LDTMP** is register 34 (the unused FSREG slot) with its own home word in
  `dynarec_local` (`LO_ldtmp`, outside `psxRegs`, so not in save states). The
  allocator treats it like a guest register (written back and reloaded at its
  home), and liveness makes it needed only between the MFC2 and its branch.
  `cop2_assemble` copies the old value first (`mov`, or the constant, or a
  load from `psxRegs`), then does the MFC2 as before.
- **No way in between the two:** a block resuming at the renamed branch would
  read the new value. So the first branch has no cycle check on its not-taken
  side (its cycles are added and checked at the renamed branch, a few
  instructions later), and pass 7 does not make the renamed branch an
  interrupt-return entry point. If the renamed branch is a branch target
  inside the block, or the block ends before it, the block is decoded again
  without that rename (the RC3 path: interpreter call), restoring the
  decoder's global state first so the second decode is exactly RC3's.
- Code: `.text` +1,616 bytes (+0.09%) in the first build.
- **Layout pin (`423e429`)**, after hardware session 1 (§5): LDTMP's home is
  the `zeromem_ptr` word of `dynarec_local` (set at init, never read) and the
  rename deny list lives in `compile_state` on the stack, so no data address
  moves. `new_dynarec_init` (runs once) jumps over a 2,460-byte pad
  (`NDRC_LAYOUT_PAD`) so that everything after the dynarec object, code and
  data, sits exactly 4 KiB later than in RC3: the Old 3DS's 16 KiB 4-way
  caches map it to the same sets. Checked with `E/layoutdiff.py`: every
  symbol after `new_dynarec_init` moves by exactly +4,096 (a few `.rodata`
  constants by 4,088–4,104). **The pad has to be recomputed whenever
  `new_dynarec.c` changes** (4096 minus the object's growth); otherwise it is
  just harmless padding.

Artifacts: `E/artifacts/` = `E/artifacts-2/` (final, identity
`dynarec-r4-2`, CIA SHA256 `e9c16752195a854a37bcdfaebb03c2aee35ce4ac6a0507249f0efd0291ff5692`,
source `5e055a9`, title counter 4100); `E/artifacts-1/` is session 1's build.

## 4. Correctness

### 4.1 Identical to RC3 where RC3 compiled

Simulator `E/h3` (first build) and `E/h4` (final source with the layout pin;
same results, Tekken's timeline identical to `E/h3`'s), production `-O2`, `PICA_SIM_HASH`), the same
runner and options as the RC3 gate; compared with the RC3 gate timelines
(`devtools/v041/gates-rc3`): per-frame draw hashes, 60-frame RAM/VRAM/GPR/
CP2/PC/cycle checkpoints, IRQ/exception trace digests, serialized state
(`E/gates-h3/`, `E/gates-h4/`, `tools/dynarec-r4/compare.py`):

| Set | Result |
|---|---|
| 13 heavy scenes × 600 frames | **12/13 identical**; Tekken 3 differs (§4.2) |
| 6 standard states × 600 frames | **6/6 identical** |
| 63 HLE cold boots × 1,200 frames | **63/63 identical** |

### 4.2 Tekken 3: the interpreter's results, different timing

- The Tekken heavy scene draws **exactly the same pictures for frames
  0–201** (draw hash every frame). At the frame-60 checkpoint VRAM and the
  GTE registers are identical, the guest cycle counter is 15 cycles apart,
  and RAM, CPU registers and the IRQ trace (1,955 vs 1,984 events) differ.
  From frame 202 on the draw hashes differ.
- Why the timing moves (as in dynarec-r3 §6): the interpreter counts 1.75
  cycles per instruction with a fractional carry kept in `psxRegs.subCycle`,
  runs its own event check after each of the two branches, and the RC3 block
  ended at the interpreter call, so the code after it was cut into different
  blocks. The compiled version counts `CLOCK_ADJUST(n)` per straight run in
  one block and checks cycles once, at the renamed branch. Events land a few
  cycles earlier or later, and the game's timing-dependent state follows.
  The interpreter's timing cannot be reproduced by compiled code.
- What the compiled code computes is the interpreter's: §4.3 and the
  self-check build (`tools/dynarec-r4/ldverify.py`, `E/v2`): the MFC2 also
  writes the old value to a shadow word and the renamed branch executes an
  undefined instruction if LDTMP differs from it. Over all 13 heavy scenes
  and 6 standard states (600 frames, assertions on): 2 sites renamed (Tekken
  `80037500`, `8003772c`), the renamed branches ran 3,588 times, no trap; no
  other game renamed anything (`E/selfcheck/`).
- One timing corner: RC3's interpreter call checked for events after the
  first branch too, so an interrupt could be taken between the two branches
  (with the MFC2 still pending). The compiled code has no check there, so
  such an interrupt comes a few instructions later, after the renamed branch
  and its delay slot. Both are valid instruction boundaries; only the moment
  differs, like any other cycle-check placement in the dynarec.

### 4.3 Microtests

`tools/dynarec-r4/microtest.c`, run as
`microtest "<Crash cue>" 1 - o3hw_off.ini` (`E/mt-v2.log`; final source
`E/mt-v3.log`, self-check `E/selfcheck3/`, same results):

- **New, load delay:** 4,000 generated programs: random ALU prelude (register
  pressure), then either `beq/bne/blez/bgtz r` with `mfc2 r,$g` in its delay
  slot (2,000) or a plain `mfc2 r,$g` (2,000), then `beq/bne/blez/bgtz` reading
  `r` (sometimes `r` on both sides, which the decoder turns into an
  unconditional branch), a delay slot that mostly keeps the new `r` live,
  markers on every path, a taken target that sometimes reads `r` (decoder
  falls back to the interpreter there) and a final read of `r`. Random GTE
  data registers, including zero and negative values. Both the interpreter
  and the dynarec run block by block, and the dynarec stops at **every**
  cycle check and resumes in a new block, as after an interrupt or at the end
  of a frame. GPR, HI, LO and PC identical to the interpreter in all 4,000;
  2,469 of them compiled through the rename.
- **The test catches the bug it is meant to catch:** with the not-taken cycle
  check put back (and nothing else changed), case 1 already fails (`r29
  expected 00000111 got 00000222`): the dynarec left at the first branch and
  the resumed block read the new value (`E/mt-v2m.log`).
- The round 2/3 suites still pass (6,000 random programs, SMC/guard
  transitions, group guards, scratchpad stubs).
- Only MFC2 into `r1`–`r15` is generated: other forms are not delay loads to
  the decoder (§2).

### 4.4 Decoder quirk survey

`E/mqrun/`: a simulator build of this branch that logs, at decode time, every
MFC0/MFC2/CFC2 the decoder does not treat as a delay load while the next
instruction reads its target (BLTZ/BGEZ readers excluded: the decoder ignores
those hazards on purpose). **None** in the 13 heavy scenes, 6 standard states
or 63 HLE cold boots. The quirk is real but no tested game hits it.

### 4.5 Other checks

- Simulator builds with assertions on: no assertion from the dynarec. Crash
  2's heavy scene stops at frame 574 on `psxBiosException:
  !psxRegs.cpuInRecursion` (HLE BIOS); the unmodified RC3 core stops at the
  same place with assertions on (`E/selfcheck-rc3/`). Production builds have
  assertions off.

## 5. Old 3DS hardware

Two sessions under `fastloop/lock.old`, each RC3 A1 → candidate → RC3 A2
(self-update in and out), 13 heavy scenes (slots 5–7), 3 s warm-up + 12 s
(NFS 1 + 5 s), PMU 0,2, frameskip off and Auto. Driver `E/hw.py` (copy of the
speed-trades driver), batches `E/batches/`, logs and TSVs `E/hw/old-{1,2}/`,
tables `E/hw/old-{1,2}/summary.md` (`E/summarize.py`). Game speed %
(pictures/s); Δ = candidate against the mean of A1 and A2; drift = A2 vs A1.

### Session 1: `dynarec-r4-1` (no layout pin, CIA `b8b7112b…`)

## Frameskip off

| Scene | RC3 A1 | candidate | RC3 A2 | Δ speed | drift | Mcycles A→B |
|---|---:|---:|---:|---:|---:|---:|
| Crash 1 | 86.03 (20.3) | **85.86 (20.2)** | 86.12 (20.2) | **-0.3%** | +0.1% | 4.58→4.58 |
| CTR | 61.00 (18.3) | **61.10 (18.4)** | 61.32 (18.3) | **-0.1%** | +0.5% | 6.10→6.10 |
| GT2 | 46.95 (12.7) | **47.16 (12.7)** | 47.30 (12.6) | **+0.1%** | +0.8% | 9.11→9.12 |
| R4 | 73.40 (22.3) | **72.86 (22.2)** | 73.47 (22.3) | **-0.8%** | +0.1% | 5.78→5.83 |
| SotN | 98.36 (59.5) | **98.28 (59.4)** | 98.48 (59.5) | **-0.1%** | +0.1% | 3.57→3.60 |
| Tekken 3 | 54.64 (33.2) | **65.47 (39.7)** | 54.72 (33.3) | **+19.7%** | +0.1% | 7.82→6.47 |
| FFVII | 99.98 (15.1) | **99.95 (15.2)** | 99.95 (15.2) | **-0.0%** | -0.0% | 3.44→3.45 |
| Klonoa | 85.03 (51.7) | **83.86 (50.8)** | 85.00 (51.5) | **-1.4%** | -0.0% | 4.88→4.94 |
| Crash 2 | 49.38 (15.2) | **48.61 (15.2)** | 49.49 (15.2) | **-1.7%** | +0.2% | 7.73→7.84 |
| NFS | 34.44 (11.2) | **34.40 (11.2)** | 34.40 (11.2) | **-0.1%** | -0.1% | 14.42→14.40 |
| SH garage | 72.95 (17.7) | **73.08 (17.8)** | 72.83 (17.8) | **+0.3%** | -0.2% | 5.90→5.88 |
| SH alley | 61.47 (18.9) | **61.17 (18.8)** | 61.45 (18.9) | **-0.5%** | -0.0% | 6.97→6.99 |
| SH cafe | 61.78 (14.8) | **62.11 (14.9)** | 61.91 (14.8) | **+0.4%** | +0.2% | 6.91→6.87 |

## Frameskip auto

| Scene | RC3 A1 | candidate | RC3 A2 | Δ speed | drift | Mcycles A→B |
|---|---:|---:|---:|---:|---:|---:|
| Crash 1 | 96.82 (14.3) | **97.00 (14.4)** | 96.37 (14.4) | **+0.4%** | -0.5% | 3.96→3.96 |
| CTR | 69.71 (15.2) | **69.35 (15.3)** | 69.15 (15.3) | **-0.1%** | -0.8% | 5.52→5.53 |
| GT2 | 47.81 (11.3) | **47.83 (11.4)** | 47.60 (11.4) | **+0.3%** | -0.4% | 9.10→9.08 |
| R4 | 84.51 (15.7) | **83.37 (15.6)** | 84.17 (15.6) | **-1.1%** | -0.4% | 5.04→5.10 |
| SotN | 99.99 (54.1) | **100.04 (53.1)** | 100.02 (53.8) | **+0.0%** | +0.0% | 3.60→3.61 |
| Tekken 3 | 69.32 (15.3) | **90.75 (15.1)** | 69.28 (15.2) | **+30.9%** | -0.0% | 6.18→4.69 |
| FFVII | 100.06 (15.2) | **99.98 (15.2)** | 99.95 (15.2) | **-0.0%** | -0.1% | 3.42→3.42 |
| Klonoa | 99.71 (15.2) | **99.46 (15.1)** | 99.30 (15.2) | **-0.1%** | -0.4% | 4.20→4.22 |
| Crash 2 | 50.01 (14.2) | **48.98 (14.2)** | 49.79 (14.3) | **-1.9%** | -0.4% | 7.65→7.80 |
| NFS | 34.66 (10.6) | **34.35 (10.4)** | 34.19 (10.4) | **-0.2%** | -1.4% | 14.45→14.46 |
| SH garage | 76.35 (15.1) | **76.07 (15.3)** | 76.06 (15.2) | **-0.2%** | -0.4% | 5.63→5.63 |
| SH alley | 69.90 (15.2) | **69.30 (15.2)** | 70.08 (15.1) | **-1.0%** | +0.3% | 6.06→6.11 |
| SH cafe | 61.28 (14.5) | **61.67 (14.7)** | 61.35 (14.6) | **+0.6%** | +0.1% | 6.97→6.93 |


Tekken +19.7% / +30.9%, but Crash 2 −1.7% / −1.9%, Klonoa −1.4% (off), R4
−1.1% and SH alley −1.0% (Auto). Those scenes run the same generated code as
RC3 (bit-identical in the simulator), and their RC3 brackets drift ≤0.8%:
code layout. Everything after `new_dynarec.o` had moved +1,616 bytes and the
dynarec/SPU `.bss` +40/+32 bytes.

### Session 2: `dynarec-r4-2` (layout pin, CIA `e9c16752…`), the final build

## Frameskip off

| Scene | RC3 A1 | candidate | RC3 A2 | Δ speed | drift | Mcycles A→B |
|---|---:|---:|---:|---:|---:|---:|
| Crash 1 | 86.16 (20.4) | **85.76 (20.3)** | 86.15 (20.3) | **-0.5%** | -0.0% | 4.58→4.60 |
| CTR | 61.21 (18.4) | **61.06 (18.3)** | 61.16 (18.3) | **-0.2%** | -0.1% | 6.09→6.09 |
| GT2 | 46.97 (12.7) | **47.15 (12.6)** | 46.92 (12.7) | **+0.4%** | -0.1% | 9.13→9.12 |
| R4 | 73.12 (22.3) | **73.32 (22.3)** | 73.48 (22.3) | **+0.0%** | +0.5% | 5.79→5.78 |
| SotN | 98.53 (59.6) | **98.47 (59.6)** | 98.15 (59.3) | **+0.1%** | -0.4% | 3.56→3.57 |
| Tekken 3 | 54.90 (33.6) | **63.72 (38.6)** | 54.58 (33.3) | **+16.4%** | -0.6% | 7.80→6.66 |
| FFVII | 99.95 (15.2) | **99.99 (15.2)** | 99.99 (15.2) | **+0.0%** | +0.0% | 3.43→3.44 |
| Klonoa | 84.86 (51.3) | **85.06 (51.6)** | 84.80 (51.4) | **+0.3%** | -0.1% | 4.88→4.87 |
| Crash 2 | 49.52 (15.2) | **49.43 (15.2)** | 49.48 (15.2) | **-0.1%** | -0.1% | 7.72→7.73 |
| NFS | 34.48 (11.2) | **33.95 (10.8)** | 34.35 (11.2) | **-1.4%** | -0.4% | 14.41→14.02 |
| SH garage | 72.80 (17.6) | **72.92 (17.6)** | 72.99 (17.7) | **+0.0%** | +0.3% | 5.89→5.90 |
| SH alley | 61.60 (19.0) | **61.44 (18.9)** | 61.43 (18.9) | **-0.1%** | -0.3% | 6.97→6.96 |
| SH cafe | 61.87 (14.7) | **61.82 (14.6)** | 61.97 (14.7) | **-0.2%** | +0.2% | 6.90→6.90 |

## Frameskip auto

| Scene | RC3 A1 | candidate | RC3 A2 | Δ speed | drift | Mcycles A→B |
|---|---:|---:|---:|---:|---:|---:|
| Crash 1 | 96.83 (14.4) | **97.01 (14.5)** | 96.84 (14.5) | **+0.2%** | +0.0% | 3.97→3.97 |
| CTR | 70.06 (15.2) | **69.74 (15.1)** | 69.80 (15.1) | **-0.3%** | -0.4% | 5.50→5.51 |
| GT2 | 47.66 (11.4) | **47.66 (11.4)** | 47.79 (11.3) | **-0.1%** | +0.3% | 9.09→9.11 |
| R4 | 83.79 (15.7) | **84.23 (15.7)** | 84.08 (15.7) | **+0.3%** | +0.4% | 5.07→5.04 |
| SotN | 100.06 (53.8) | **99.99 (54.1)** | 99.96 (53.9) | **-0.0%** | -0.1% | 3.60→3.60 |
| Tekken 3 | 69.09 (15.3) | **89.83 (15.2)** | 69.26 (15.2) | **+29.9%** | +0.2% | 6.20→4.74 |
| FFVII | 99.88 (15.2) | **99.99 (15.2)** | 99.97 (15.2) | **+0.1%** | +0.1% | 3.42→3.42 |
| Klonoa | 99.58 (15.5) | **99.31 (15.1)** | 99.32 (15.3) | **-0.1%** | -0.3% | 4.20→4.23 |
| Crash 2 | 49.72 (14.3) | **49.64 (14.3)** | 50.09 (14.2) | **-0.5%** | +0.7% | 7.67→7.68 |
| NFS | 33.25 (10.0) | **34.69 (10.6)** | 34.49 (10.6) | **+2.4%** | +3.7% | 14.28→14.37 |
| SH garage | 76.06 (15.2) | **76.16 (15.2)** | 75.94 (15.3) | **+0.2%** | -0.2% | 5.63→5.62 |
| SH alley | 70.14 (15.3) | **70.26 (15.2)** | 69.91 (15.1) | **+0.3%** | -0.3% | 6.06→6.05 |
| SH cafe | 61.32 (14.6) | **61.42 (14.6)** | 61.35 (14.5) | **+0.1%** | +0.1% | 6.97→6.97 |


All the session 1 losses are gone (Klonoa +0.3%, Crash 2 −0.1% / −0.5%, R4
+0.0% / +0.3%, SH alley −0.1% / +0.3%). NFS −1.4% off / +2.4% Auto is its
5-second run: its own RC3 brackets drift up to 3.7%, and its Mcycles per
vblank went down (14.41 → 14.02). Tekken keeps +16.4% / +29.9% (session 1's
+19.7% / +30.9% is the other layout: Tekken itself also moves with layout).
With Auto, Tekken stays at the 15 pictures/s floor and now runs at 90% game
speed instead of 69%.

After each session the Old was on RC3 (`release-v041-rc3`), idle in the
library, checked by status and screenshot (`E/hw/old-{1,2}/final.png`). No
protected slot was written, no HOME/exit.

## 6. Next steps

- New 3DS check (`E/PENDING-NEW.md`); the New was unavailable this round.
- Integration: take `ea1bc94` + `423e429` (skip the "Test build identity"
  commits) and recompute `NDRC_LAYOUT_PAD` for the integrated build with
  `E/layoutdiff.py` (or drop it and re-run the layout A/B/A).
- GT2: let LOAD followed by LWL/LWR into the same register (different base)
  compile; exact, ~38 interpreter calls per vblank.
- Decoder quirk: recognise MFC2/MFC0 into `r16`–`r31` and CFC2 as delay loads
  (an accuracy fix; no tested game hits it, §4.4).

## Reproduce

```sh
source devtools/env.sh
T=tools/dynarec-r4; E=devtools/dynarec-r4
bash $T/mk.sh h3 hash                            # private simulator from this worktree
bash $E/gate.sh $E/h3 $E/gates-h3                # heavy/std/boot vs the RC3 gate timelines
python3 $T/prepare.py $E/v2 --hash && python3 $T/ldverify.py $E/v2 && ASSERTS=1 bash $T/build.sh $E/v2
(cd $E/v2 && qemu-arm ./microtest "<Crash Bandicoot cue>" 1 - o3hw_off.ini)
bash $T/mk.sh p-rc3 perf --ref aa3eeff; bash $T/mk.sh p-r4 perf
python3 $T/run.py $E/p-r4 $E/prof/r4 --heavy --mode profile --game tekken
python3 $E/hw.py old $E/hw/old-1 check expect:release-v041-rc3 bench:... install:r4a ... final
```
