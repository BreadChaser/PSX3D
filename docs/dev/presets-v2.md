# Presets v2 implementation and validation

Based on local `t3/core-first` (`6083ee9`). The final executable is
`presets-v2-8` (`341b323`), title counter 4100. CIA SHA256:
`448aa564222d050a9dfd079c6fb0c2ff5e3a80c7a6afd7cc8169dddac758c898`.
Artifacts and full raw evidence are in the ignored `devtools/presets-v2/`.

## Behavior

- `curated` is metadata only. Visibility/technical ownership follows the actual
  option set on that console; Omega Boost no longer takes over frameskip.
- Every explicit option has validated source/confidence in the same section.
  Original set capability values are unchanged (200 legacy serial/model views).
  Unassessed FF restrictions and extrapolated regional controls are labeled honestly.
- Advanced → Show all settings is global and persisted from either settings UI.
  Revealing does not change values (including a withheld widescreen preference). Per-game technical overrides beat presets,
  including after hiding the rows again. P marks preset values/policies and help
  shows evidence; * marks per-game overrides. Physical console limits remain.
- Technical: defaults < global < preset < per-game. Taste: defaults < preset <
  global < per-game. The schema currently offers technical policies and initial
  controls, not taste values for sound/screen/dither/FPS/3D. Explicit controls win.
- New config files save deliberate choices only, plus paths/schema. Internal
  synchronization and resolved maps/defaults are omitted. Dense legacy files retain
  all represented supported choices, including factory-valued choices: older files
  lack intent metadata, so deleting guessed defaults could lose a real choice.
  Reset all clears this retained global intent; individual game reset keeps globals.
- Strict bounded runtime parsing validates a complete file before publication.
  Required metadata, serial grammar, duplicates, options, evidence, ASCII/line/file/
  index limits and versions are checked. Lexical earlier file wins a conflict;
  the whole later file is rejected. SD fallback is file-level, not pack transaction.
- Remote preset uploads are not implemented; docs now explicitly describe manual
  copying while the app is stopped and the remote whitelist limitation (m3).
- `auto_floor` accepts 1–60 pictures/s, default 15, sparse and per console. No
  shipped preset sets it. Per-load `o:auto_floor=N` experiments do not save it.
  Frontend normalizes smart-controller picture credit by N/15; core code unchanged.

## Offline checks

- tests/presets/run.sh PASS: 40 files / 78 serials / pack 2, 200 legacy views
  identical, four inert clocks, precedence/fallback/conflicts, full-index replacement/overflow, 31 malformed-file
  corpus cases plus nine VERSION cases run against C runtime and Python checker.
- Production host input/UI/config checks PASS (ASan/UBSan), including sparse new
  saves, conservative legacy migration, Omega visibility/override/reset, floor
  inheritance, global persistence and per-model frameskip presence/precedence.
- settings navigation/drawing and production rewind engine checks PASS.
- Developer control/transport/update guards, widescreen (including policy override
  and rollback), frameskip controller, PICA layout, covers/cover worker, video
  teardown, border scanout and renderer config checks PASS.
- Smart-controller simulation validates the lower-floor speed/picture tradeoff,
  saturation when the game cannot supply the requested floor, skip-streak cap,
  and bit-identical floor-15 policy. Simulation is not hardware performance evidence.
- tools/layout/check.sh final PASS. Against core-first-2: all 80 assembly core,
  1638 C core and 2949 library code symbols keep the same addresses; core
  constants/strings move +2 pages and core data/bss +3 pages, preserving offsets.
  The required frontend hot-path Old gate passes below.
- Exact final CIA installed and booted in private Azahar profile, hidden workspace:
  New-model library 25 seconds (78 preset serials) and Crash state0 40 seconds. No abort;
  state load enters gameplay. Interpreter-only smoke; real-BIOS discovery then
  state restores HLE. Does not establish hardware speed or cold HLE correctness.

Functional host logs use *-final7 or earlier unchanged parser suites; exact executable smoke/layout logs use *-final8.

## Changed existing test expectations

1. Global reset no longer serializes inherited `map_TOUCH_L2=-2`.
2. A fixture with absent Old frameskip leaves it visible and respects the saved
   global value; explicit New frameskip=off hides only that model's row.
