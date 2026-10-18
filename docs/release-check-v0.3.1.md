# PSX3D v0.3.1 candidate hardware check

> Historical report: submodule/patch instructions apply to its original commit.
> Current builds use the committed `core/` directory; see [vendor-core.md](vendor-core.md).

Local candidate; unpublished. App version 0.3.1, CIA title counter 3076.
The exact executable, source commit and SHA256 are recorded with the artifacts
under the main checkout's ignored `devtools/release_v031/`.

## Conditions

Old 3DS: BootNTR loaded, inherited requested quality 30 / 128 KiB/s active stream.
New 3DS: no NTR; a passive UDP observer records continued silence. These are
separate conditions. No stream-free claim is made for the Old 3DS candidate.
Stock PS1 clock and 30% syscore allowance; Old slot 2 / New slot 0. Warmup 10 s,
15 complete one-second sample intervals; end pictures occur after sampling.
Each console session holds its own flock. No protected state is written.

The Old gate2 comparison comes from the matched low-stream six-game matrix in
[the performance report](old-3ds-performance-final.md). New gate2 was remeasured
under the same no-NTR condition as this candidate. FPS means emulated vblanks per
second, not distinct pictures. Automatic frameskip improves game speed by drawing
less frequently. Short samples and sparse stills do not establish whole-game
accuracy, absence of flicker, or an always-full-speed guarantee.

## Included changes

Held presentation, integer audio drift correction, the measured submission and
primitive paths, texture mask/memo and guarded skip paths, and split renderer
layout are defaults. Developer A/B controls remain available. Dense cadence is
opt-in with cap 4; the normal frameskip default is unchanged. Idle pointer/delay
experiments remain off; the rejected vertex shader experiment is excluded.
GPU save-state previews, the Old cover-worker priority fix and token-gated UI
capture commands are integrated. SotN retains its Software preset on both models.

## Validation

Host developer lifecycle/transport/updater, frontend UI/presets, frameskip,
polygon/held presentation and layout checks pass. Integer resampling passes
full-scale Q12 phases and chunk/kernel transitions under ASan/UBSan. The actual
core sources regenerate patch 0002 byte-for-byte and pass reverse-apply checks.

## Crash's purple idle animation

The suspected magenta corruption is the game's Wumpa-fruit idle animation.
Crash gets covered in purple juice and spins it off. The game deliberately
selects a purple model colour table present on the disc; the reference renderer,
strict interpreter and released Software renderer show the same sequence.
No colour-suppression patch was made. A zero-magenta screenshot target would
remove correct game content. Full local trace and RAM/disc evidence are preserved
under `devtools/release_v031/magenta/`.

See [the colour investigation](crash-idle-colour.md) for reproducible disc-table
verification and old/new-path comparison. Each hardware idle series contains
72 lossless screenshots; intended purple frames are retained as evidence.

## Measured candidate against v0.3.0

These are the exact final RC2 executable's results, not the earlier research
binary's numbers. There are 29 successful runs, 435 complete one-second
intervals and zero near-black probes. All New samples and all Old auto samples
report zero audio underruns and drops. Old frameskip-off CTR, GT2 and SotN
Software still underrun; SotN GPU/Software off also report dropped audio samples.
Rates below are events/s for underruns and samples/s for drops. Audio was not
listened to remotely. The release build lacks these audio diagnostics.

The missing Old SotN Software baseline and New SotN GPU-auto baseline were not
remeasured after downgrade restoration was blocked. A dash means unavailable,
not zero. Historical no-BootNTR results are not substituted for matched numbers.

## Old 3DS

BootNTR loaded, inherited requested quality 30 / 128 KiB/s active stream.

| Game | Renderer | Frameskip | v0.3.0 FPS | v0.3.1 FPS | Change | v0.3.0 speed | v0.3.1 speed | Underruns/s | Drops/s |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| Crash Bandicoot | gpu | off | 46.35 | 49.09 | +5.92% | 77.24% | 81.82% | 0.000 | 0.000 |
| Crash Bandicoot | gpu | auto | 59.64 | 59.98 | +0.57% | 99.40% | 99.97% | 0.000 | 0.000 |
| Crash Team Racing | gpu | off | 29.12 | 30.97 | +6.38% | 48.53% | 51.62% | 0.932 | 0.000 |
| Crash Team Racing | gpu | auto | 37.18 | 39.55 | +6.35% | 61.97% | 65.91% | 0.000 | 0.000 |
| Gran Turismo 2 | gpu | off | 28.12 | 31.14 | +10.75% | 46.87% | 51.91% | 0.136 | 0.000 |
| Gran Turismo 2 | gpu | auto | 37.02 | 41.12 | +11.07% | 61.71% | 68.54% | 0.000 | 0.000 |
| Silent Hill | gpu | off | 34.10 | 38.15 | +11.86% | 56.83% | 63.58% | 0.000 | 0.000 |
| Silent Hill | gpu | auto | 46.37 | 51.36 | +10.75% | 77.29% | 85.60% | 0.000 | 0.000 |
| Ridge Racer Type 4 | gpu | off | 51.93 | 58.50 | +12.66% | 86.55% | 97.50% | 0.000 | 0.000 |
| Ridge Racer Type 4 | gpu | auto | 58.52 | 60.00 | +2.53% | 97.54% | 100.01% | 0.000 | 0.000 |
| Castlevania - Symphony of the Night | gpu | off | 51.98 | 54.69 | +5.22% | 86.63% | 91.16% | 0.000 | 8.812 |
| Castlevania - Symphony of the Night | gpu | auto | 60.01 | 60.00 | -0.02% | 100.02% | 100.00% | 0.000 | 0.000 |
| Castlevania - Symphony of the Night | sw | off | — | 51.83 | — | — | 86.39% | 0.200 | 1074.536 |
| Castlevania - Symphony of the Night | sw | auto | — | 59.99 | — | — | 99.98% | 0.000 | 0.000 |

