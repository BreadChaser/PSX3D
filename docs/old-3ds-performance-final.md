# Old 3DS final performance report — 2026-10-18

Raw FPS is the priority. The final full stack with split layout gains 4.2–12.5% frameskip-off throughput versus matched gate2. It does not achieve full speed in CTR, GT2 or Silent Hill. Crash and R4 reach near 100% sampled speed at 17.11/17.83 distinct pictures/s with dense; SotN GPU reaches near 100% at 37.62 pictures/s with legacy auto. These short averages do not establish the user’s always-full-speed target. The purple Crash model defect is positively present in the released gate2 build, as shown in the comparison below.

All final performance comparisons use the same active low NTR condition; they are separate from the earlier stream-free release acceptance matrix.

| Game | Gate2 emulated fps, skip off | Stack emulated fps, skip off | Change | Stack distinct pictures/s, skip off |
|---|---:|---:|---:|---:|
| Crash | 46.35 | 50.96 | 9.96% | 25.51 |
| CTR | 29.12 | 30.72 | 5.50% | 15.36 |
| GT2 | 28.12 | 31.22 | 11.04% | 15.61 |
| Silent Hill | 34.10 | 37.99 | 11.41% | 19.00 |
| R4 | 51.93 | 58.44 | 12.54% | 29.32 |
| SotN fight | 51.98 | 54.17 | 4.22% | 54.17 |

## Final Old 3DS comparison

GPU, protected slot 2, stock clock and syscore allowance. All rows below use BootNTR loaded with an active requested quality 30 / 128 KiB/s stream, verified by a fresh passive observer. Release is the immutable gate2 CIA; candidate is o3ds60-17-pica-split (7472f3b…) with the selected per-game stack. These measurements are separate from the earlier no-BootNTR release matrix.

Speed is emulated vblanks per second divided by 60. Candidate pictures/s counts admitted new picture decisions, normalized to measured wall time. Gate2 lacks this metric: its picture/present counters include held images and cannot establish distinct pictures/s. PMU draw/skip/idle buckets classify emitted renderer work, not visible picture decisions: GT2/SotN can retain offscreen drawing during skipped pictures, and an unavailable skip bucket means no pure skip population was observed. Target met means the sampled mean was within 0.5% of 100%, with zero reported audio underruns; it is not an always-full-speed guarantee.

| Game | Release off fps | Candidate off fps | Off change | Release auto speed | Candidate auto speed / pictures/s | Candidate dense speed / pictures/s | Dense underruns/s | Target met |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| Crash | 46.35 | 50.96 | 9.96% | 99.40% | 99.89% / 11.25 | 99.74% / 17.11 | 0.000 | yes |
| CTR | 29.12 | 30.72 | 5.50% | 61.97% | 65.46% / 6.58 | 69.54% / 4.22 | 0.000 | no |
| GT2 | 28.12 | 31.22 | 11.04% | 61.71% | 69.29% / 6.92 | 70.03% / 6.53 | 0.331 | no |
| Silent Hill | 34.10 | 37.99 | 11.41% | 77.29% | 85.21% / 9.26 | 93.51% / 5.61 | 0.267 | no |
| R4 | 51.93 | 58.44 | 12.54% | 97.54% | 100.04% / 9.99 | 99.95% / 17.83 | 0.000 | yes |
| SotN fight | 51.98 | 54.17 | 4.22% | 100.02% | 100.01% / 37.62 | 100.05% / 34.96 | 0.000 | yes |

## Crash

Selected request: `game=Crash_Bandicoot state=2 renderer=gpu texmask=new texmemo=new skipgate=new pica_prims_fast submitstate=new submitclip=new submittex=new warmup=10 seconds=15 pmu=0,2 shot=1 frameskip=auto rtfs=dense`

Largest off-mode measured groups per vblank: emulation excluding PICA 12.48ms, PICA CPU 6.42ms, frontend audio 0.71ms. SPU lies within emulation; PICA includes CPU-side GPU submission/waits.

Fixed draw-one/skip-one picture cadence: 99.99% speed, 15.00 pictures/s; drawn/skipped/idle 6.935/3.572/2.773 Mcycles; underruns 0.000/s.

