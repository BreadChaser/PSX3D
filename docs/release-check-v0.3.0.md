# PSX3D v0.3.0 final hardware check

Branch `release/v0.3.0-final`; executable source `4ed8026`. App 0.3.0,
CIA title counter 3075. Developer identity `release-v030-rttgate-2`.
No push/publication performed.

## Measurement conditions

Both consoles rebooted without BootNTR/NTR-HR. No viewer requests; passive UDP
8001 monitor verified silence before/after every batch. Same CIA, normal clock,
5 s warmup, 20 s sampling target (19–20 complete one-second intervals per run),
Old slot 2 / New slot 0. Per-console flock held only
for each automated batch. Build identity checked and release reinstalled when
the other worker's build was present. End screenshots occur after sampling.
Software uses Old half-height polygons; New/Old states differ. Host FPS is not
unique images; sparse probes/screenshots cannot prove no flicker or broad accuracy.

## Renderers, frameskip off

| Game | Old GPU FPS | Old Software FPS | New Software FPS |
|---|---:|---:|---:|
| Crash Bandicoot | 47.77 | 22.76 | 59.99 |
| Crash Team Racing | 30.02 | 20.59 | 59.99 |
| Gran Turismo 2 | 28.11 | 21.50 | 60.00 |
| Silent Hill | 35.01 | 28.64 | 60.03 |
| Ridge Racer Type 4 | 53.65 | 32.00 | 60.02 |
| Castlevania: SotN (Dracula fight) | 53.47 | 56.01 | 60.00 |

## Automatic RTT gate

| Old 3DS / GPU | Auto RTT | Forced old RTT/cache | Forced new RTT/cache | Auto vs best forced |
|---|---:|---:|---:|---:|
| Crash Bandicoot | 47.77 | 48.52 | 45.99 | -1.55% |
| Crash Team Racing | 30.02 | 28.58 | 30.01 | +0.05% |
| Gran Turismo 2 | 28.11 | 28.09 | 28.02 | +0.07% |
| Silent Hill | 35.01 | 33.36 | 34.98 | +0.08% |
| Ridge Racer Type 4 | 53.65 | 33.41 | 53.64 | +0.01% |

R4 measured 53.65 FPS with automatic RTT and 33.41 with both old paths (+60.58%). The gain persists without streaming, so the earlier R4 jump was not solely a streaming artifact.

## SotN, frameskip off and automatic

| Console | GPU, retained regions | GPU, single square | Software | GPU, auto frameskip | Software, auto frameskip |
|---|---:|---:|---:|---:|---:|
| Old 3DS | 53.47 | 13.90 | 56.01 | 60.00 | 60.00 |
| New 3DS | 60.00 | 31.02 | 60.00 | 60.00 | 60.00 |

Menu measurements use spare slot 8, with the same warmup/sample durations.

| Console / SotN menu | Automatic GPU | Old RTT/cache | Software |
|---|---:|---:|---:|
| Old 3DS | 60.00 | 36.81 | 52.45 |
| New 3DS | 60.00 | 60.00 | 60.00 |

No fresh title FPS is published: the spare title state had been replaced by the
menu, and the earlier title measurements used different streaming conditions.

Keep Software preset on BOTH consoles per user decision: GPU now runs close to
Software in the tested fight; Software has the proven picture. Verified IDs:
SLUS00067, SLES00524, SCPS45196, SLPM86023, SLPM86073, SLPM87328
(libretro-database Redump DAT). USA hardware tested.

## Silent Hill cache isolation

| Console | GPU new cache | GPU old cache | Change |
|---|---:|---:|---:|
| old | 34.98 | 35.17 | -0.54% |
| new | 60.01 | 59.99 | +0.03% |

All 45 runs passed. Across these runs, 899 one-second probes recorded 0 near-black samples. This is sparse sampling, not a frame-by-frame flicker audit.

## Threshold flags

- Old Crash Bandicoot: automatic vs original paths -1.55%: no cost exceeding 2%.
- Old Silent Hill: automatic vs original paths +4.93%: no cost exceeding 2%.
- Old Silent Hill: retained vs single-square cache -0.54%: within the 3% regression threshold.
- New Silent Hill: retained vs single-square cache +0.03%: within the 3% regression threshold.

## RC02 screening comparison

RC02 used different streaming conditions and New HBL rather than this CIA.
These changes flag follow-ups; they do not isolate source changes.

