# Audio smoothing and Old 3DS memory

Implemented independently from this repository's code. No external source was
copied or adapted. Earlier local commits were replaced before handoff, removing
the old implementation and credits from both branches' reachable histories.

## Branches and behavior

- `t3/port-o3ds-audio-stretch`, based on `30c62a9`: audio, memory budget, `fs3`.
- `t3/audio-stretch-main`, based on `origin/main` at `ea8d8c9`: audio only;
  no core patch, cache, memory, `acap`, SPU-worker or frameskip-switch changes.

**Smooth audio when slow** is on by default on both models, available in library
and in-game Settings. `config.ini` stores `audio_stretch=50`; 0 disables it,
1..50 sets the maximum slowdown percentage. The UI's on selection means 50.
Existing files without the key get the enabled default.

The controller measures samples supplied per wall-clock second in 250 ms
windows, filters that estimate and only starts slowdown when the smoothed queue
is below half its target. Pitch moves by at most five percentage points per
second. Full-speed playback retains our existing Hermite interpolation and
±0.5% clock-drift correction. Slowdown uses an independent Q15 linear
interpolator with the same sample history and fractional position. Sound slows
and lowers in pitch; this is not pitch-preserving time stretching.

The DSP rate stays fixed. Both NDSP and CSND receive frontend-resampled output.
Batches are split into at most 1024 input stereo frames, with room for 4096
output frames, safe at the minimum 0.495 input/output ratio. This also fixes
truncation of large stretched batches. Stack scratch is about 4 KiB, replacing
the old 32 KiB static float scratch.

SPU mixing/work stealing is untouched. `acap` on the GPU branch hashes original
core samples before resampling or debug-tone substitution, including when the
playback backend is unavailable. Recording retains distinct core/played data.
Menu/suspend resets the production estimate. Emulation below roughly half speed
and long I/O stalls can still underrun. Initial gaps are possible while pitch
glides; allow 10–15 seconds before judging steady playback.

## CPU cost and automated checks

`tools/audio_bench.c` measures the actual helper plus controller call with 735
stereo input frames per emulated NTSC frame. ARM Linux GCC 14.2.1, `-O2
-march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfp -marm`, static linking:

- Ratio 1.000: **113,696 ARM instructions/emulated frame**.
- Ratio 0.700: **26,790 instructions/frame** (30% slowdown).
- Ratio 0.495: **37,663 instructions/frame** (maximum slowdown plus drift).

QEMU cache-plugin counts are `sum(ninsns * execs)`; subtract the 300-frame run
from the 600-frame run and divide by 300 to remove startup. The controller adds
roughly 70–85 instructions/callback. Slowdown is cheaper than the normal Hermite
path despite producing more samples. These counts exclude DSP submission/cache
flushes and are not hardware cycle estimates.

A 200,000-frame host run on Ryzen 5 5500U measured 10.14, 2.26 and 3.24 us/frame
respectively. Old 3DS hardware still must judge cache/layout and audible quality.

Reproduce from either branch:

```sh
cc -O2 -g -fsanitize=address,undefined -o /tmp/audio-test \
  tests/audio_resample_test.c psx3d/source/audio_resample.c -lm
/tmp/audio-test
cc -O2 -o /tmp/audio-bench tools/audio_bench.c psx3d/source/audio_resample.c -lm
/tmp/audio-bench 1 200000
/tmp/audio-bench .7 200000
/tmp/audio-bench .495 200000
```

Both branches pass the 3DS `make -j16` build and sanitizer tests. Tests cover
full-speed suppression with a low queue, sustained slowdown, bursty callbacks,
disabling/recovery, signed extremes, output bounds and variable chunk sizes.
An additional host stub run of actual `audio_write` exercised 8192-frame batches
through NDSP/CSND at maximum slowdown under ASan/UBSan and checked unchanged core
hashes with debug tones on/off. This passed. Existing format-truncation and
executable-stack build warnings remain.

Final 600-frame simulator runs with frameskip off, the same states/options and
small mode, including an 8 MiB dynarec cache, match the baseline:

```text
state    draw hash           core audio hash (441000 stereo frames)
o3auto   6ba7ef2f6e06cf04    5742a60d
ctr      497b85e24ef6ff72    80da5468
gt2      7b3a81441ad9b8aa    ac23d3ef
```

Patch 0002 is regenerated and both core patches apply to a clean upstream tree.
The simulator verifies core determinism, not NDSP listening/syscore scheduling.
Threaded versus unthreaded SPU output already differs upstream: compare hashes
only within the same SPU mode.

## Memory finding (GPU branch only)

Our build has the same structural pressure: `TARGET_SIZE_2=24` reserves 16 MiB
in `.bss` before heap sizing. Existing local Old 3DS recordings, for example
`devtools/o3_spu_Ridge_Racer_Type_4_on.log`, show an 8 MiB LUT mapping failing and
falling back into heap; only 107 KiB linear memory remained before core startup,
and `OOM for vout_buf` was logged. No console was accessed for this work; final
save/load success is still a hardware verification item.

The CTR build now sets `-DTARGET_SIZE_2=23` through a configurable core-header
constant. Other platforms retain 16 MiB. Both 3DS models use 8 MiB because this
is static memory; New 3DS also needs a long-session cache-regression check.