| Mode | Draw / skip / idle Mcycles | Run ms | PICA ms (inside run) | SPU / wait ms (inside run) | Present ms | Audio ms | Loop ms | Underruns/s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| off | 6.777 / — / 3.120 | 18.90 | 6.42 | 0.55 / 0.02 | 0.30 | 0.71 | 0.15 | 0.135 |
| legacy auto | 6.909 / 3.547 / 2.621 | 14.23 | 2.65 | 0.53 / 0.02 | 0.17 | 0.89 | 0.14 | 0.000 |
| dense | 6.926 / 3.572 / 2.856 | 15.93 | 3.87 | 0.54 / 0.02 | 0.21 | 0.89 | 0.21 | 0.000 |
| fixed 1:1 pictures | 6.935 / 3.572 / 2.773 | 15.33 | 3.44 | 0.53 / 0.02 | 0.20 | 0.88 | 0.20 | 0.000 |

## CTR

Selected request: `game=Crash_Team_Racing state=2 renderer=gpu texmask=new texmemo=new skipgate=new pica_prims_fast submitstate=new submitclip=new submittex=new warmup=10 seconds=15 pmu=0,2 shot=1 frameskip=auto rtfs=dense`

Largest off-mode measured groups per vblank: emulation excluding PICA 21.64ms, PICA CPU 10.42ms, frontend audio 0.78ms. SPU lies within emulation; PICA includes CPU-side GPU submission/waits.

Fixed draw-one/skip-one picture cadence: 60.04% speed, 9.01 pictures/s; drawn/skipped/idle 9.388/5.530/5.843 Mcycles; underruns 0.000/s.

| Mode | Draw / skip / idle Mcycles | Run ms | PICA ms (inside run) | SPU / wait ms (inside run) | Present ms | Audio ms | Loop ms | Underruns/s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| off | 9.206 / — / 5.841 | 32.06 | 10.42 | 1.55 / 0.29 | 0.32 | 0.78 | 0.14 | 0.951 |
| legacy auto | 9.173 / 5.491 / 5.785 | 25.13 | 3.62 | 1.56 / 0.29 | 0.16 | 0.74 | 0.14 | 0.000 |
| dense | 9.173 / 5.489 / 5.735 | 23.64 | 2.24 | 1.52 / 0.24 | 0.12 | 0.74 | 0.18 | 0.000 |
| fixed 1:1 pictures | 9.388 / 5.530 / 5.843 | 27.28 | 5.64 | 1.57 / 0.27 | 0.21 | 0.77 | 0.22 | 0.000 |

## GT2

Selected request: `game=Gran_Turismo_2 state=2 renderer=gpu texmask=new texmemo=new skipgate=new pica_prims_fast submitstate=new submitclip=new submittex=new warmup=10 seconds=15 pmu=0,2 shot=1 frameskip=auto rtfs=dense`

Largest off-mode measured groups per vblank: emulation excluding PICA 20.47ms, PICA CPU 11.05ms, frontend audio 0.82ms. SPU lies within emulation; PICA includes CPU-side GPU submission/waits.

Fixed draw-one/skip-one picture cadence: 63.52% speed, 9.56 pictures/s; drawn/skipped/idle 7.162/—/6.445 Mcycles; underruns 0.000/s.

| Mode | Draw / skip / idle Mcycles | Run ms | PICA ms (inside run) | SPU / wait ms (inside run) | Present ms | Audio ms | Loop ms | Underruns/s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| off | 10.097 / — / 6.572 | 31.52 | 11.05 | 0.67 / 0.02 | 0.31 | 0.82 | 0.18 | 0.134 |
| legacy auto | 7.370 / 4.262 / 6.158 | 23.70 | 4.10 | 0.66 / 0.02 | 0.17 | 0.71 | 0.16 | 0.000 |
| dense | 7.503 / 4.233 / 6.186 | 23.70 | 4.02 | 0.66 / 0.02 | 0.17 | 0.71 | 0.19 | 0.331 |
| fixed 1:1 pictures | 7.162 / — / 6.445 | 25.78 | 5.63 | 0.66 / 0.02 | 0.22 | 0.73 | 0.21 | 0.000 |