| Console / default renderer | Game | Current FPS | RC02 FPS | Change | Flag |
|---|---|---:|---:|---:|---|
| old | Crash Bandicoot | 47.77 | 40.81 | +17.06% | within threshold |
| old | Crash Team Racing | 30.02 | 25.89 | +15.96% | within threshold |
| old | Gran Turismo 2 | 28.11 | 29.55 | -4.87% | REGRESSION >3% |
| old | Silent Hill | 35.01 | 31.02 | +12.85% | within threshold |
| old | Ridge Racer Type 4 | 53.65 | 34.80 | +54.16% | within threshold |
| new | Crash Bandicoot | 59.99 | 59.98 | +0.02% | within threshold |
| new | Crash Team Racing | 59.99 | 59.06 | +1.58% | within threshold |
| new | Gran Turismo 2 | 60.00 | 60.00 | +0.00% | within threshold |
| new | Silent Hill | 60.03 | 59.97 | +0.09% | within threshold |
| new | Ridge Racer Type 4 | 60.02 | 60.01 | +0.02% | within threshold |

## GT2 transition correction

The first automatic gate (a63bece) kept oversized legacy cache allocations after
activation. A same-binary no-stream check found automatic 27.28 vs forced-old
28.35 FPS. Native replay reproduced 22 evictions and 61 tile decodes in frame 60,
while forced-new had zero of either. Commit 4ed8026 retires the legacy entries
at the next texture lookup after draining GPU readers; all 60 complete-VRAM
hashes match the earlier gate. The final table above is measured from that fix,
not mixed with the rejected transition's results.

## Validation and artifacts

Gate host checks: ordinary rendering stays coarse; sampling GPU-drawn areas,
readback and VRAM-copy detection enable fine tracking conservatively. State/game
reset drops the gate. The transition retires legacy oversized cache allocations at
the next lookup after draining readers, avoiding sustained GT2 cache thrashing.
RTT fixtures match scanout/full VRAM in old/cache-off/new/
automatic modes with delayed atlas-reader guards. ASan/UBSan developer tests,
frontend controls/presets/settings and builds passed; patch reverse-apply passes.
Normal startup creates no developer thread/socket/history; on-card `remote` plus
nonempty token required for server and self-update. Existing executable-stack
linker warning remains.

Artifacts: main checkout `devtools/release_final/gated2-artifacts/`;
full raw batch files, status histories, TSV, PNGs and streaming evidence under
`devtools/release_final/nostream2/`. SHA256:

```text
fa690587e25e07f91bc7f521c3ec60226eae21438998b8b68f3991885c7c5429  PSX3D.cia
413ed0eb34cae6d2fa77b9db30ecf70f267e8b011de38ada684cea03fdc54665  PSX3D.3dsx
c992ade6a54ef456e6d8c0730b6fa32f0e3992a3805d1b82791bf8b347ef7787  PSX3D.elf
```

## Screenshots and state checks

Both consoles passed an explicit Crash load → save slot 9 → load slot 9 check
on the final executable. Protected benchmark slots Old 2 / New 0 were not written.

Current remote `shot` captures from the final Old 3DS build:

| File under `docs/screenshots/` | Size | Contents |
|---|---|---|
| `library.png` | 400×480 | Both screens; real remote-mode library, covers suppressed |
| `crash.png` | 400×240 | Crash Bandicoot top screen |
| `silent-hill.png` | 400×240 | Silent Hill top screen |
| `touch-layout.png` | 320×240 | Full Old 3DS touch layout and FPS readout |

Pause-menu/settings captures are pending InputRedirection on the New 3DS.
The verified Old MENU tap left gameplay visible; the script stopped before
sending any further input. The existing README menu image has not yet been
replaced. No screen content is fabricated. PNG crops/compression are lossless.

SotN GPU/Software HUD and menu captures were inspected. No gross new HUD/menu
corruption was observed; sparse captures and one-second probes do not establish
frame-by-frame flicker freedom or whole-game accuracy. Software remains the
SotN preset. Final Old console restoration is pending the parallel performance
worker's queue; it must restore and verify `release-v030-rttgate-2` at the end.


## Publishing (requires user approval)

Merge release/v0.3.0-final into main; tag v0.3.0; push gpu-renderer/main and tag;
create GitHub release with PSX3D.cia, PSX3D.3dsx and SHA256SUMS. CIA asset must
be named PSX3D.cia for the stable FBI QR URL. Notes/QR accompany the release.
After v0.3.0, vendor/prune core and update the build instructions.