3. Explicit per-game technical settings beat presets; Advanced reveals owned rows.
4. SotN without a frameskip entry retains that Performance row despite curated=yes.
5. A malformed built-in file is rejected whole, rather than tolerating bad lines.
6. Cross-file conflict uses lexical order and rejects the whole later file.
7. Pre-touch-slots global files now retain default-valued explicit touch bindings
   and right-stick enable instead of guessing that they were untouched defaults.
   This is the same conservative migration rule used for other legacy settings.

The renderer fixture gained stubs for new APIs; its functional expectations did
not change. No legacy set capability value changed.

## Hardware measurements

Corrected real Old session: `hw/old-defaults/`, 2026-10-21 starting 08:06 UTC.
The first session (`hw/old-final/`) is rejected for performance claims: orderly
self-update saved stale in-memory settings over a staged defaults file. The
corrected driver uses a live Settings reset after every install, checks each
step by screenshot, verifies the written config, and removes game overrides.
Candidate 8's real Settings round trip saves only library paths and format
metadata, confirming sparse persistence on hardware.

All floor curves below use real Old, Auto, player defaults, clean states 5/6/7,
GPU, clock Auto, normal sound thread, recording/replay off and PMU 0/2. Each
non-default sample has adjacent independent floor-15 loads:
15/10/15/12/15/20/15/24/15. Warmup/sample is 3/12 seconds (NFS 1/5).
Known host NTR viewer loops are paused by PID; console BootNTR is unchanged.
Pictures/s is min(drawn,presented counter rates), an estimate rather than unique
image tracking. Floor-15 values average five loads; other values are single
samples. These are scene results, not confidence intervals or whole-game proof.
No non-default floor is shipped; the owner picks.

## Heavy off gate

| Scene | A1 speed | B speed | A2 speed | B vs mean A | A2/A1 drift | B pictures/s |
|---|---:|---:|---:|---:|---:|---:|
| Crash Bandicoot | 86.01% | 86.52% | 86.04% | +0.57% | +0.04% | 20.54 |
| Crash Team Racing | 78.03% | 78.07% | 77.85% | +0.16% | -0.23% | 23.25 |
| Ridge Racer Type 4 | 79.77% | 79.96% | 79.96% | +0.12% | +0.24% | 24.25 |
| Castlevania - Symphony of the Night | 97.77% | 97.52% | 97.89% | -0.31% | +0.12% | 59.00 |
| Silent Hill garage | 80.17% | 80.02% | 80.05% | -0.11% | -0.14% | 19.42 |
| Silent Hill alley | 69.75% | 69.31% | 69.84% | -0.69% | +0.13% | 21.00 |
| Silent Hill café | 70.41% | 70.15% | 70.46% | -0.41% | +0.08% | 16.59 |
| Gran Turismo 2 | 51.67% | 51.69% | 51.67% | +0.03% | -0.01% | 13.56 |
| Tekken 3 | 65.73% | 65.84% | 65.74% | +0.17% | +0.01% | 39.92 |
| Final Fantasy VII | 100.01% | 100.02% | 100.06% | -0.01% | +0.05% | 15.17 |
| Klonoa - Door to Phantomile | 86.76% | 86.69% | 86.37% | +0.14% | -0.44% | 52.58 |
| Crash Bandicoot 2 - Cortex Strikes Back | 73.70% | 73.16% | 74.04% | -0.96% | +0.47% | 22.17 |
| Need for Speed - High Stakes | 37.44% | 37.23% | 37.33% | -0.41% | -0.29% | 11.75 |

Worst paired change: -0.96%; best: +0.57%; mean: -0.13%. Maximum absolute A2/A1 drift: 0.47%.

## Heavy auto gate