## Silent Hill

Selected request: `game=Silent_Hill state=2 renderer=gpu texmask=new texmemo=new skipgate=new pica_prims_fast submitstate=new submitclip=new submittex=new warmup=10 seconds=15 pmu=0,2 shot=1 frameskip=auto rtfs=dense`

Largest off-mode measured groups per vblank: emulation excluding PICA 16.17ms, PICA CPU 9.54ms, frontend audio 0.72ms. SPU lies within emulation; PICA includes CPU-side GPU submission/waits.

Fixed draw-one/skip-one picture cadence: 79.53% speed, 11.88 pictures/s; drawn/skipped/idle 9.157/4.373/3.959 Mcycles; underruns 0.000/s.

| Mode | Draw / skip / idle Mcycles | Run ms | PICA ms (inside run) | SPU / wait ms (inside run) | Present ms | Audio ms | Loop ms | Underruns/s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| off | 9.457 / — / 4.121 | 25.71 | 9.54 | 0.21 / 0.02 | 0.31 | 0.72 | 0.13 | 0.000 |
| legacy auto | 9.398 / 4.373 / 3.870 | 19.09 | 3.55 | 0.22 / 0.02 | 0.17 | 0.59 | 0.13 | 0.000 |
| dense | 9.407 / 4.362 / 3.752 | 17.33 | 2.04 | 0.21 / 0.02 | 0.11 | 0.59 | 0.16 | 0.267 |
| fixed 1:1 pictures | 9.157 / 4.373 / 3.959 | 20.31 | 4.59 | 0.21 / 0.02 | 0.22 | 0.61 | 0.21 | 0.000 |

## R4

Selected request: `game=Ridge_Racer_Type_4 state=2 renderer=gpu texmask=new texmemo=new skipgate=new pica_prims_fast submitstate=new submitclip=new submittex=new warmup=10 seconds=15 pmu=0,2 shot=1 frameskip=auto rtfs=dense`

Largest off-mode measured groups per vblank: emulation excluding PICA 13.16ms, PICA CPU 3.43ms, frontend audio 0.62ms. SPU lies within emulation; PICA includes CPU-side GPU submission/waits.

Fixed draw-one/skip-one picture cadence: 100.00% speed, 15.05 pictures/s; drawn/skipped/idle 7.218/5.519/1.536 Mcycles; underruns 0.000/s.

| Mode | Draw / skip / idle Mcycles | Run ms | PICA ms (inside run) | SPU / wait ms (inside run) | Present ms | Audio ms | Loop ms | Underruns/s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| off | 7.222 / — / 1.464 | 16.59 | 3.43 | 0.49 / 0.02 | 0.30 | 0.62 | 0.11 | 0.000 |
| legacy auto | 7.273 / 5.503 / 1.473 | 14.48 | 1.20 | 0.51 / 0.02 | 0.15 | 0.84 | 0.12 | 0.000 |
| dense | 7.281 / 5.510 / 1.563 | 15.56 | 2.07 | 0.51 / 0.02 | 0.22 | 0.87 | 0.19 | 0.000 |
| fixed 1:1 pictures | 7.218 / 5.519 / 1.536 | 15.16 | 1.72 | 0.51 / 0.02 | 0.21 | 0.88 | 0.19 | 0.000 |

## SotN fight

Selected request: `game=Castlevania_-_Symphony_of_the_Night state=2 renderer=gpu texmask=new texmemo=new skipgate=new pica_prims_fast submitstate=new submitclip=new submittex=new warmup=10 seconds=15 pmu=0,2 shot=1 frameskip=auto rtfs=dense`

Largest off-mode measured groups per vblank: PICA CPU 9.81ms, emulation excluding PICA 7.94ms, frontend audio 0.65ms. SPU lies within emulation; PICA includes CPU-side GPU submission/waits.

Fixed draw-one/skip-one picture cadence: 100.01% speed, 30.00 pictures/s; drawn/skipped/idle 3.179/—/— Mcycles; underruns 0.000/s.