## New 3DS

No NTR; fresh passive silence recorded.

| Game | Renderer | Frameskip | v0.3.0 FPS | v0.3.1 FPS | Change | v0.3.0 speed | v0.3.1 speed | Underruns/s | Drops/s |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| Crash Bandicoot | sw | off | 60.00 | 59.97 | -0.05% | 100.00% | 99.95% | 0.000 | 0.000 |
| Crash Bandicoot | sw | auto | 60.00 | 60.00 | +0.00% | 100.00% | 100.00% | 0.000 | 0.000 |
| Crash Team Racing | sw | off | 59.99 | 59.97 | -0.04% | 99.99% | 99.94% | 0.000 | 0.000 |
| Crash Team Racing | sw | auto | 59.99 | 60.00 | +0.00% | 99.99% | 99.99% | 0.000 | 0.000 |
| Gran Turismo 2 | sw | off | 60.00 | 60.00 | -0.00% | 100.00% | 100.00% | 0.000 | 0.000 |
| Gran Turismo 2 | sw | auto | 59.99 | 59.99 | +0.01% | 99.98% | 99.99% | 0.000 | 0.000 |
| Silent Hill | sw | off | 60.00 | 60.00 | -0.00% | 100.00% | 100.00% | 0.000 | 0.000 |
| Silent Hill | sw | auto | 59.99 | 59.98 | -0.01% | 99.98% | 99.97% | 0.000 | 0.000 |
| Ridge Racer Type 4 | sw | off | 60.03 | 59.99 | -0.06% | 100.05% | 99.99% | 0.000 | 0.000 |
| Ridge Racer Type 4 | sw | auto | 60.03 | 60.02 | -0.01% | 100.04% | 100.04% | 0.000 | 0.000 |
| Castlevania - Symphony of the Night | sw | off | 60.00 | 60.00 | -0.00% | 100.00% | 100.00% | 0.000 | 0.000 |
| Castlevania - Symphony of the Night | sw | auto | 60.00 | 60.00 | -0.01% | 100.00% | 99.99% | 0.000 | 0.000 |
| Crash Bandicoot | gpu | off | 60.00 | 60.00 | +0.00% | 100.00% | 100.00% | 0.000 | 0.000 |
| Castlevania - Symphony of the Night | gpu | off | 60.00 | 60.00 | -0.01% | 100.00% | 100.00% | 0.000 | 0.000 |
| Castlevania - Symphony of the Night | gpu | auto | — | 60.00 | — | — | 100.01% | 0.000 | 0.000 |

## UI and save states

Both consoles passed the real library/box-art, pause and global/game-settings
captures. GPU-held slot 9 save, thumbnail browser, resume and load passed on
both; protected benchmark slots 2/0 were never written. Direct default checks
confirmed Old Crash GPU, New Crash Software and SotN Software on both.
README pictures are fresh remote PNGs, with lossless screen crops where needed.
The Old console clock is incorrect; host UTC records establish capture order.

## Final console state and restore limitation

Read-only status and top/bottom screenshots at 11:30:40–41 UTC on 2026-10-18
confirmed **both consoles responsive in the library on RC2**, build
`release-v031-rc2-split`, with no active benchmark. They are **not restored to
released v0.3.0**.

The New 3DS gate2 restore failed during the AM install read/write loop with
`CIA install read/write failed; use FBI recovery`. The updater calls
`AM_CancelCIAInstall` on that failure path. The same RC2 process remained
responsive. This does not establish installed-title integrity. No retry, exit,
HOME, reboot or FBI recovery was attempted.

The failed restore installs gate2 title counter 3075 over RC2 counter 3076;
downgrade rejection is a plausible explanation, not a confirmed result code.
The host client discarded the numeric error. To avoid repeating the demonstrated
failure path, Old restoration was not attempted. Its successfully installed RC2
was left in the library. Restoring gate2 safely remains outstanding for an
attended session. The requested exact gate2 CIA is archived locally.

Old retains its inherited low NTR stream; New remains without NTR. No console
went offline. There is no pending benchmark, update or temporary CIA server.
Nothing has been pushed, tagged or published.
