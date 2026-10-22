# Dynarec memory-access code (dynarec-mem), 2026-10-22

Branch `t3/dynarec-mem` from `0e1ca70d` (v0.5.0 source). Code commit `19bc889b`;
`9a92fa11` only sets the test build identity (skip it when integrating).
Evidence: `devtools/dynarec-mem/` (ignored), full notes in `REPORT.md` there.

## Summary

The dynarec guesses, at compile time, whether a PS1 load/store goes to RAM or to
the scratchpad and emits a short check for that guess. A wrong guess sends the
access to a slow stub: a separate piece of code per access, far from the hot
block. Runs of accesses through one base register share one check (round 2
"groups"); when that shared check fails, a cold copy of the run executes, and
until now every access in it repeated the same wrong guess and went through its
own slow stub.

**Change (always on):** in a group's cold copy, accesses through the head's
base guess the other kind (RAM <-> scratchpad). The copy only runs when the
first guess failed, so the members now pass their own checks. Hot code is
unchanged; only cold copies differ. Guest results are bit-identical.

Old 3DS A/B/A (rc4 -> this build -> rc4, same session, 13 heavy scenes; A2 vs A1
drift at most 0.4% except SotN off 0.7%):

| Scene | Off | Auto |
|---|---:|---:|
| Silent Hill garage | **+4.8%** | **+5.3%** |
| NFS High Stakes | +3.1% | +3.4% |
| Tekken 3 | +2.0% | +3.5% |
| Silent Hill alley | +1.9% | +2.5% |
| Silent Hill café | +1.6% | +1.3% |
| Klonoa | +1.9% | 0.0% (at 99.6%) |
| Gran Turismo 2 | +1.4% | +1.8% |
| R4 | +0.8% | 0.0% |
| SotN, FFVII, CTR, Crash 1 | -0.2 … +0.4% | -0.1 … +0.1% |
| **Crash 2** | **-1.5%** | **-1.9%** |

Crash 2 loses beyond the ~1% gate in both modes (A2 within 0.4/0.4% of A1). In
the simulator its dynarec executes exactly the same instructions with and
without the change (no failed guess in its steady state), so the loss is code
placement: bigger cold copies shift where later blocks land in the translation
cache. Owner/coordinator decision: accept (net +2–5% in the heavy CPU-bound
scenes) or hold.

## Finding: main TLB misses matter

First ARM11 TLB measurement (`pmu=3,10`: I-micro-TLB, main TLB misses, per
emulated frame). Main TLB misses: SH garage 5.9k, GT2 3.8k, NFS 4.1k, SH
café/alley 3.1–3.3k, others 0.6–2.1k. The change cut them sharply (SH garage
5.9k -> 3.0k, GT2 3.8k -> 2.3k, NFS 4.1k -> 2.7k), which tracks the speed gains
better than the simulator's instruction counts do (-1 to -2%). Executed
generated code is spread over 340–900 4-KiB pages per frame (simulator), about
four times what it would need if packed, because each block's slow stubs and
cold copies sit between the hot blocks. **Next lever:** emit stubs and cold
copies into a separate region of the translation cache so hot blocks share
pages (needs a second output pointer with its own expiry; medium-large).

## Correctness

- Simulator, candidate core vs rc4 core: 13 heavy + 6 standard states x600
  frames and 68 HLE cold boots x1200 frames bit-identical (per-frame draw
  hashes, 60-frame RAM/VRAM/register/cycle checkpoints, IRQ traces, saved state).
- Same with every group check forced into its cold copy (`DYNR2_FORCE_COLD`):
  13 + 6 identical.
- Round 2–4 microtests (4,000 load-delay programs, 6,000 random memory programs,
  group runs across window/IO/RAM-end/mirror bases, SMC through scratchpad
  stubs): pass.
- Layout: `NDRC_LAYOUT_PAD` 1908 -> 1660; everything after `new_dynarec_init`
  (code, data, rodata, bss) is at rc4's addresses; only compile-time dynarec
  functions move. The two new compile-state fields reuse `compile_state.unused`.
  The flip uses a 0/1 field so GCC keeps rc4's switch table (rodata unchanged).

## Rejected / not pursued

- Followers (C2LS/LWL/LWR through the same base extend the cold copy) and
  follower-only groups: no change in executed dynarec instructions in the
  simulator, only model I-miss noise.
- Flipping nested groups' bases too: worse in the model (SH garage, café).
- Scratchpad base in a pinned host register (-1 instruction per scratchpad
  check): the guess is only known after register allocation, so it would cost
  a register in every memory-heavy block.
- Skipping RAM checks for `sp`: Silent Hill keeps its stack in the scratchpad.
- Second-chance scratchpad check inside slow stubs: saves only two dependent
  table loads per entry (<1%).