| Mode | Draw / skip / idle Mcycles | Run ms | PICA ms (inside run) | SPU / wait ms (inside run) | Present ms | Audio ms | Loop ms | Underruns/s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| off | 4.004 / — / — | 17.75 | 9.81 | 0.39 / 0.01 | 0.55 | 0.65 | 0.14 | 0.000 |
| legacy auto | 3.500 / — / — | 15.02 | 6.63 | 0.40 / 0.01 | 0.40 | 0.86 | 0.15 | 0.000 |
| dense | 3.428 / — / — | 14.71 | 6.37 | 0.40 / 0.01 | 0.41 | 0.85 | 0.20 | 0.000 |
| fixed 1:1 pictures | 3.179 / — / — | 13.70 | 5.59 | 0.40 / 0.01 | 0.40 | 0.87 | 0.20 | 0.000 |

# Layout six-game screen

Same fixed low NTR, pmurender on, warm5/sample15. Each binary tests old and fullstack switches.

|Game|Old baseline fps|Old split fps|Old gain%|Stack baseline fps|Stack split fps|Stack gain%|
|---|---:|---:|---:|---:|---:|---:|
|Crash Bandicoot|42.133|44.149|4.784|46.994|48.293|2.764|
|Silent Hill|32.965|33.438|1.436|36.767|37.549|2.129|
|Ridge Racer Type 4|52.687|52.831|0.274|56.955|57.518|0.990|
|Crash Team Racing|28.536|28.404|-0.463|30.414|30.519|0.347|
|Gran Turismo 2|26.057|26.165|0.412|29.143|29.499|1.219|
|Castlevania - Symphony of the Night|51.055|50.046|-1.977|54.484|53.659|-1.514|

|Stack renderer|Baseline Mcycles|Split Mcycles|I-cache k baseline→split|D-read k baseline→split|PICA ms baseline→split|
|---|---:|---:|---:|---:|---:|
|Crash Bandicoot|2.029|1.849|10.281→7.482|2.455→2.593|7.386→6.867|
|Silent Hill|2.920|2.717|17.409→13.726|4.063→3.891|10.115→9.588|
|Ridge Racer Type 4|0.975|0.948|5.340→4.800|1.120→1.144|3.566→3.449|
|Crash Team Racing|2.025|1.979|9.578→9.348|3.323→3.506|10.532→10.407|
|Gran Turismo 2|3.340|3.200|17.033→14.474|3.702→3.708|12.256→11.847|
|Castlevania - Symphony of the Night|1.988|2.126|12.871→14.750|3.603→3.825|9.588→9.920|

## Crash purple-fragment attribution

Later investigation identified this as the game's authored Wumpa-fruit idle
animation. See [the traced colour-table evidence](crash-idle-colour.md). The
observations below record the earlier attribution stage, before that finding.

**The purple body/head fragment exists in the RELEASED v0.3.0 gate2 build too.** It is not exclusive to the new submission, primitive, texture, skip-gate, frontend or split-layout experiments. This comparison establishes presence, not that every path has the same frequency or that the defect is fixed.

Old 3DS, Crash protected state 2, GPU, frameskip off, RTT auto, same beach scene; BootNTR loaded with the unchanged active requested quality 30/128KiB/s stream. Gate2 identity and immutable CIA SHA256 were checked before loading. Four lossless release stills initially missed the intermittent fragment. A subsequent 35s passive recording of the existing stream caught it in the same idle-animation pose. No NTR request or input was sent by the receiver.

| Path | Purple observed | Evidence |
|---|---|---|
| Released gate2, CIA fa690587… | yes | `devtools/o3ds60/ntr-low-pictures-release-passive/release-purple.png` |
| Candidate build17, baseline layout, all new renderer switches OLD | yes | `devtools/o3ds60/ntr-low-pictures-stack17/path-01-03.png` |
| Candidate integrated split17, full renderer stack | yes | `devtools/o3ds60/ntr-low-layout17-final-crash/old/0b77d7212b63-old-01.png` |
| Candidate baseline/fullstack and split/OLD dedicated short still series | not in those few stills | `ntr-low-pictures-stack17-full`, `ntr-low-pictures-split17-old` |