The old 8 MiB reserve cannot fit RAM `0x210000`, scratch `0x10000`, BIOS
`0x80000` and LUTs `0x800000`: **10.625 MiB** total. We reserve **12 MiB** so the
8 MiB LUT stays outside the state heap. Linear memory gets a **19 MiB** floor:
~13.5 MiB already used by GPU/frontend, ~2.25 MiB core VRAM/vout (640×512×4 plus
1 MiB and 4 KiB), 1.375 MiB for eleven 256×256 RGB565 browser textures, plus
room for audio/alignment. New 3DS remains capped at 24 MiB linear.

The **4.25 MiB** serialized state (`0x440000`) is freed before allocating the
**96 KiB** thumbnail pixels. Allocation failures and missing thumbnails are
logged explicitly. Browsing every slot no longer competes with the serialized
state buffer during thumbnail generation.

ELF text+data+bss falls from **23,405,672 to 14,986,120 bytes**, ~8.03 MiB less;
the cache itself frees exactly 8 MiB. Illustratively, with 64 MiB application
RAM and a 1 MiB stack, ~48.7 MiB remains after static data: 19 MiB linear,
12 MiB mapping reserve and ~17.7 MiB normal heap. Previously ~19 MiB heap could
lose 8 MiB to LUT fallback. Exact loader commitments vary; this is a budget,
not a measured free-heap promise. Check startup logs and real save/load success.

Continue screenshots are separate on this branch: `video_snapshot` reads the
software `fbs`, whereas GPU gameplay displays `pica_picture`'s texture. More
memory cannot populate the software buffer with that GPU picture. Thumbnails
can remain blank/stale on either model until a GPU readback path exists.
Validate restored gameplay separately. The release branch's existing Old 3DS
Continue-screenshot limitation and memory layout are unchanged by its audio port.

## Frameskip and other ideas

`fs3` changes only the Old 3DS skip interval to 3; the default remains 2 and the
selected skip policy remains intact. Remove it to restore defaults. `fs1`,
explicit `o:frameskip_interval=...` and fast-forward retain precedence. Existing
Core options also exposes interval 3. No timing/presentation changes overlap
`t3/improve-frameskip-performance`.

No additional features were taken:

- Half-height software rasterization is irrelevant to the GPU renderer.
- Coarse fixed-cycle idle charging would weaken our exact-timing idle skip.
- Syscore CD read-ahead, SPU work sharing and event profiling already exist.
- Nonblocking CD-cache locking needs dedicated concurrency review/measurement;
  duplicate disc reads could cancel the benefit.
- Timing-based frameskip/holding unchanged frames belongs to the other worker.
- Primitive profiler sampling mostly addresses software-renderer syscall cost.
- Higher syscore limits were already rejected for system/input starvation.

## Hardware owner's test steps

No consoles were touched. Back up existing states/configuration before testing.

1. Install each branch with the normal CIA build workflow. On the GPU branch,
   Old 3DS Crash slot 2: `stats noskip acap game=Crash_Bandicoot state=2`. Use
   the exact installed library title for `game=`; loading through the menu is
   equivalent. Run with **Smooth audio when slow** off for 20–30 seconds, then
   on for 20–30 seconds. Repeat CTR, GT2 and R4. Listen for gradual lower pitch
   and fewer repeated cracks/gaps; inspect `underruns` and `rate` in `log.txt`.
   Repeat under default auto frameskip with `noskip` removed. Below half speed,
   some gaps are an expected limit.
2. Fresh-launch the same state twice with `acap`, off then on: core hash must
   match. Repeat with `nosputhread` if diagnosing work sharing, comparing that
   pair separately from threaded output. Optionally repeat with `csnd` for the
   fallback. The integrated v0.3.0 release retains both diagnostic switches.
3. On New 3DS use full-speed Crash/Silent Hill, frameskip off, setting on/off.
   Pitch must remain unchanged; logged rate should stay within ±0.5%. Also
   try a slower scene. On release main use Core options to disable frameskip
   rather than GPU-only switches. On both models test menu pause, HOME/sleep,
   game changes and setting persistence across application restarts.
4. Old 3DS GPU build: populate all ten slots, repeatedly save/load Crash, CTR
   and GT2, and return to library between games. Enable **Continue where I
   left off**, exit, relaunch and choose Continue. Verify actual restored
   position. Check for no LUT map failure, `OOM for vout_buf`, state allocation
   failure or missing-thumbnail warning. Startup linear heap should be ≥19 MiB.
   Blank/stale thumbnails remain a separate renderer limitation; record them
   independently from state restoration success.
5. Compare Old 3DS `stats` with/without `fs3`, default auto policy, no `noskip`,
   `fs1` or explicit interval overrides, Crash/CTR/GT2 slot 2. Judge game speed,
   input response and picture choppiness. Interval should show 3 only for the
   test run; removing `fs3` restores 2.
6. Play GT2/CTR for 10–15 minutes on both models, including scene transitions,
   then save/load and Continue again. Watch for cache recompilation hitches or
   FPS/PMU regressions: simulator results cannot predict Old 3DS I-cache layout.

## Release integration status

The audio/memory group is retained for functionality, independently of the
renderer hot-path speed keeper. An external Old 3DS run saved Crash slot 2 to
spare slot 9 and loaded it successfully with the combined memory changes.
**Known issue: the Old 3DS save-state thumbnail was missing (“No preview”).**
State data and restored gameplay worked; long-session memory stress and this
complete release binary still require hardware acceptance.