| Scene | A1 speed | B speed | A2 speed | B vs mean A | A2/A1 drift | B pictures/s |
|---|---:|---:|---:|---:|---:|---:|
| Crash Bandicoot | 97.01% | 97.44% | 97.09% | +0.40% | +0.08% | 14.58 |
| Crash Team Racing | 89.40% | 89.06% | 89.01% | -0.17% | -0.44% | 15.17 |
| Ridge Racer Type 4 | 88.00% | 88.49% | 88.38% | +0.34% | +0.43% | 17.51 |
| Castlevania - Symphony of the Night | 100.07% | 100.05% | 99.99% | +0.02% | -0.08% | 53.17 |
| Silent Hill garage | 84.11% | 83.87% | 84.01% | -0.22% | -0.12% | 15.17 |
| Silent Hill alley | 81.30% | 80.59% | 81.21% | -0.82% | -0.11% | 15.17 |
| Silent Hill café | 72.96% | 72.72% | 72.98% | -0.34% | +0.03% | 15.17 |
| Gran Turismo 2 | 52.24% | 52.21% | 52.10% | +0.07% | -0.26% | 12.24 |
| Tekken 3 | 90.78% | 90.84% | 90.83% | +0.04% | +0.06% | 15.08 |
| Final Fantasy VII | 100.00% | 99.98% | 100.10% | -0.07% | +0.10% | 15.17 |
| Klonoa - Door to Phantomile | 99.32% | 99.79% | 99.54% | +0.37% | +0.21% | 15.08 |
| Crash Bandicoot 2 - Cortex Strikes Back | 91.00% | 90.06% | 90.85% | -0.96% | -0.17% | 15.92 |
| Need for Speed - High Stakes | 37.68% | 37.63% | 37.68% | -0.14% | +0.00% | 10.98 |

Worst paired change: -0.96%; best: +0.40%; mean: -0.11%. Maximum absolute A2/A1 drift: 0.44%.

## Auto floor curves (speed% / estimated pictures/s)

| Scene | 10 | 12 | 15 (five A runs) | 20 | 24 | Floor-15 speed range |
|---|---:|---:|---:|---:|---:|---:|
| Crash Bandicoot | 97.9% / 14.7 | 97.9% / 14.7 | 97.3% / 14.5 | 91.2% / 17.4 | 85.7% / 19.7 | 97.1–97.5% |
| Crash Team Racing | 97.5% / 10.3 | 94.5% / 12.1 | 89.2% / 15.2 | 81.0% / 20.3 | 76.8% / 22.8 | 89.0–89.3% |
| Ridge Racer Type 4 | 92.6% / 14.9 | 90.8% / 15.9 | 88.1% / 17.4 | 82.3% / 20.6 | 80.1% / 22.8 | 87.9–88.3% |
| Castlevania - Symphony of the Night | 100.0% / 53.1 | 100.0% / 52.9 | 100.0% / 53.1 | 100.0% / 53.0 | 100.0% / 52.7 | 100.0–100.0% |
| Silent Hill garage | 91.4% / 10.1 | 88.3% / 12.1 | 84.3% / 15.2 | 78.9% / 19.1 | 78.7% / 19.1 | 84.1–84.6% |
| Silent Hill alley | 93.7% / 10.1 | 88.7% / 12.1 | 81.3% / 15.1 | 70.0% / 20.3 | 68.9% / 21.0 | 81.2–81.4% |
| Silent Hill café | 86.7% / 10.1 | 81.1% / 12.2 | 72.9% / 15.2 | 69.4% / 16.4 | 69.6% / 16.4 | 72.9–72.9% |
| Gran Turismo 2 | 55.4% / 10.2 | 53.0% / 11.3 | 52.2% / 12.2 | 51.2% / 13.6 | 51.3% / 13.6 | 52.1–52.2% |
| Tekken 3 | 95.7% / 11.7 | 95.4% / 12.2 | 91.2% / 15.2 | 84.3% / 20.3 | 79.2% / 24.3 | 90.9–91.8% |
| Final Fantasy VII | 100.1% / 15.0 | 100.0% / 15.2 | 100.0% / 15.1 | 100.0% / 15.2 | 100.0% / 15.2 | 100.0–100.1% |
| Klonoa - Door to Phantomile | 99.9% / 12.2 | 99.9% / 13.2 | 99.6% / 15.1 | 97.2% / 20.3 | 93.2% / 24.3 | 99.4–99.7% |
| Crash Bandicoot 2 - Cortex Strikes Back | 98.0% / 13.4 | 96.0% / 14.1 | 90.4% / 16.0 | 76.9% / 20.0 | 71.9% / 21.7 | 89.6–91.0% |
| Need for Speed - High Stakes | 38.7% / 7.9 | 38.4% / 9.5 | 37.7% / 11.0 | 37.0% / 11.8 | 37.0% / 11.7 | 37.5–37.9% |