The release PNG is a 90-degree rotation of the recorded stream JPEG, without colour adjustment. Original: `devtools/o3ds60/ntr-low-pictures-release-passive/passive/1792320471860894030-00133.jpg`. Candidate evidence above is direct lossless remote PNG. Side-by-side content was visually inspected: both show a bright magenta head/torso/arm with the same surrounding beach/crates. The release pass contains 152 complete stream frames; the colour screen only selects candidates for visual review and is not a rigorous defect-frequency measurement. The positive RELEASE capture resolves attribution without blaming a new runtime switch. Source/root cause remains a separate correctness task.

Metadata: `devtools/o3ds60/ntr-low-pictures-release-passive/pictures-complete.json` pins release identity/SHA256/load; `colour-screen.json` records the passive frame selection. Both pictures and exact requests are preserved locally.

Clickable local evidence: [released gate2 purple frame](/home/tony/Development/PS1_3ds/devtools/o3ds60/ntr-low-pictures-release-passive/release-purple.png), [candidate old renderer switches](/home/tony/Development/PS1_3ds/devtools/o3ds60/ntr-low-pictures-stack17/path-01-03.png), [candidate full stack with split](/home/tony/Development/PS1_3ds/devtools/o3ds60/ntr-low-layout17-final-crash/old/0b77d7212b63-old-01.png).

An additional candidate baseline-layout sample with frontend/audio and renderer switches explicitly OLD yielded 106 passive frames without a positive purple frame. This short negative sample does not outweigh the direct positive released-build capture, and does not isolate a cause. The redundant pending frontend NEW sample was canceled before it ran.

## Recommended v0.3.1 defaults

Enable held presentation and integer audio drift correction; `submitstate=new submitclip=new submittex=new pica_prims_fast texmask=new texmemo=new skipgate=new` is the measured full renderer stack. Keep existing coarse-to-fine RTT auto-gating and deferred old-cache retirement. Keep `idleptr=off idledelay=off`: their hardware gains did not clear noise. Keep stock PS1 clock and 30% syscore allowance. These recommendations are from the six supplied states, not universal game compatibility certification.

Recommend split for v0.3.1: it helps Crash/SH/GT2 and is flat to slightly faster in R4/CTR. The known SotN GPU cost is 1.51% in the six-game screen and 2.02% in a follow-up ABA (0.04% baseline drift); the user accepts it because the Old preset uses Software. Final all-six GPU records use one integrated split binary, exposing the trade-off. Section/Os have not been hardware accepted. The split choice is a compile-time A/B option (`PICA_LAYOUT=baseline|split`), while the semantic optimizations retain named runtime switches. No runtime code duplication was added merely to toggle a code-layout experiment.

Dense cadence remains opt-in, cap 4 unchanged. Do not make very sparse high-cap skipping a performance strategy. The cap 9/11/15 experiment was canceled before editing/building. Near-100% sampled averages are not an always-full-speed guarantee. Retain Software for the existing SotN curated preset until broader picture validation supports changing it.

## What helped, what did not

The full stack gained 6.6–11.9% raw FPS in same-binary build14 A/B across all six games; the final matched release comparison above is the net binary result. Frontend same-binary Crash/CTR savings were 7.25%/5.87%, with presentation about 1.9/1.8→0.28 ms, but original-vs-candidate cross-binary frontend gains were flat: layout matters. The split layout Crash ABA gained 4.65% with baseline drift 0.34%; renderer cycles fell 10.3%, I-cache misses 28.4%, D-read misses stayed similar.

Additional idle polling/countdown prototypes did not help: CTR off+0.06%, FF+0.52%; GT2+0.13%, R4−0.04%, Crash−0.39%. The optional coarse shortcut was only 0.83% beyond the plain stack and was excluded. Vertex-shader conversion was flat in Crash off/on/off 49.40/49.19/48.88 fps: +0.09% versus bracketing mean, with 1.05% baseline drift. It was not expanded or integrated. Cap 5 reduced pictures without fixing CTR/SH full-speed failure. The section and Os layout binaries were not evaluated on hardware; no claim is made for them.

