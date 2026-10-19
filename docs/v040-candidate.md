# v0.4.0 release candidate acceptance

**Status: BLOCKED — offline and candidate checks pass; RC2 restoration failed on both consoles, so Old A2 and final installed-title verification are incomplete.** Nothing pushed,
tagged or published. Worktree `/home/tony/.t3/worktrees/PS1_3ds/v040`, branch
`release/v0.4.0-candidate`, based on combined2 `67c67d2` (frozen executable
`047dded`). Evidence: `/home/tony/Development/PS1_3ds/devtools/v040/`.

## Candidate contents

The GPU-only combined2 base retains the scratchpad fast path, indirect lookup,
NCLIP, G1 movie synchronization, shared alternating-buffer border fix and opt-in
developer recorder. The rejected nearest-SPU experiment is removed from startup,
remote parsing, status, metadata and interpolation override code. Normal Old
simple/New gaussian interpolation defaults remain.

Settings `31a21ee` and covers `5a9c580`, `ca90e78`, `fdc670f` plus report
`d8188a6` are integrated. Merge conflicts preserve the recorder pause entry,
clip-safe command gate and both cover/settings remote commands. Renderer and
half-height Software rows are deleted. Legacy Software configuration stays
ignored. No changes to `core/` versus combined2.

Centering was checked before hiding its page: gpulib computes manual
`screen.x/y` and `hres/vres`, while `pica_picture()` selects `src_x/src_y` and
`screen.w/h`, and the frontend scales that picture directly. The GPU gameplay
path does not apply manual offsets; exposing the controls would promise a
feature it does not implement. RGB24 movies use the shared CPU scanout path;
there is no general GPU picture-centering setting in this candidate.

No unfinished widescreen, GPU-stereo or HLE-compat branches are included. HLE is
the normal BIOS configuration. Real per-object 3D falls back to depth-mesh screen
3D until GPU stereo lands. README remains explicit about experimental GPU
rendering, WIP status and AI assistance. QR URL and image are unchanged.

## Frozen executable and footprint

Version **0.4.0**, title counter **4099**, identity **release-v040-rc1-split**.
Frozen executable source **a4d0c60**, split layout, devkitARM GCC 16.1.
CIA/3DSX/ELF are immutable in `artifacts/`, with `SHA256SUMS` and `manifest.json`.

| Section | RC2 | combined2 | Candidate | vs RC2 | vs combined2 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `.text` bytes | 2,146,052 | 1,801,964 | 1,804,468 | −341,584 (−15.92%) | +2,504 (+0.14%) |

| Artifact | SHA256 |
| --- | --- |
| PSX3D.cia | `7c7464fbd055aa0d0d431668bf40b6b8475321d0cb98ccd0491ed1d04009071a` |
| PSX3D.3dsx | `4e555e5ac98bd9870ea27277081bbb7b9f72d06ac179632941e5ad681067f7d3` |
| PSX3D.elf | `1c86f516e26c6e2416f33aacff3b5f71ecd6d743ae07870c8aff02304443adba` |

## Offline gates

| Gate | Verdict | Evidence |
| --- | --- | --- |
| CIA + 3DSX build | PASS | `build-frozen.log`, counter 4099 |
| Frontend host controls/config/presets/reset/UI | PASS | `host.log`, `host-pictures/` |
| Developer control/transport/updater ASan/UBSan | PASS | `dev.log` |
| Categorized settings inventory/drawing/scope/reset + rewind safety | PASS | `settings.log`, `settings-pictures/` |
| GPU-only config migration | PASS | `config.log` |
| Cover lifecycle/PNG decoder ASan/UBSan | PASS | `covers.log` |
| Recorder codec/input/checkpoint/corruption sanitizers | PASS | `codec.log` |
| Six gameplay states × 600 frames vs combined2 | PASS | `states-identity.json` |
| 31 real-BIOS cold boots × 1,200 frames vs combined2 | PASS | `boots-identity.json` |
| 31 HLE cold boots × 1,200 frames vs combined2 | PASS | `hle-identity.json` |
| Recorder capture + two simulator replays | PASS | `replay/results.json`, `replay/` |

Every six-state comparison checks 600 per-frame draw hashes, ten guest
RAM/VRAM/GPR/CP2/PC/cycle checkpoints and all 4,456,448 serialized bytes.
Boots check the same complete timelines and final serialization, with twenty
checkpoints per game. HLE boots explicitly select HLE and use isolated empty
BIOS directories; real-BIOS logs identify the reference BIOS. These are identity
gates against combined2, not claims of universal game compatibility.