Each non-15 result has adjacent independent floor-15 A loads before and after it. These are single B samples, not confidence intervals. See analysis.json for paired percentage-point/picture changes.

## Proposed per-game floors (owner picks; not shipped)

| Game | Proposed floor | Reason |
|---|---:|---|
| Crash Bandicoot | 15 | Lower floors gain less than one speed point; higher floors lose substantial speed. |
| CTR | 15 | Floor 12 adds about 5 speed points but loses about 3 pictures/s in a racer. |
| R4 | 15 | Floor 12 adds under 3 speed points and loses about 1.5 pictures/s. |
| SotN | 15 | All tested floors already give full speed and about 53 pictures/s. |
| Silent Hill (all three scenes) | 12 | Gains about 4–8 speed points at about 12 pictures/s versus about 15; compromise for owner judgment. |
| GT2 | 15 | Floor 12 adds only 0.8 speed points and reduces pictures/s; floor 10 remains very choppy. |
| Tekken 3 | 15 | Prefer about 15 pictures/s to floor 12’s roughly 4-point speed gain in a fighting game. |
| FFVII | 15 | Full speed and about 15 pictures/s across all tested floors; no higher-floor benefit. |
| Klonoa | 20 | About 5 more pictures/s for about 2.4 speed points (97.2%); worthwhile smoothness option. |
| Crash 2 | 12 | About 5.6 speed points for about 1.9 fewer pictures/s; lower floor 10 gives less additional gain. |
| NFS | 15 | Floor 12 adds less than one speed point and loses about 1.5 pictures/s. |

These are proposals from the supplied heavy scenes, not whole-game compatibility or play-feel validation. Every shipped file continues to inherit 15. No floor-10 or floor-24 change is proposed.

## Closing gate and console handoff

The real Old final A/B/A against core-first-2 passes the approximately 1% gate
in all 13 scenes for both Off and Auto. Worst paired change is −0.96% in each;
mean is −0.13% Off / −0.11% Auto. Maximum absolute A2/A1 drift is 0.47% / 0.44%.
Crash 2 is the closest scene to the gate. Core/library code addresses are fixed;
frontend layout still explains why this hardware check is required.

All 195 runs passed their effective-settings/status checks. At 09:29:53 UTC the
Old was handed back on exact `release-v041-rc4` (CIA `00d894f6…`, session
`e76d072256e`), library idle, bench inactive. A live Settings reset and screenshots
confirm smoothing On / FPS Off / PS1 colour shading On. Written config is checked,
then paths-only config and empty game overrides are restored. The driver and
screenshot helper exited successfully and released the lock. No protected Old 2
or New 0 slot was written.

New received no install, game load, settings/config write or other mutation.
Initial read-only status was `gpu-stereo3-4`; the last available read-only check
at 05:40 UTC saw `gpu-stereo3-7` after another worker's accepted update. Its config
remained byte-identical (SHA256 `9438ed68b4e8fce21996fa2b32a8bca923445181b6daf88d50d3f774d18cc340`).
At finalization its lock was busy, so no contact was attempted and that worker's
session/build was left alone.

`input-manifest.json` hashes all CIAs, batches and source states;
`run-manifest.json` records the 195 corrected runs and hashes 311 evidence files.
The earlier manifest/measurements are archived with `invalid7` names and excluded
from all tables and proposals. Raw evidence is under `hw/old-defaults/`.

## Remote UI evidence

`shot both` captures from candidate 7, after live reset (same UI/source as 8):
`ui-09-global-advanced-on.png` shows the global toggle/help;
`ui-16-ctr-preset-screen.png` shows P plus source/confidence;
`ui-17-ctr-screen-override.png` and `ui-18-ctr-widescreen-override.png` show game
overrides; `ui-19-ctr-inherit-restored.png` shows X restore;
`ui-21-game-advanced-off.png` shows the global switch from in-game settings.
Only the first performance sweep is invalid, not these post-reset UI captures.
Candidate 8 adds verified post-install defaults screenshots. No New 3DS category
was added and no rows were moved; README has the requested feature section.