## Remaining costs and future work

CTR and GT2 remain above the 16.7 ms/vblank budget even when renderer pictures are skipped aggressively; their measured idle populations also exceed that budget. Improving renderer work alone cannot close those gaps. Raw PICA CPU reductions remain worthwhile because each saved millisecond improves the picture rate without requiring more skipping.

Silent Hill skipped frames cost 0.536M cycles more than idle frames in the full-stack FF probe (~2.00 ms at 268 MHz). DMA-chain handling consumes 1.278 ms of that gap (~64%); 0.312 ms PICA is nested, leaving about 0.97 ms outside PICA in command/state walking/copying. SPU and waits differ little. This is a future raw skip-path optimization target, not proof that the whole cost is safely removable. Do not confuse it with frontend presentation, which lies outside these PMU buckets.

## Starting point and reproducibility

The initial release 2d851cf six-game off table used the earlier streamed setup: Crash 44.40, CTR 28.17, GT2 25.50, SH 33.94, R4 51.90, SotN 53.21 emulated fps. Those are diagnostic starting points, not matched conditions. The final gate2 table above is the fair release comparison: same fixed low NTR, GPU, protected slot2, stock clock/syscore, 10s warm-up/15s sampling, PMU 0,2. The earlier 45-run no-BootNTR release acceptance matrix remains separate and unchanged.

The measured final CIA is `devtools/o3ds60/best-stack/PSX3D.cia`, SHA256 `7472f3b45722eb3b4be6f81c7b690c96d8093d430a2a1d9e5d4a74e730d33500`, executable source 133b73d. It preserves opt-in runtime flags; recommended defaults are not silently applied to the release CIA. Build with `PICA_LAYOUT=split`; code layout has a compile-time A/B because duplicating the renderer to toggle its layout at runtime changes the cache experiment itself. Never assume a relink has the same Old 3DS performance without remeasurement.

## Validation and limits

Final candidate records are 24 successful runs/360 complete intervals; matched release records are 12 successful runs/180 intervals. All have zero near-black probes, which does not exclude bright corruption. All results are short automated slot 2 samples, with end-of-run stills inspected for expected content. Native texture/stack replay matched 110 comparable full-VRAM frames across the six states; layout candidates matched 21,600 draw hashes. Parser/polygon/presentation sanitizer checks and CIA packaging pass. The Crash purple-fragment attribution is documented in the dedicated picture comparison above; sparse stills/probes do not establish a flicker-free renderer. Dense provides evenly spaced picture-selection decisions; actual LCD interval jitter was not measured, and the R4 result shows remaining conservative density headroom. Audio underrun rates are shown explicitly. Remote speaker listening was unavailable. FF mutes audio and is only a headroom diagnostic. No protected benchmark state was written; no push performed.

## Final console state

This is the state at the end of the earlier performance session. Later v0.3.1
candidate installs supersede it; see [the candidate check](release-check-v0.3.1.md)
for the current state and blocked downgrade restoration.

Both consoles are restored to the immutable released v0.3.0 gate2 CIA, build `release-v030-rttgate-2`, SHA256 `fa690587e25e07f91bc7f521c3ec60226eae21438998b8b68f3991885c7c5429`. Old restoration/identity/library/no-active-batch verification completed at 10:50:43 UTC; New was separately restored and reverified at 10:39 UTC. Both top/bottom library screenshots were visually reviewed. No protected benchmark slots were written. No pending driver, upload or benchmark remains.

Evidence: `devtools/o3ds60/restored-release-{old,new}/{status.json,screens.png,cia-sha256.txt}`; Old update log `restore-release-old-final.log`. Old remains BootNTR-loaded with the existing requested low 30/128KiB/s stream; no remote stop/reboot was attempted. New received no NTR request. The passive observer remains read-only on UDP8001 and never requests a stream. Its New silence-duration counter restarted with the observer at 10:44:29 UTC; this does not invalidate earlier saved silence evidence. The original shared NTR-off marker remains archived, with New-only evidence separate.