The shared ROM directory grew from 31 to 47 games during other work. All 47
candidate real-BIOS and HLE runs completed. `boot-inventory.json` lists the 16
additional games without a combined2 oracle; only the exact original 31-game
subset is counted as an identity PASS. Original complete logs/results are
retained, with reference subsets in `boots/reference31` and `hle/reference31`.
The initial comparator correctly rejected unequal game sets; no emulation
mismatch was suppressed. Shared simulator sources/core copies are untouched.

The scripted recorder captured 600 frames/10 checkpoints and passed two replays
with zero divergence. Console-to-simulator replay portability is not promised;
the known archived console clip is tested on its originating console separately.

## Hardware procedure and current state

Each session starts only under its exclusive `lock.old` / `lock.new`, with no
UNAVAILABLE marker, build `release-v031-rc2-split`, library mode and inactive
benchmark. `tools/v040/session.py` retains the lock across actions, never sends
HOME/reboot/power/FTP/InputRedirection, and never retries an uncertain install.
Install requests allow 600 seconds. Token values are never logged.

Old: RC2 A1 → candidate six-game off / six-game auto + pictures, covers, recorder
and spare-slot-9 save/load → RC2 A2. If all acceptance gates pass, reinstall the
candidate as the new baseline. Otherwise leave RC2. New: GPU six games off and
auto, pictures/covers/settings and spare-slot 9. Old slot 2, New slot 0 and SotN
slot 8 remain load-only.

`batches/` contains exact requests; raw histories and results live in
`hardware/{old,new}/acceptance-1/`. The first Old A1 batch was rejected before
loading because RC2 does not recognize `record=off`; corrected `A1valid` and A2
omit that candidate-only switch. The rejected batch is retained and excluded
from measurements.


## Candidate hardware measurements

Measurements taken 2026-10-19 UTC (2026-10-18 local). GPU, clock 57,
PMU 0/2, 10-second warmup and 15 one-second samples per ordinary run.
Old slot 2 / New slot 0 were only read. NTR was not explicitly unloaded;
retained observer snapshots report hours without packets. These are specific
saved scenes, not whole-game speed or compatibility claims.

| Game | Old off FPS | Old auto FPS / speed | New off FPS | New auto FPS / speed |
| --- | ---: | ---: | ---: | ---: |
| Crash Bandicoot | 52.998 | 60.029 / 100.05% | 59.995 | 59.991 / 99.98% |
| CTR | 37.357 | 49.924 / 83.21% | 59.979 | 59.983 / 99.97% |
| Gran Turismo 2 | 34.440 | 47.403 / 79.00% | 59.997 | 60.007 / 100.01% |
| Silent Hill | 42.048 | 60.000 / 100.00% | 60.003 | 59.988 / 99.98% |
| Ridge Racer Type 4 | 60.000 | 59.995 / 99.99% | 59.999 | 60.013 / 100.02% |
| Symphony of the Night | 57.794 | 60.001 / 100.00% | 60.000 | 60.000 / 100.00% |

New performance gate: **PASS**, all twelve runs 59.98–60.01 FPS. All 24
candidate ordinary runs have zero near-black one-second probes. All off/auto
gameplay screenshots were reviewed; they are coherent, with existing GPU
limitations. These sparse captures do not establish absence of transient flicker.
Crash's purple Wumpa idle animation in a capture is known authored behavior.

## Old comparison: provisional, bracket incomplete

| Game | RC2 A1 FPS | combined2 FPS | Candidate FPS | Raw vs combined2 | RC2 A2 |
| --- | ---: | ---: | ---: | ---: | --- |
| Crash Bandicoot | 50.520 | 51.188 | 52.998 | +3.54% | NOT RUN |
| CTR | 32.134 | 36.455 | 37.357 | +2.47% | NOT RUN |
| Gran Turismo 2 | 32.188 | 33.907 | 34.440 | +1.57% | NOT RUN |
| Silent Hill | 39.119 | 41.946 | 42.048 | +0.24% | NOT RUN |

The raw comparison meets the no-more-than-1% regression threshold in all four
scenes, but **the required RC2 A1 → candidate → RC2 A2 gate is NOT PASSED**.
A2 never ran because the attempted exact RC2 restore failed. Without A2, bracket
drift and RC2-normalized gains cannot be calculated. `hardware-verdict.json`
records this as incomplete, never a successful final gate.

## Behavior, UI, saves and recorder

| Check | Old | New | Evidence |
| --- | --- | --- | --- |
| Library box art in remote mode | PASS | PASS | `covers-crash-library.png`, New `candidate-idle-reviewed-screens.png` |
| Fresh HTTPS cover fetch | PASS | PASS | `covers-crash-complete.json`: 1 attempt, 1 download, curl 0, HTTP 200, no failures |
| Settings home/categories and scope | PASS | PASS | `settings-global-*`, `settings-game-home.png` |
| Reset confirmation, canceled | PASS (game) | PASS (game + global) | `settings-*-reset-confirm.png`; no settings reset |
| Spare slot 9 save/load | PASS | PASS | `save9-write/load.json`, browser and resumed-game PNGs |
| Recorder determinism | PASS, archived clip1 twice | PASS, new 120-frame capture + twice | `recorder-replays/*/results.tsv`, New `recorder-complete.json` |

Old replay: 900 frames, 15 checkpoints, zero divergence in each of two runs.
New capture/replays: 120 frames, 2 checkpoints, zero divergence in both replays.
No gameplay input was injected. Protected Old 2, New 0 and SotN 8 were never
written. Save/load used Crash slot 9. No global/per-game reset was confirmed.
New `default-gpu-crash.json` loaded without a renderer parameter successfully;
GPU is the only linked path. Settings/covers navigation screenshots were viewed
before each dependent key. Commands acknowledge before rendering: early stale
captures are retained; `*-settled` and subsequent reviewed captures supersede them.

## Installation failure and exact handoff

Both forward candidate installs succeeded, and their replies validate the exact
CIA SHA below. The Old A2 transition and subsequent New fallback restore
each returned:

> `CIA install read/write failed; use FBI recovery`

The error arises in the AM install read/write loop, after download/hash/title
validation. The updater cancels the failed install. The reply does not include a
low-level Result code, so the cause and final installed-title integrity are
**unknown**. Do not assume a downgrade/counter issue or a Wi-Fi fault. No retry,
HOME/reboot/power/FTP/InputRedirection or post-marker contact was performed.
Both session drivers exited and released their locks, with markers retained.

| Console | Last verified running identity | Session | Last verified mode | Final installed title |
| --- | --- | --- | --- | --- |
| Old | `release-v040-rc1-split` / 0.4.0 | `b4f575cc26` | idle library, no benchmark | UNVERIFIED after failed RC2 write |
| New | `release-v040-rc1-split` / 0.4.0 | `bd7d47c300` | idle library, no benchmark | UNVERIFIED after failed RC2 write |

Last successfully installed candidate CIA SHA256:
`7c7464fbd055aa0d0d431668bf40b6b8475321d0cb98ccd0491ed1d04009071a`.
Attempted exact RC2 CIA SHA256:
`5fa3d650a11a8f2b0c9d247e3d0c1fe1c51af7818a4fef07c2befcec57b07660`.

Evidence: Old `A2-restore-before.json`, `A2-restore-before-screens.png`,
`STOP-1792386048.json`; New `acceptance-blocked-restore-before.json`, its
screens PNG and STOP JSON; both `requests.jsonl`. Failed update replies are
captured in STOP errors because the client raises on an error response.

### Baseline handoff notes for future workers

- **NOT a new accepted hardware baseline.** Do not treat candidate results as
  completion of the required Old bracket.
- `devtools/fastloop/OLD_UNAVAILABLE` and `NEW_UNAVAILABLE` exist. No console
  contact until the maintainer recovers the titles and explicitly clears
  the markers. No automatic reinstall attempts.
- Both locks are released. Last running app and SHA are listed above; final
  installed title cannot be asserted after the failed writes.
- maintainer recovery must establish a working exact RC2 (FBI recovery is
  the updater's stated route), verify remote-idle library, then finish the Old
  bracket and final installation decision under exclusive locks. Do not alter
  protected save slots.
- Candidate artifacts are frozen at executable source `a4d0c60`; later commits
  only add tooling/docs/screenshots. Keep these binaries for any continuation.

## Deliverables and remaining work

`devtools/v040/artifacts/` contains read-only CIA/3DSX/ELF, manifest and SHA256SUMS.
`devtools/v040/index.html` is the real-console screenshot gallery, with hashed
`screenshots-manifest.json`. Evidence and batch files stay outside tracked source.
README now uses measured candidate GPU numbers, updated real settings/library
captures, HLE guidance and explicit real-3D fallback. Draft release notes are
`docs/release-notes-v0.4.0.md`; QR is unchanged. No public action occurred.

Remaining required work: console recovery, RC2 A2, bracket verdict, final candidate
or RC2 install decision and verified final remote-idle handoff. This candidate is
**not release-approved** while those remain incomplete.

## Local integration commits

- `a4d0c60`: frozen executable, approved settings/covers integration cleanup,
  nearest-SPU removal, GPU-only menu and 0.4.0 identity/counter.
- `7766c78`: exclusive interactive hardware acceptance driver.
- `951f077`: initial release draft and GPU-only user guidance.
- This report commit: final measured README, real captures, release draft and
  explicit blocked acceptance/recovery handoff. Earlier settings/cover
  cherry-picks remain separate commits on the candidate branch.
