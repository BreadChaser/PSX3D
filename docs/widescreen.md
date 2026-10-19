# Curated widescreen bundles — v0.4.1 work

Screen → Widescreen owns the game's complete curated bundle: projection,
culling/frustum, LOD and HUD companions together. No individual codes need
user management. This branch is based on the GPU-only v0.4.0 release branch
`6b811d3`. The library teardown and HLE memory-card fixes are included; the final
frameskip release work is still pending integration. **24 disc/EXE identities are runtime-enabled**;
Gran Turismo 2 Arcade and Simulation count separately. This is scene-limited
coverage, not complete support for the user's 94-title collection.
The audit covers 225 unique identities in the local and read-only NAS libraries.
Uncurated games retain the GTE fallback; that fallback is not native widescreen
coverage and can still cull the edges.

## Matching, toggle and console policy

Files in `romfs:/widescreen/SERIAL-FNV.ws` match both normalized disc serial
and FNV-1a over the on-disc PS-X EXE header plus declared text. A different
revision never borrows codes by title or description. The serial collision
between the inventory's Urban Chaos and Threads of Fate is disambiguated by
the boot fingerprint. Staged `tools/widescreen/research/` files are never
loaded by the frontend.

Application is atomic. Unsupported, malformed, unaligned or out-of-RAM codes
reject the complete file. A7 compares known originals; A4 guards overlay
blocks; D0 guards its single following write. Leaving Widescreen restores
still-owned changes, preserves subsequent overlay/game writes, and invalidates
the dynarec blocks affected. Patched states also clean up when loaded with the
toggle off. Guards constrain restoration as well as application. The ordinary
cheat list and its saved selections do not own or expose the bundle.
See [the format reference](../tools/widescreen/README.md).

Native bundles fit 16:9 into 400×225; GTE fallback retains 5:3.
`models=Old,New` combines with per-console presets to hide unsupported modes. Per console: 0 hidden, 1 offered, 2 offered with a speed warning, 3 held back to keep the speed up (Settings explains it).
CTR and Silent Hill remain New-only after Old cost failures. Crash 1, Spyro 1,
Musashi, Digimon World and NFS High Stakes are explicitly hidden. The catalog
also records 32 hidden identities for 2D-only presentations; Stretch remains
available. Unassessed games expose the existing fallback.

Old cost policy: hide a bundle if matched frameskip-off fps loses **>5%**,
or automatic frameskip loses previously full game speed. Representative cost
tests are not individual acceptance for other games; their table cells say
unmeasured. Inactive-path integration has the usual approximately 1% gate.
Development-only `wsbundle=on|off` and `wsscreen=4:3|stretch|wide` allow remote
comparison without saving user settings.

## Sources and credits

Major sources are credited one line each in
[tools/art/CREDITS.md](../tools/art/CREDITS.md). The user explicitly chose to
ship third-party patch data; per-code permission/credit is not a release gate.
The data is not relabelled MIT or CC BY-SA merely because a collection's
processing scripts use that licence.

- DuckStation chtdb, `8b2f1dd257ab9130d16b72e0caeb5a07af8bcbdc`: primary
  projection/culling/HUD search; patch rights remain with original authors,
  and its processing scripts are MIT. Exact source sections remain in data.
- libretro-database, `12c21cf79f1e20eff6ac68b80875816c717d0a7a`:
  adapted data retains **CC BY-SA 4.0** attribution and the full bundled licence.
- CTR-ModSDK (GPL-3.0), REDRIVER2 (MIT), GT1 OverlayCheat (GPL-3.0), Crash
  decomp and PeterDelta releases were inspected for companion repairs and
  matched retail revisions. No project source code was copied into runtime.
- GameHacking.org direct requests returned 403; no codes were imported from
  that site. Community PS1 releases in PeterDelta/PCSX2 and the
  gamemasterplc archive did not yield additional matching USA priority codes.

The third-round scan added thirteen exact-revision candidates from the wider
library. Both raw source notes and negative technical findings are preserved.
No ROM bytes or library paths are committed.

## Simulator evidence

Primary checks use HLE cold boots and our own resulting simulator states,
scripted movement, paired 3600-frame runs, active-write diagnostics and
rollback comparisons. Pictures replay captured commands through actual
`gpu_pica`, rather than judging the software renderer's reference output.
The private simulator is in `devtools/widescreen/round3/sim/` and builds from
this worktree. XA/CD audio and CD/SPU worker threads were disabled in simulator
runs; these results do not certify audio, thread stress or whole campaigns.
The paired gameplay pictures precede the inherited HLE card correction; no
bundle or renderer algorithm changed afterward. The final runner was rebuilt
with that correction for a separate 24-title cold-boot rerun and the six-state
full-VRAM/output gate. This does not imply a repeated full gameplay campaign.

Crash 2 reached Turtle Woods and traversed multiple jungle sections. Warped
reached Orient Express and exercised riding/jumps. Ape Escape used the actual
DualShock subclass (the first probe incorrectly selected analog joystick)
and exercised training movement/club actions. MGS reached the dock; codec and
disc 2 remain unverified. Spyro 3 traversed the opening outdoor area. Soul
Reaver traversed the opening cave, and Spider-Man reached rooftop gameplay.
Klonoa traversed the first village, Vagrant Story its opening rooms, Toy Story
2 the house, Mega Man Legends its post-tutorial corridor, and MediEvil II its
crypt rooms. Syphon Filter 1/2/3 reached the Washington street, Colorado
mountain and Tokyo hotel missions; paired movement, firing and crouching
retain filled expanded edges and visible armor/radar/ammunition. Each has
two active projection writes, with both originals restored. Syphon Filter 2
acceptance covers disc 1 only. Their paired runs retain filled edges and visible HUD elements.
Spyro 2 now folds **all ten source LOD bypasses** into its 30-operation bundle;
all ten originals match its exact EXE. No L3 toggle is exposed.

Musashi completes the scripted forest run but loses large floor triangles in
actual GPU output. Digimon World reaches its village, but its fixed background
does not widen with the actors. Both are rejected. Croc loses overhead/HUD
sprites, Croc 2 does not match its active hook, and Tenchu 2's write remains
inactive in the tested scene. They stay staged. Crash 1's incomplete visibility
and Spyro 1's crash warning remain rejection reasons.

| Third-round case | Paired frames per mode | Active changed halfwords restored | Outcome |
|---|---:|---:|---|
| ape | 3600 | 1 | Run/rollback pass; picture verdict in table |
| crash2 | 3600 | 5 | Run/rollback pass; picture verdict in table |
| crash3 | 3600 | 5 | Run/rollback pass; picture verdict in table |
| croc | 3600 | 24 | Run/rollback pass; picture verdict in table |
| croc2 | 3600 | 6 | Run/rollback pass; picture verdict in table |
| digimon | 3600 | 1 | Run/rollback pass; picture verdict in table |
| klonoa | 3600 | 1 | Run/rollback pass; picture verdict in table |
| medievil2-ghost | 3600 | 1 | Run/rollback pass; picture verdict in table |
| medievil2 | 3600 | 1 | Run/rollback pass; picture verdict in table |
| mgs | 3600 | 27 | Run/rollback pass; picture verdict in table |
| mml-tutorial | 3600 | 28 | Run/rollback pass; picture verdict in table |
| mml | 3600 | 28 | Run/rollback pass; picture verdict in table |
| moh2-boot-originals | 3600 | 24 | Run/rollback pass; picture verdict in table |
| moh2 | 3600 | 31 | Run/rollback pass; picture verdict in table |
| musashi | 3600 | 1 | Run/rollback pass; picture verdict in table |
| soulreaver | 3600 | 4 | Run/rollback pass; picture verdict in table |
| spiderman | 3600 | 2 | Run/rollback pass; picture verdict in table |
| spyro2 | 3600 | 28 | Run/rollback pass; picture verdict in table |
| spyro3 | 3600 | 18 | Run/rollback pass; picture verdict in table |
| syphon | 3600 | 2 | Run/rollback pass; picture verdict in table |
| syphon2 | 3600 | 2 | Run/rollback pass; picture verdict in table |
| syphon3 | 3600 | 2 | Run/rollback pass; picture verdict in table |
| tenchu-boot-originals | 3600 | 0 | Run/rollback pass; picture verdict in table |
| tenchu | 3600 | 1 | Run/rollback pass; picture verdict in table |
| tenchu2 | 3600 | 0 | Run/rollback pass; picture verdict in table |
| toy2 | 3600 | 2 | Run/rollback pass; picture verdict in table |
| vagrant | 3600 | 2 | Run/rollback pass; picture verdict in table |

A successful run/rollback is necessary but does not override a failed picture.

Inactive bundle path: all six 600-frame command streams with every-frame
full-VRAM snapshots are byte-identical to the v0.4.0 baseline. Actual PICA
replay completes for both sides. The baseline capture runner was built from
`cb34685`; merged v0.4.0 core changes after it are comments/labels, not renderer
behavior. Evidence: `round3/gate-v040-summary.json`.

| State | Bytes compared | Equality |
|---|---:|---|
| o3auto | 649,884,088 | PASS |
| sh | 641,480,464 | PASS |
| r4 | 635,887,008 | PASS |
| ctr | 642,276,508 | PASS |
| gt2 | 644,419,400 | PASS |
| sotn | 634,262,260 | PASS |

Tenchu and Medal of Honor Underground initially failed because boot-only
originals were zero while their gameplay values load later. Tenchu now checks
the measured live 0x1000 original; Underground checks all live originals and
an unchanged adjacent main-game instruction guards its entire block. Its
weapon is restored, all 31 changed halfwords roll back, and both paired
3600-frame gameplay runs pass. The original failed captures remain separate.

All 24 enabled identities completed a fresh 6000-frame HLE cold boot with
the complete bundle on (exit 0 for each). The scripted startup ends in gameplay, intro or menu
depending on title; it is combined with the deeper paired gameplay runs, not
misrepresented as 24 completed first levels. The original checks are in `cold-wide*.log`; the final inherited-HLE rerun
is recorded separately in `cold-final10.log`.

Optional GT2 distance extensions request 325% emulated CPU overclock and benefit
from 8MB polygon buffers; its full AI LOD patch requires 8MB RAM. These are
not accepted companions for the shipping 2MB core. Native race/mirror frustum
and menu/HUD repairs are already included. HUD-hide toggles in other source
files remove interface elements and are not included as HUD repairs.

Runtime parser, atomic validation, overlay guards, patched-state rollback and
console-limit tests pass under ASan/UBSan. The dev-control transport/update
suite and settings visibility/navigation tests pass. Logs are in `round3/`.

## Hardware and artifacts

All contact holds `lock.new` / `lock.old` for the entire session, checks markers
before contact and under lock, then requires an idle recognized baseline.
No HOME, exit, FTP, InputRedirection, gameplay inputs or console save writes
are used. New self-updates allow 600 seconds and one candidate attempt.
The exact entry baseline is restored, followed by `shot both` and an idle
library status. Counter stays **4099**, never RC2's downgrade counter.
All three completed New sessions entered on `release-v040-final`, CIA SHA256
`cba906a2c4cf8568ae71d80c3824dd2dd3030388b1d15a9a0d2e40606582ccdf`,
and restored that exact artifact. During testing it moved from
`devtools/v040-final/artifacts/` to `devtools/v040-final/rejected-1/artifacts/`;
a private identical copy is retained as `round3/baseline-final.cia`.
Future sessions must re-read the baseline manifest and common policy; the
release build is replacing this build. Do not infer an exact CIA from a
reused build identity. rc1 is accepted only when it was the entry baseline.

Frozen hardware candidate: `577f0f6880d0e16ca2b032356eee2ceaed64aba2`, identity `widescreen-10-split`,
version 0.4.0, counter 4099. CIA SHA256
`8f4aacb101711fa1cba4d41ea411c2eb00cca6fee683b69fcdeb02d92ccb86fb`.
Immutable CIA/3DSX/ELF/manifest: `devtools/widescreen/round3/artifacts-10/`.
Final package hardware checks are recorded in their own build-10 table below. Its executable includes the inherited release library teardown and HLE card fixes, so the earlier build 9 `.text` equality to New-tested build 7 is historical evidence only. Build 9 and its manifest remain preserved. Fresh final-core simulator checks are recorded below. Individual new-game hardware gameplay and Old executable/cost acceptance remain open. Historical build 7/8 ELF containers were rewritten during objcopy section extraction; their CIA/3DSX hashes still match. Separate `PROVENANCE-CORRECTION.json` files record the actual ELF hashes without changing their original manifests. All three build 10 artifact hashes are verified.

### New 3DS · widescreen-5

26 rows recorded; 26 status-ok; 26 near-black one-second probes.

| Game | Mode | 4:3 fps | Wide fps | Delta |
|---|---|---:|---:|---:|
| Crash_Team_Racing | matched state | 60.04 | 60.06 | +0.03% |
| Silent_Hill | matched state | 59.98 | 59.97 | -0.01% |
| R4 | matched state | 60.00 | 59.99 | -0.01% |
| Gran_Turismo_2_(Arcade_Mode) | matched state | 60.01 | 60.01 | +0.01% |
| Crash_Bandicoot_2 | HLE cold boot / menu; not gameplay | 59.98 | 59.98 | +0.00% |
| Crash_Bandicoot_-_Warped | HLE cold boot / menu; not gameplay | 60.03 | 60.05 | +0.03% |
| Ape_Escape | HLE cold boot / menu; not gameplay | 60.28 | 60.32 | +0.07% |
| Spyro_2 | HLE cold boot / menu; not gameplay | 59.80 | 59.82 | +0.03% |
| Spyro_-_Year_of_the_Dragon | HLE cold boot / menu; not gameplay | 59.93 | 59.99 | +0.09% |
| Metal_Gear_Solid | HLE cold boot / menu; not gameplay | 59.99 | 60.01 | +0.02% |
| Legacy_of_Kain_-_Soul_Reaver | HLE cold boot / menu; not gameplay | 60.19 | 60.19 | +0.00% |
| Spider-Man | HLE cold boot / menu; not gameplay | 59.99 | 59.97 | -0.03% |
| Medal_of_Honor | HLE cold boot / menu; not gameplay | 59.78 | 60.23 | +0.74% |

The normal restore stopped before sending an install request because the
baseline CIA had moved to `v040-final/rejected-1/artifacts/`. A separate
locked recovery verified its exact entry SHA256 and restored it successfully.
Recovery status/picture are in `hardware-new-recovery/`; no uncertain install
was retried. A private baseline copy is now retained for subsequent sessions.

Final status: `release-v040-final`, `library`, bench active `false`. `hardware-new-recovery/final-both.png` records handoff.

Cold-boot menu/intro pictures verify boot and presentation only; they are not
edge-geometry gameplay acceptance or meaningful bundle-cost measurements.

### New 3DS · widescreen-7

18 rows recorded; 18 status-ok; 30 near-black one-second probes.

| Game | Mode | 4:3 fps | Wide fps | Delta |
|---|---|---:|---:|---:|
| Crash_Team_Racing | matched state | 60.04 | 60.06 | +0.03% |
| Silent_Hill | matched state | 60.00 | 60.00 | +0.00% |
| R4 | matched state | 60.03 | 60.00 | -0.05% |
| Gran_Turismo_2_(Arcade_Mode) | matched state | 60.00 | 60.01 | +0.02% |
| Klonoa | HLE cold boot / menu; not gameplay | 59.83 | 60.24 | +0.70% |
| Vagrant_Story | HLE cold boot / menu; not gameplay | 60.01 | 60.00 | -0.01% |
| Toy_Story_2 | HLE cold boot / menu; not gameplay | 60.00 | 60.00 | +0.00% |
| Mega_Man_Legends | HLE cold boot / menu; not gameplay | 57.55 | 57.55 | +0.00% |
| MediEvil_II | HLE cold boot / menu; not gameplay | 59.97 | 60.21 | +0.40% |

Final status: `release-v040-final`, `library`, bench active `false`. `hardware-new-07/final-both.png` records handoff.

Cold-boot menu/intro pictures verify boot and presentation only; they are not
edge-geometry gameplay acceptance or meaningful bundle-cost measurements.

### New 3DS · widescreen-10

8 rows recorded; 8 status-ok; 0 near-black one-second probes.

| Game | Mode | 4:3 fps | Wide fps | Delta |
|---|---|---:|---:|---:|
| Crash_Team_Racing | matched state | 60.04 | 60.06 | +0.03% |
| Silent_Hill | matched state | 59.98 | 59.97 | -0.01% |
| R4 | matched state | 60.03 | 60.00 | -0.05% |
| Gran_Turismo_2_(Arcade_Mode) | matched state | 60.00 | 60.02 | +0.02% |

Final status: `release-v040-final`, `library`, bench active `false`. `hardware-new-10/final-both.png` records handoff.

Cold-boot menu/intro pictures verify boot and presentation only; they are not
edge-geometry gameplay acceptance or meaningful bundle-cost measurements.

### Old 3DS · widescreen-10

Deferred by the coordinator’s Old priority queue. The waiting driver was
canceled before lock acquisition or console contact. No Round 3 Old run,
final-code cost acceptance, or inactive-path hardware acceptance is claimed.
Resume only after an explicit “Old queue released” policy update.

Historical matched Old gameplay: CTR 37.37→33.38 fps (−10.68%), Silent Hill
41.86→38.69 (−7.58%); both hidden on Old. R4 ~60 and GT2 Arcade −3.42% were
within the threshold in Round 2. Round 3 measurements above supersede them
where present. No simulator cycle count is represented as hardware fps. The TSV `fps` metric
counts emulated VBlank/host frames, not necessarily distinct game pictures:
a PS1 title rendering 30 pictures/s can still report 60 at full emulation
speed. The new FPS overlay displays picture rate separately from game speed.
The final New build-10 GPU street pair shows no added Silent Hill border junk;
this sparse check does not certify every scene. Silent Hill's separate junk-row repair is separate work; no such
fix is implemented here. Earlier and current GPU street pictures should be
judged separately from the retired Software path.

## Enabled bundles and per-disc table

All files declare 16:9. Links contain every code/original/guard used.
“Rows” counts guards and terminators as well as writes. A native projection
can need no separate bypass in a tested scene; that does not certify all
levels. Unmeasured individual Old costs remain explicitly unmeasured.

| Game / exact identity | Codes used | Culling / LOD repair | HUD / 2D | Old cost | Verdict / remaining work |
|---|---|---|---|---|---|
| Ape Escape (USA) · `SCUS94423-1F9F011C` | [1 rows](../psx3d/romfs/widescreen/SCUS94423-1F9F011C.ws) | No independent bypass required in tested training area; expanded geometry stays filled. | Icons and training dialogue remain visible; no separate fix available. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| CTR - Crash Team Racing (USA) · `SCUS94426-CC613C2E` | [41 rows](../psx3d/romfs/widescreen/SCUS94426-CC613C2E.ws) | Yes: 043168 frustum width | HUD, font and overlay helpers included | 37.37 -> 33.38 fps (-10.68%); held back on Old | runtime enabled: accepted in tested scenes; models `3,1` (off on Old to keep the speed up). Full native bundle restores tower sections missing with projection-only in GPU simulator. HLE cold 9000-frame arcade race reached; 3600-frame protected-state-copy race passes. 35/35 changed active halfwords roll back. New 3DS pictures/full speed pass. Guarded AFA2 overlay fix remains unexercised; broader overlay/track coverage remains open. Own-HLE 3600-frame acceleration runs both finish; race timers/progress diverge (about 57s versus 54s), so host-vblank fps is not proof of identical guest game speed. |
| Crash Bandicoot (USA) · `SCUS94900-10A81C20` | [3 rows](../psx3d/romfs/widescreen/SCUS94900-10A81C20.ws) | No | No separate HUD repair found | Not individually measured | hidden: verified incomplete projection/culling; models `0,0`. REJECTED: projection leaves sawtooth ground gaps at N. Sanity Beach. Baked SLST visibility requires an actual repair; render-all prototypes corrupt guest RAM. |
| Crash Bandicoot 2 - Cortex Strikes Back (USA) · `SCUS94154-C5DF1D69` | [5 rows](../psx3d/romfs/widescreen/SCUS94154-C5DF1D69.ws) | Five-part projection/frustum bundle; Turtle Woods edges filled during traversal. | No separate HUD repair in source. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Gran Turismo 2 (USA) (Arcade Mode) (Rev 1) · `SCUS94455-5B4CDDF4` | [53 rows](../psx3d/romfs/widescreen/SCUS94455-5B4CDDF4.ws) | Race and mirror frustum extents included | Car selection, prerace, results, trophy and GT-mode 2D fixes included | 32.30 -> 31.19 fps (-3.42%); within 5% cost threshold | runtime enabled: accepted in tested scenes; models `2,1` (offered on Old with a speed warning). 16:9 uses 400x225 letterboxing; scene-limited verification only.; Old/New pictures checked in one benchmark scene; full-course culling/HUD acceptance pending.; Optional higher-distance source requests 325% PS1 CPU overclock and benefits from 8MB polygon buffers; full AI-car LOD requires 8MB RAM. Those are not validated 2MB companions and are excluded; the bundle already includes native race/mirror frustum and HUD repairs. |
| Klonoa - Door to Phantomile (USA) · `SLUS00585-BD171E3C` | [5 rows](../psx3d/romfs/widescreen/SLUS00585-BD171E3C.ws) | Four projection sites (one duplicated source row); first-level traversal keeps expanded geometry filled. | HUD remains visible but stretches; no separate source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Legacy of Kain - Soul Reaver (USA) · `SLUS00708-54A3F489` | [5 rows](../psx3d/romfs/widescreen/SLUS00708-54A3F489.ws) | Five-part native projection and companion instructions; movement through the opening cave stays filled. | No separate source HUD fix; HUD remains visible. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Metal Gear Solid (USA) (Disc 1) (Rev 1) · `SLUS00594-23BB5B12` | [28 rows](../psx3d/romfs/widescreen/SLUS00594-23BB5B12.ws) | Complete injected projection bundle; dock geometry remains filled. | Radar/text remain visible but stretch; no separate source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies.; Disc 2/swap and codec presentation are not accepted by dock-only checks. |
| R4 - Ridge Racer Type 4 (USA) · `SLUS00797-4AA72B9E` | [1 rows](../psx3d/romfs/widescreen/SLUS00797-4AA72B9E.ws) | No separate culling fix found; needs edge validation | No HUD correction available | 60.07 -> 60.19 fps (+0.21%); within 5% cost threshold | runtime enabled: accepted in tested scenes; models `1,1`. 16:9 uses 400x225 letterboxing; scene-limited verification only.; Old/New pictures checked in one benchmark scene; full-course culling/HUD acceptance pending. |
| Silent Hill (USA) · `SLUS00707-6434EBE6` | [2 rows](../psx3d/romfs/widescreen/SLUS00707-6434EBE6.ws) | No separate fix found | No separate repair found | 41.86 -> 38.69 fps (-7.58%); held back on Old | runtime enabled: accepted in tested scenes; models `3,1` (off on Old to keep the speed up). HLE street, camera movement and PICA/rollback checked; no proven general frustum repair.; Old 3DS cost exceeds 5% threshold; Widescreen is hidden there. New remains experimental. |
| Spider-Man (USA) · `SLUS00875-E2EA0021` | [2 rows](../psx3d/romfs/widescreen/SLUS00875-E2EA0021.ws) | Native projection and companion instruction; rooftop edge geometry remains filled during traversal. | HUD remains visible; no independent source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Spyro the Dragon (USA) · `SCUS94228-05FE587F` | [3 rows](../psx3d/romfs/widescreen/SCUS94228-05FE587F.ws) | Includes projection and companion A7BB instruction; needs edge validation | No separate HUD fix | Not individually measured | rejected: source warns of crashes; models `0,0`. upstream DuckStation patch warns "May Cause Crashes"; scene-limited testing cannot rule this out.; Hardware acceptance pending; no Old 3DS cost measurement.; Hidden on both consoles per Round 3 decision; no safe complete alternative verified. |
| Vagrant Story (USA) · `SLUS01040-2DF49560` | [2 rows](../psx3d/romfs/widescreen/SLUS01040-2DF49560.ws) | Native projection plus companion ratio; opening room movement stays filled. | Radar and HP remain visible; no separate source HUD repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Brave Fencer Musashi (USA) · `SLUS00726-490B13D4` | [1 rows](../psx3d/romfs/widescreen/SLUS00726-490B13D4.ws) | No independent fix found | No independently verified repair | Not individually measured | rejected: incomplete bundle; models `0,0`. Rejected: widened forest loses large floor triangles; no matched culling companion found. |
| Crash Bandicoot - Warped (USA) · `SCUS94244-317BD2C7` | [5 rows](../psx3d/romfs/widescreen/SCUS94244-317BD2C7.ws) | Five-part projection/frustum bundle; Orient Express edges stay filled during riding and jumps. | No separate source HUD repair; counters remain visible. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Digimon World (USA) · `SLUS01032-E2974D8B` | [1 rows](../psx3d/romfs/widescreen/SLUS01032-E2974D8B.ws) | No independent fix found | No independently verified repair | Not individually measured | rejected: incomplete bundle; models `0,0`. Rejected for field presentation: fixed backgrounds stay unchanged while projected actors shift and shrink. |
| Disney-Pixar Toy Story 2 - Buzz Lightyear to the Rescue! (USA) (Rev 1) · `SLUS00893-1203F43F` | [2 rows](../psx3d/romfs/widescreen/SLUS00893-1203F43F.ws) | Native projection and companion; Andy house traversal shows added geometry without holes. | No independent source HUD repair available. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Gran Turismo 2 (USA) (Simulation Mode) (Rev 2) · `SCUS94488-91BB5858` | [53 rows](../psx3d/romfs/widescreen/SCUS94488-91BB5858.ws) | Race and mirror frustum extents included | Car selection, prerace, results, trophy and GT-mode 2D fixes included | Not individually measured | runtime enabled: accepted in tested scenes; models `2,1` (offered on Old with a speed warning). 16:9 uses 400x225 letterboxing; scene-limited verification only.; Hardware acceptance pending; no Old 3DS cost measurement.; Optional higher-distance source requests 325% PS1 CPU overclock and benefits from 8MB polygon buffers; full AI-car LOD requires 8MB RAM. Those are not validated 2MB companions and are excluded; the bundle already includes native race/mirror frustum and HUD repairs. |
| Medal of Honor (USA) · `SLUS00974-DD99BDE6` | [33 rows](../psx3d/romfs/widescreen/SLUS00974-DD99BDE6.ws) | No independent frustum bypass found | Camera/weapon companions and aspect coefficient included | Not individually measured | runtime enabled: accepted in tested scenes; models `1,1`. HLE first mission and paired 3600-frame movement pass; 31/31 changed halfwords roll back. No independent universal frustum fix established; hardware and wider level/weapon/HUD acceptance remain open. |
| Medal of Honor - Underground (USA) · `SLUS01270-9436FCBE` | [33 rows](../psx3d/romfs/widescreen/SLUS01270-9436FCBE.ws) | Full projection/companion injection and weapon/HUD correction; main-game overlay guarded. | Weapon is intact with corrected live originals; compass and ammunition remain visible. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies.; Boot-only original conversion was superseded by measured HLE gameplay originals; earlier failed captures are retained, not the shipped code. |
| MediEvil II (USA) · `SCUS94564-0FF78A7B` | [1 rows](../psx3d/romfs/widescreen/SCUS94564-0FF78A7B.ws) | Native projection; expanded crypt geometry stays filled during room traversal. | No separate source HUD repair available. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Mega Man Legends (USA) · `SLUS00603-643F0FDD` | [28 rows](../psx3d/romfs/widescreen/SLUS00603-643F0FDD.ws) | Native injected projection/companion routine; post-tutorial corridor traversal stays filled. | HUD and minimap remain visible; no separate source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Need for Speed - High Stakes (USA) · `SLUS00826-C5D38319` | [16 rows](../psx3d/romfs/widescreen/SLUS00826-C5D38319.ws) | projection/GeomScreen and sky coordinates; no separate frustum bypass verified. | companion coordinates included; no independent HUD patch found. | Not individually measured | rejected: incomplete menu/effects treatment; models `0,0`. Flares/smoke/sun unscaled. Separate menu patch lacks clear redistribution grant.; Hardware acceptance pending; no Old 3DS cost measurement. |
| Spyro - Year of the Dragon (USA) (Rev 1) · `SCUS94467-E868CD6A` | [20 rows](../psx3d/romfs/widescreen/SCUS94467-E868CD6A.ws) | Injected companion routine; opening outdoor edges filled during movement. | No separate HUD fix available. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Spyro 2 - Ripto's Rage! (USA) · `SCUS94425-1C372E92` | [30 rows](../psx3d/romfs/widescreen/SCUS94425-1C372E92.ws) | Native companion projection routine plus all ten source LOD bypasses (no separate user toggle). | No separate HUD fix | Not individually measured | runtime enabled: accepted in tested scenes; models `1,1`. Projection plus ten LOD companion operations passed HLE opening-area traversal and rollback; individual hardware/Old cost pending. |
| Syphon Filter (USA) (Rev 1) · `SCUS94240-A9FFAA57` | [2 rows](../psx3d/romfs/widescreen/SCUS94240-A9FFAA57.ws) | Two native projection sites; Washington street traversal retains filled expanded edges. No independent bypass found. | Armor, danger indicator, radar and ammunition stay visible; no separate source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; later missions, cutscenes and overlay families are not campaign-tested.; Old cost and individual console gameplay pictures not measured. |
| Syphon Filter 2 (USA) (Disc 1) · `SCUS94451-260DF52C` | [2 rows](../psx3d/romfs/widescreen/SCUS94451-260DF52C.ws) | Two native projection sites; Colorado mountain opening traversal retains filled expanded edges. No independent bypass found. | Armor, radar and ammunition stay visible; no separate source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; later missions, cutscenes and overlay families are not campaign-tested.; Old cost and individual console gameplay pictures not measured.; Disc 1 only; disc 2/swap not verified. |
| Syphon Filter 3 (USA) · `SCUS94640-56B9564B` | [2 rows](../psx3d/romfs/widescreen/SCUS94640-56B9564B.ws) | Two native projection sites; Tokyo hotel opening traversal retains filled expanded edges. No independent bypass found. | Armor, radar and ammunition stay visible; no separate source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; later missions, cutscenes and overlay families are not campaign-tested.; Old cost and individual console gameplay pictures not measured. |
| Tenchu - Stealth Assassins (USA) (Rev 1) · `SLUS00706-65EFFADA` | [1 rows](../psx3d/romfs/widescreen/SLUS00706-65EFFADA.ws) | Native projection; starting mission edges stay filled during movement after matching the live overlay original. | Health and item HUD remain visible; no independent source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies.; Boot-only original conversion was superseded by measured HLE gameplay originals; earlier failed captures are retained, not the shipped code. |

## Complete 225-identity library audit

Local revisions take priority over NAS duplicates. Staged files are research
only; a source hit or a matching serial is not technical acceptance.

| Game / exact identity | Codes used | Culling / LOD repair | HUD / 2D | Old cost | Verdict / remaining work |
|---|---|---|---|---|---|
| Ape Escape (USA) · `SCUS94423-1F9F011C` | [1 rows](../psx3d/romfs/widescreen/SCUS94423-1F9F011C.ws) | No independent bypass required in tested training area; expanded geometry stays filled. | Icons and training dialogue remain visible; no separate fix available. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| CTR - Crash Team Racing (USA) · `SCUS94426-CC613C2E` | [41 rows](../psx3d/romfs/widescreen/SCUS94426-CC613C2E.ws) | Yes: 043168 frustum width | HUD, font and overlay helpers included | 37.37 -> 33.38 fps (-10.68%); held back on Old | runtime enabled: accepted in tested scenes; models `3,1` (off on Old to keep the speed up). Full native bundle restores tower sections missing with projection-only in GPU simulator. HLE cold 9000-frame arcade race reached; 3600-frame protected-state-copy race passes. 35/35 changed active halfwords roll back. New 3DS pictures/full speed pass. Guarded AFA2 overlay fix remains unexercised; broader overlay/track coverage remains open. Own-HLE 3600-frame acceleration runs both finish; race timers/progress diverge (about 57s versus 54s), so host-vblank fps is not proof of identical guest game speed. |
| Castlevania - Symphony of the Night (USA) · `SLUS00067-71FAA98F` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D game, no useful GTE widescreen; models `preset/fallback`. Widescreen hidden on both consoles; Stretch remains available. |
| Crash Bandicoot (USA) · `SCUS94900-10A81C20` | [3 rows](../psx3d/romfs/widescreen/SCUS94900-10A81C20.ws) | No | No separate HUD repair found | Not individually measured | hidden: verified incomplete projection/culling; models `0,0`. REJECTED: projection leaves sawtooth ground gaps at N. Sanity Beach. Baked SLST visibility requires an actual repair; render-all prototypes corrupt guest RAM. |
| Crash Bandicoot 2 - Cortex Strikes Back (USA) · `SCUS94154-C5DF1D69` | [5 rows](../psx3d/romfs/widescreen/SCUS94154-C5DF1D69.ws) | Five-part projection/frustum bundle; Turtle Woods edges filled during traversal. | No separate HUD repair in source. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Driver - You Are the Wheelman (USA) (Rev 1) · `SLUS00842-659EA8ED` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Driver 2 (USA) (Disc 1) (Rev 1) · `SLUS01161-AB2662BF` | [1 rows](../tools/widescreen/research/SLUS01161-AB2662BF.ws) | No | No separate repair found | Not individually measured | staged: technical gates pending; models `preset/fallback`. REJECTED for this revision: A707EC48 expects 1000; actual gameplay contains 0002. No patch is active; serial-only source does not match Rev 1. |
| Einhaender (USA) · `SCUS94243-58962070` | [1 rows](../tools/widescreen/research/SCUS94243-58962070.ws) | No independent fix found | No independently verified repair | Not individually measured | staged: technical gates pending; models `preset/fallback`. 6000-frame HLE cold smoke; gameplay/edge/Old acceptance pending. |
| Final Fantasy VII (USA) (Disc 1) · `SCUS94163-D5BF7E0E` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Final Fantasy VIII (USA) (Disc 1) · `SLUS00892-48E21985` | [6 rows](../tools/widescreen/research/SLUS00892-48E21985.ws) | No | Battle guard; fields remain 4:3 in game | Not individually measured | staged: technical gates pending; models `preset/fallback`. Battle-only guarded patch; no actual battle acceptance. Disc 1 HLE cold/new-game probe reaches opening movie. Other discs share boot EXE fingerprint but lack individual gameplay tests. Scene-aware 4:3 fields versus 16:9 battles is required before runtime enablement. |
| Gran Turismo (USA) · `SCUS94194-9F6EE4BB` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Gran Turismo 2 (USA) (Arcade Mode) (Rev 1) · `SCUS94455-5B4CDDF4` | [53 rows](../psx3d/romfs/widescreen/SCUS94455-5B4CDDF4.ws) | Race and mirror frustum extents included | Car selection, prerace, results, trophy and GT-mode 2D fixes included | 32.30 -> 31.19 fps (-3.42%); within 5% cost threshold | runtime enabled: accepted in tested scenes; models `2,1` (offered on Old with a speed warning). 16:9 uses 400x225 letterboxing; scene-limited verification only.; Old/New pictures checked in one benchmark scene; full-course culling/HUD acceptance pending.; Optional higher-distance source requests 325% PS1 CPU overclock and benefits from 8MB polygon buffers; full AI-car LOD requires 8MB RAM. Those are not validated 2MB companions and are excluded; the bundle already includes native race/mirror frustum and HUD repairs. |
| Klonoa - Door to Phantomile (USA) · `SLUS00585-BD171E3C` | [5 rows](../psx3d/romfs/widescreen/SLUS00585-BD171E3C.ws) | Four projection sites (one duplicated source row); first-level traversal keeps expanded geometry filled. | HUD remains visible but stretches; no separate source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Legacy of Kain - Soul Reaver (USA) · `SLUS00708-54A3F489` | [5 rows](../psx3d/romfs/widescreen/SLUS00708-54A3F489.ws) | Five-part native projection and companion instructions; movement through the opening cave stays filled. | No separate source HUD fix; HUD remains visible. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Mega Man Legends 2 (USA) · `SLUS01140-F7BC5241` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Mega Man X4 (USA) · `SLUS00561-C9E8AF1A` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Metal Gear Solid (USA) (Disc 1) (Rev 1) · `SLUS00594-23BB5B12` | [28 rows](../psx3d/romfs/widescreen/SLUS00594-23BB5B12.ws) | Complete injected projection bundle; dock geometry remains filled. | Radar/text remain visible but stretch; no separate source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies.; Disc 2/swap and codec presentation are not accepted by dock-only checks. |
| Oddworld - Abe's Oddysee (USA) (Rev 2) · `SLUS00190-D63C055D` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| R4 - Ridge Racer Type 4 (USA) · `SLUS00797-4AA72B9E` | [1 rows](../psx3d/romfs/widescreen/SLUS00797-4AA72B9E.ws) | No separate culling fix found; needs edge validation | No HUD correction available | 60.07 -> 60.19 fps (+0.21%); within 5% cost threshold | runtime enabled: accepted in tested scenes; models `1,1`. 16:9 uses 400x225 letterboxing; scene-limited verification only.; Old/New pictures checked in one benchmark scene; full-course culling/HUD acceptance pending. |
| Rayman (USA) · `SLUS00005-B79EF4ED` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Resident Evil 2 - Dual Shock Ver. (USA) (Disc 1) · `SLUS00748-94DADFD9` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Silent Hill (USA) · `SLUS00707-6434EBE6` | [2 rows](../psx3d/romfs/widescreen/SLUS00707-6434EBE6.ws) | No separate fix found | No separate repair found | 41.86 -> 38.69 fps (-7.58%); held back on Old | runtime enabled: accepted in tested scenes; models `3,1` (off on Old to keep the speed up). HLE street, camera movement and PICA/rollback checked; no proven general frustum repair.; Old 3DS cost exceeds 5% threshold; Widescreen is hidden there. New remains experimental. |
| Spider-Man (USA) · `SLUS00875-E2EA0021` | [2 rows](../psx3d/romfs/widescreen/SLUS00875-E2EA0021.ws) | Native projection and companion instruction; rooftop edge geometry remains filled during traversal. | HUD remains visible; no independent source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Spyro the Dragon (USA) · `SCUS94228-05FE587F` | [3 rows](../psx3d/romfs/widescreen/SCUS94228-05FE587F.ws) | Includes projection and companion A7BB instruction; needs edge validation | No separate HUD fix | Not individually measured | rejected: source warns of crashes; models `0,0`. upstream DuckStation patch warns "May Cause Crashes"; scene-limited testing cannot rule this out.; Hardware acceptance pending; no Old 3DS cost measurement.; Hidden on both consoles per Round 3 decision; no safe complete alternative verified. |
| Street Fighter Alpha 3 (USA) · `SLUS00821-AF4B6F36` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Tekken 3 (USA) · `SLUS00402-AEDCC9C5` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tomb Raider (USA) (Rev 6) · `SLUS00152-4B8F9B3B` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tony Hawk's Pro Skater 2 (USA) · `SLUS01066-C5DAD834` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Twisted Metal 2 (USA) · `SCUS94306-C6DDDD9D` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Vagrant Story (USA) · `SLUS01040-2DF49560` | [2 rows](../psx3d/romfs/widescreen/SLUS01040-2DF49560.ws) | Native projection plus companion ratio; opening room movement stays filled. | Radar and HP remain visible; no separate source HUD repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Wipeout XL (USA) · `SCUS94351-20951767` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Ace Combat 2 (USA) · `SLUS00404-C583466D` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Ace Combat 3 - Electrosphere (USA) · `SLUS00972-6D4A6460` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Advanced Dungeons & Dragons - Iron & Blood - Warriors of Ravenloft (USA) · `SLUS00378-F52F265A` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Alone in the Dark - One-Eyed Jack's Revenge (USA) · `SLUS00239-C59E1857` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Alone in the Dark - The New Nightmare (USA) (Disc 1) · `SLUS01201-199DE121` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Alone in the Dark - The New Nightmare (USA) (Disc 2) · `SLUS01377-199DE121` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Alundra (USA) (Rev 1) · `SLUS00553-9C26A44B` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Alundra 2 - A New Legend Begins (USA) · `SLUS01017-27E52388` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Asteroids (USA) · `SLUS00773-44B10E07` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Azure Dreams (USA) · `SLUS00614-366E137A` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Bishi Bashi Special (Europe) · `SLES02537-56F57C67` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Bishi Bashi Special 2 (Japan) · `SLPM86267-C6F53DFA` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Bishi Bashi Special 3 - Step Champ (Japan) · `SLPM86539-6E9C002A` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Blood Omen - Legacy of Kain (USA) · `SLUS00027-85F55048` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Brave Fencer Musashi (USA) · `SLUS00726-490B13D4` | [1 rows](../psx3d/romfs/widescreen/SLUS00726-490B13D4.ws) | No independent fix found | No independently verified repair | Not individually measured | rejected: incomplete bundle; models `0,0`. Rejected: widened forest loses large floor triangles; no matched culling companion found. |
| Breath of Fire III (USA) · `SLUS00422-98F0BA7D` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Breath of Fire IV (USA) · `SLUS01324-6335622A` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Castlevania Chronicles (USA) · `SLUS01384-A3ED1736` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Chrono Cross (USA) (Disc 1) · `SLUS01041-A8CCEC38` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Chrono Cross (USA) (Disc 2) · `SLUS01080-A8CCEC38` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Chrono Trigger (Japan) · `SLPS02430-90F21F89` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Colony Wars (USA) (Disc 1) · `SLUS00543-E8AA0C19` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Colony Wars (USA) (Disc 2) · `SLUS00554-E8AA0C19` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Colony Wars - Red Sun (USA) · `SLUS00866-4E5C7696` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Colony Wars - Vengeance (USA) · `SLUS00722-EF80AA77` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Crash Bandicoot - Warped (USA) · `SCUS94244-317BD2C7` | [5 rows](../psx3d/romfs/widescreen/SCUS94244-317BD2C7.ws) | Five-part projection/frustum bundle; Orient Express edges stay filled during riding and jumps. | No separate source HUD repair; counters remain visible. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Croc - Legend of the Gobbos (USA) · `SLUS00530-BD2EB81B` | [30 rows](../tools/widescreen/research/SLUS00530-BD2EB81B.ws) | No matched companion established | No matched repair established | Not individually measured | staged: technical gate failed; models `preset/fallback`. Staged: crystals/HUD disappear in widened scene; complete repair not verified. |
| Croc 2 (USA) · `SLUS00634-4585B65A` | [32 rows](../tools/widescreen/research/SLUS00634-4585B65A.ws) | No matched companion established | No matched repair established | Not individually measured | staged: technical gate failed; models `preset/fallback`. Staged only: gameplay hook originals do not match active overlay; only cave writes apply. |
| Darkstone (USA) · `SLUS01182-43F56464` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Diablo (USA) (En,Fr,De,Sv) · `SLUS00619-570A2F38` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Digimon - Digital Card Battle (USA) · `SLUS01328-4353896D` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Digimon Rumble Arena (USA) · `SLUS01404-22B1A168` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Digimon World (USA) · `SLUS01032-E2974D8B` | [1 rows](../psx3d/romfs/widescreen/SLUS01032-E2974D8B.ws) | No independent fix found | No independently verified repair | Not individually measured | rejected: incomplete bundle; models `0,0`. Rejected for field presentation: fixed backgrounds stay unchanged while projected actors shift and shrink. |
| Digimon World 2 (USA) · `SLUS01193-F9CAD060` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Digimon World 3 (USA) · `SLUS01436-C6F24CC9` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Dino Crisis (USA) (Rev 1) · `SLUS00922-95310A3B` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Dino Crisis 2 (USA) · `SLUS01279-81676822` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Disney-Pixar Toy Story 2 - Buzz Lightyear to the Rescue! (USA) (Rev 1) · `SLUS00893-1203F43F` | [2 rows](../psx3d/romfs/widescreen/SLUS00893-1203F43F.ws) | Native projection and companion; Andy house traversal shows added geometry without holes. | No independent source HUD repair available. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Dragon Warrior VII (USA) (Disc 1) · `SLUSP01206-399DA9E5` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Dragon Warrior VII (USA) (Disc 2) · `SLUSP01346-05BF15AE` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Driver 2 (USA) (Disc 2) (Rev 1) · `SLUS01318-AB2662BF` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Echo Night (USA) · `SLUS00820-E2D6FECA` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Echo Night 2 - Nemuri no Shihaisha (Japan) · `SLPS02167-19925B9D` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Final Fantasy Anthology - Final Fantasy V (USA) (Rev 1) · `SLUS00879-2A850C21` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Final Fantasy Anthology - Final Fantasy VI (USA) (Rev 1) · `SLUS00900-334F68E6` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Final Fantasy Chronicles - Chrono Trigger (USA) (Rev 1) · `SLUS01363-8372ED4D` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Final Fantasy Chronicles - Final Fantasy IV (USA) (Rev 1) · `SLUS01360-81142184` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Final Fantasy IX (USA) (Disc 1) (Rev 1) · `SLUS01251-EA9762B2` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Final Fantasy IX (USA) (Disc 2) (Rev 1) · `SLUS01295-EA9762B2` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Final Fantasy IX (USA) (Disc 3) (Rev 1) · `SLUS01296-EA9762B2` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Final Fantasy IX (USA) (Disc 4) (Rev 1) · `SLUS01297-EA9762B2` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Final Fantasy Origins (USA) (Rev 1) · `SLUS01541-BAC483FD` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Final Fantasy Tactics (USA) · `SCUS94221-BF574167` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Final Fantasy VII (USA) (Disc 2) · `SCUS94164-D5BF7E0E` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Final Fantasy VII (USA) (Disc 3) · `SCUS94165-D5BF7E0E` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Final Fantasy VIII (USA) (Disc 2) · `SLUS00908-48E21985` | [6 rows](../tools/widescreen/research/SLUS00908-48E21985.ws) | No | Battle guard | Not individually measured | staged: technical gates pending; models `preset/fallback`. Battle-only guarded patch; no actual battle acceptance. Disc 1 HLE cold/new-game probe reaches opening movie. Other discs share boot EXE fingerprint but lack individual gameplay tests. Scene-aware 4:3 fields versus 16:9 battles is required before runtime enablement. |
| Final Fantasy VIII (USA) (Disc 3) · `SLUS00909-48E21985` | [6 rows](../tools/widescreen/research/SLUS00909-48E21985.ws) | No | Battle guard | Not individually measured | staged: technical gates pending; models `preset/fallback`. Battle-only guarded patch; no actual battle acceptance. Disc 1 HLE cold/new-game probe reaches opening movie. Other discs share boot EXE fingerprint but lack individual gameplay tests. Scene-aware 4:3 fields versus 16:9 battles is required before runtime enablement. |
| Final Fantasy VIII (USA) (Disc 4) · `SLUS00910-48E21985` | [6 rows](../tools/widescreen/research/SLUS00910-48E21985.ws) | No | Battle guard | Not individually measured | staged: technical gates pending; models `preset/fallback`. Battle-only guarded patch; no actual battle acceptance. Disc 1 HLE cold/new-game probe reaches opening movie. Other discs share boot EXE fingerprint but lack individual gameplay tests. Scene-aware 4:3 fields versus 16:9 battles is required before runtime enablement. |
| Fisherman's Bait - A Bass Challenge (USA) · `SLUS00802-F6661C55` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Fisherman's Bait 2 - Big Ol' Bass (USA) · `SLUS00999-A2E54042` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Front Mission 3 (USA) · `SLUS01011-694D732B` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Gran Turismo 2 (USA) (Simulation Mode) (Rev 2) · `SCUS94488-91BB5858` | [53 rows](../psx3d/romfs/widescreen/SCUS94488-91BB5858.ws) | Race and mirror frustum extents included | Car selection, prerace, results, trophy and GT-mode 2D fixes included | Not individually measured | runtime enabled: accepted in tested scenes; models `2,1` (offered on Old with a speed warning). 16:9 uses 400x225 letterboxing; scene-limited verification only.; Hardware acceptance pending; no Old 3DS cost measurement.; Optional higher-distance source requests 325% PS1 CPU overclock and benefits from 8MB polygon buffers; full AI-car LOD requires 8MB RAM. Those are not validated 2MB companions and are excluded; the bundle already includes native race/mirror frustum and HUD repairs. |
| Grand Theft Auto (USA) · `SLUS00106-40982470` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Grand Theft Auto - Mission Pack 1 - London 1969 (USA) · `SLUS00846-7513398F` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Grand Theft Auto 2 (USA) · `SLUS00789-969C7D25` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Grandia (USA) (Disc 1) · `SCUS94457-BA4DF812` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Grandia (USA) (Disc 2) · `SCUS94465-BA4DF812` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Harvest Moon - Back to Nature (USA) · `SLUS01115-02093AD3` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Hogs of War (USA) · `SLUS01195-A0154FB2` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| I.Q - Intelligent Qube (USA) · `SCUS94181-E11655D1` | [1 rows](../tools/widescreen/research/SCUS94181-E11655D1.ws) | No independent fix found | No independently verified repair | Not individually measured | staged: technical gates pending; models `preset/fallback`. 6000-frame HLE cold smoke; gameplay/edge/Old acceptance pending. |
| Koudelka (USA) (Disc 1) · `SLUS01051-58D76708` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Koudelka (USA) (Disc 2) · `SLUS01100-58D76708` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Koudelka (USA) (Disc 3) · `SLUS01101-58D76708` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Koudelka (USA) (Disc 4) · `SLUS01102-58D76708` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Le Mans 24 Hours (Europe) (En,Fr,De,Es,It,Pt) · `SLES01362-D3F598DC` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Legend of Dragoon, The (USA) (Disc 1) · `SCUS94491-776DB50E` | [1 rows](../tools/widescreen/research/SCUS94491-776DB50E.ws) | No independent fix found | No independently verified repair | Not individually measured | staged: technical gates pending; models `preset/fallback`. 6000-frame HLE cold smoke; gameplay/edge/Old acceptance pending. |
| Legend of Dragoon, The (USA) (Disc 2) · `SCUS94584-776DB50E` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Legend of Dragoon, The (USA) (Disc 3) · `SCUS94585-776DB50E` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Legend of Dragoon, The (USA) (Disc 4) · `SCUS94586-776DB50E` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Legend of Legaia (USA) · `SCUS94254-F5CE62B1` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Legend of Mana (USA) · `SLUS01013-14A06A15` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Lunar - Silver Star Story Complete (USA) (Disc 1) · `SLUS00628-0DE8DF47` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Lunar - Silver Star Story Complete (USA) (Disc 2) · `SLUS00899-0DE8DF47` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Lunar 2 - Eternal Blue Complete (USA) (Disc 1) · `SLUS01071-8D5F5751` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Lunar 2 - Eternal Blue Complete (USA) (Disc 2) · `SLUS01239-8D5F5751` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Lunar 2 - Eternal Blue Complete (USA) (Disc 3) · `SLUS01240-8D5F5751` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Martian Gothic - Unification (USA) · `SLUS01148-C617DFCA` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Marvel Super Heroes vs. Street Fighter (USA) · `SLUS00793-AEC9BDA7` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Medal of Honor (USA) · `SLUS00974-DD99BDE6` | [33 rows](../psx3d/romfs/widescreen/SLUS00974-DD99BDE6.ws) | No independent frustum bypass found | Camera/weapon companions and aspect coefficient included | Not individually measured | runtime enabled: accepted in tested scenes; models `1,1`. HLE first mission and paired 3600-frame movement pass; 31/31 changed halfwords roll back. No independent universal frustum fix established; hardware and wider level/weapon/HUD acceptance remain open. |
| Medal of Honor - Underground (USA) · `SLUS01270-9436FCBE` | [33 rows](../psx3d/romfs/widescreen/SLUS01270-9436FCBE.ws) | Full projection/companion injection and weapon/HUD correction; main-game overlay guarded. | Weapon is intact with corrected live originals; compass and ammunition remain visible. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies.; Boot-only original conversion was superseded by measured HLE gameplay originals; earlier failed captures are retained, not the shipped code. |
| MediEvil (USA) · `SCUS94227-E618F9F2` | [1 rows](../tools/widescreen/research/SCUS94227-E618F9F2.ws) | No independent fix found | No independently verified repair | Not individually measured | staged: technical gates pending; models `preset/fallback`. 6000-frame HLE cold smoke; gameplay/edge/Old acceptance pending. |
| MediEvil II (USA) · `SCUS94564-0FF78A7B` | [1 rows](../psx3d/romfs/widescreen/SCUS94564-0FF78A7B.ws) | Native projection; expanded crypt geometry stays filled during room traversal. | No separate source HUD repair available. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Mega Man 8 (USA) · `SLUS00453-99008F12` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Mega Man Legends (USA) · `SLUS00603-643F0FDD` | [28 rows](../psx3d/romfs/widescreen/SLUS00603-643F0FDD.ws) | Native injected projection/companion routine; post-tutorial corridor traversal stays filled. | HUD and minimap remain visible; no separate source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Mega Man X5 (USA) · `SLUS01334-E9BBD008` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Mega Man X6 (USA) (Rev 1) · `SLUS01395-91DC45D4` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Metal Gear Solid (USA) (Disc 2) (Rev 1) · `SLUS00776-23BB5B12` | [28 rows](../tools/widescreen/research/SLUS00776-23BB5B12.ws) | No independent bypass found | Injected projection companion | Not individually measured | staged: technical gates pending; models `preset/fallback`. Same boot EXE as disc 1; 6000-frame cold smoke only. Disc swap pending. |
| Metal Gear Solid - VR Missions (USA) · `SLUS00957-69D1E237` | [28 rows](../tools/widescreen/research/SLUS00957-69D1E237.ws) | No independent bypass found | Injected projection companion | Not individually measured | staged: technical gates pending; models `preset/fallback`. 6000-frame cold smoke only; VR gameplay edge acceptance pending. |
| Monster Rancher (USA) · `SLUS00568-DF1DC50D` | [1 rows](../tools/widescreen/research/SLUS00568-DF1DC50D.ws) | No independent fix found | No independently verified repair | Not individually measured | staged: technical gates pending; models `preset/fallback`. 6000-frame HLE cold smoke; gameplay/edge/Old acceptance pending. |
| Monster Rancher 2 (USA) · `SLUS00917-80546B12` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Monster Rancher Battle Card - Episode II (USA) · `SLUS01178-CCB67D66` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Monster Rancher Hop-A-Bout (USA) · `SLUS01325-0E371265` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| N-Gen Racing (USA) · `SLUS01155-019A8CB2` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Need for Speed - High Stakes (USA) · `SLUS00826-C5D38319` | [16 rows](../psx3d/romfs/widescreen/SLUS00826-C5D38319.ws) | projection/GeomScreen and sky coordinates; no separate frustum bypass verified. | companion coordinates included; no independent HUD patch found. | Not individually measured | rejected: incomplete menu/effects treatment; models `0,0`. Flares/smoke/sun unscaled. Separate menu patch lacks clear redistribution grant.; Hardware acceptance pending; no Old 3DS cost measurement. |
| Need for Speed - Porsche Unleashed (USA) · `SLUS01104-FA723E90` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Need for Speed - V-Rally (USA) · `SLUS00590-83EB438A` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Need for Speed - V-Rally 2 (USA) · `SLUS01003-D12F1765` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Need for Speed II (USA) · `SLUS00276-5A821267` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Need for Speed III - Hot Pursuit (USA) · `SLUS00620-1A73CAC4` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Nightmare Creatures (USA) · `SLUS00582-0B705372` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Nightmare Creatures II (USA) · `SLUS01112-11519F77` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Oddworld - Abe's Exoddus (USA) (Disc 1) · `SLUS00710-4749E668` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Oddworld - Abe's Exoddus (USA) (Disc 2) · `SLUS00731-1190C59F` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| PaRappa the Rapper (USA) (En,Fr,De,Es,It) · `SCUS94183-9DDE0C0B` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Parasite Eve (USA) (Disc 1) · `SLUS00662-C7EB4A20` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Parasite Eve (USA) (Disc 2) · `SLUS00668-C7EB4A20` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Persona (USA) · `SLUS00339-72150A2B` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Persona 2 - Eternal Punishment (USA) · `SLUS01158-14AEA965` | [1 rows](../tools/widescreen/research/SLUS01158-14AEA965.ws) | No independent fix found | No independently verified repair | Not individually measured | staged: technical gates pending; models `preset/fallback`. 6000-frame HLE cold smoke; gameplay/edge/Old acceptance pending. |
| Rayman 2 - The Great Escape (USA) (En,Fr,Es) · `SLUS01235-3B6A1451` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Rayman Brain Games (USA) · `SLUS01265-5D4B3F23` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Rayman Rush (USA) · `SLUS01458-9ECE27E9` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Resident Evil (USA) · `SLUS00170-B6D006BB` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Resident Evil - Director's Cut - Dual Shock Ver. (USA) · `SLUS00747-94CD6E9C` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Resident Evil - Survivor (USA) · `SLUS01087-8F8661F0` | [1 rows](../tools/widescreen/research/SLUS01087-8F8661F0.ws) | No independent fix found | No independently verified repair | Not individually measured | staged: technical gates pending; models `preset/fallback`. 6000-frame HLE cold smoke; gameplay/edge/Old acceptance pending. |
| Resident Evil 2 - Dual Shock Ver. (USA) (Disc 2) · `SLUS00756-71060043` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Resident Evil 3 - Nemesis (USA) · `SLUS00923-F22C8CE9` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Road & Track Presents - The Need for Speed (USA) · `SLUS00204-C6CA79EC` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| SaGa Frontier (USA) · `SCUS94230-A89E9365` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| SaGa Frontier 2 (USA) · `SLUS00933-5DF6EC96` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Soul Blade (USA) (Rev 1) · `SLUS00240-738EE69D` | [1 rows](../tools/widescreen/research/SLUS00240-738EE69D.ws) | No independent fix found | No independently verified repair | Not individually measured | staged: technical gates pending; models `preset/fallback`. 6000-frame HLE cold smoke; gameplay/edge/Old acceptance pending. |
| Spider-Man 2 - Enter - Electro (USA) (Rev 1) · `SLUS01378-E2B15771` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Spyro - Year of the Dragon (USA) (Rev 1) · `SCUS94467-E868CD6A` | [20 rows](../psx3d/romfs/widescreen/SCUS94467-E868CD6A.ws) | Injected companion routine; opening outdoor edges filled during movement. | No separate HUD fix available. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies. |
| Spyro 2 - Ripto's Rage! (USA) · `SCUS94425-1C372E92` | [30 rows](../psx3d/romfs/widescreen/SCUS94425-1C372E92.ws) | Native companion projection routine plus all ten source LOD bypasses (no separate user toggle). | No separate HUD fix | Not individually measured | runtime enabled: accepted in tested scenes; models `1,1`. Projection plus ten LOD companion operations passed HLE opening-area traversal and rollback; individual hardware/Old cost pending. |
| Star Ocean - The Second Story (USA) (Disc 1) · `SCUS94421-880BBFD2` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Star Ocean - The Second Story (USA) (Disc 2) · `SCUS94422-880BBFD2` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Star Trek - Invasion (USA) · `SLUS00924-8113243A` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Street Fighter - The Movie (USA) · `SLUS00041-CA84EC82` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Street Fighter Alpha - Warriors' Dreams (USA) · `SLUS00197-9A511AA6` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Street Fighter Alpha 2 (USA) (Rev 1) · `SLUS00258-300F4AB9` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Street Fighter Collection (USA) (Disc 1) (Rev 1) · `SLUS00423-C9AF9BE3` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Street Fighter Collection (USA) (Disc 2) (Rev 1) · `SLUS00584-E84591E6` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Suikoden (USA) (Rev 1) · `SLUS00292-D49E89C0` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Suikoden II (USA) · `SLUS00958-88DD8A60` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Super Puzzle Fighter II Turbo (USA) · `SLUS00418-E97A7F6D` | None; hidden | N/A: 2D-only presentation | No useful native/GTE wide scene; Stretch remains available | Not individually measured | hidden: 2D-only presentation; models `preset/fallback`. GTE squeeze cannot create extra 2D background; Widescreen hidden on both consoles. Stretch remains available. |
| Syphon Filter (USA) (Rev 1) · `SCUS94240-A9FFAA57` | [2 rows](../psx3d/romfs/widescreen/SCUS94240-A9FFAA57.ws) | Two native projection sites; Washington street traversal retains filled expanded edges. No independent bypass found. | Armor, danger indicator, radar and ammunition stay visible; no separate source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; later missions, cutscenes and overlay families are not campaign-tested.; Old cost and individual console gameplay pictures not measured. |
| Syphon Filter 2 (USA) (Disc 1) · `SCUS94451-260DF52C` | [2 rows](../psx3d/romfs/widescreen/SCUS94451-260DF52C.ws) | Two native projection sites; Colorado mountain opening traversal retains filled expanded edges. No independent bypass found. | Armor, radar and ammunition stay visible; no separate source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; later missions, cutscenes and overlay families are not campaign-tested.; Old cost and individual console gameplay pictures not measured.; Disc 1 only; disc 2/swap not verified. |
| Syphon Filter 2 (USA) (Disc 2) · `SCUS94492-260DF52C` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Syphon Filter 3 (USA) · `SCUS94640-56B9564B` | [2 rows](../psx3d/romfs/widescreen/SCUS94640-56B9564B.ws) | Two native projection sites; Tokyo hotel opening traversal retains filled expanded edges. No independent bypass found. | Armor, radar and ammunition stay visible; no separate source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; later missions, cutscenes and overlay families are not campaign-tested.; Old cost and individual console gameplay pictures not measured. |
| Tactics Ogre (USA) · `SLUS00560-947D9C7C` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tales of Destiny (USA) · `SLUS00626-CED34558` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tales of Destiny II (USA) (Disc 1) · `SLUS01355-0CB0A81B` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tales of Destiny II (USA) (Disc 2) · `SLUS01367-0CB0A81B` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tales of Destiny II (USA) (Disc 3) · `SLUS01368-0CB0A81B` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tekken 2 (USA) (Rev 1) · `SLUS00213-C73D8163` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tenchu - Stealth Assassins (USA) (Rev 1) · `SLUS00706-65EFFADA` | [1 rows](../psx3d/romfs/widescreen/SLUS00706-65EFFADA.ws) | Native projection; starting mission edges stay filled during movement after matching the live overlay original. | Health and item HUD remain visible; no independent source repair. | Not individually measured | runtime enabled: simulator gameplay accepted; hardware follow-up; models `1,1`. Scene-limited acceptance; broader levels and cutscenes are not campaign-tested.; Old cost not individually measured; representative Old gate applies.; Boot-only original conversion was superseded by measured HLE gameplay originals; earlier failed captures are retained, not the shipped code. |
| Tenchu 2 - Birth of the Stealth Assassins (USA) · `SLUS00939-90517B47` | [1 rows](../tools/widescreen/research/SLUS00939-90517B47.ws) | No matched companion established | No matched repair established | Not individually measured | staged: technical gate failed; models `preset/fallback`. Staged only: source write never becomes active in this scene. |
| Threads of Fate (USA) · `SLUS01019-D13BD5F5` | [2 rows](../tools/widescreen/research/SLUS01019-D13BD5F5.ws) | No matched companion established | No matched repair established | Not individually measured | staged: technical gates pending; models `preset/fallback`. No complete revision-matched bundle verified |
| Time Crisis (USA) · `SLUS00405-3AD2A2C2` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Time Crisis - Project Titan (USA) · `SLUS01336-E0BDE722` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tobal 2 (Japan) (Rev 1) · `SLPM86033-1D5570F3` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tobal No. 1 (USA) · `SCUS94208-FB19BE03` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tomb Raider - The Last Revelation (USA) (Rev 1) · `SLUS00885-96AAD607` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tomb Raider Chronicles (USA) (Rev 1) · `SLUS01311-983B8063` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tomb Raider II - Starring Lara Croft (USA) (Rev 3) · `SLUS00437-A116C633` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tomb Raider III - Adventures of Lara Croft (USA) (Rev 2) · `SLUS00691-A116C633` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tomba! (USA) · `SCUS94236-15EF433E` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tomba! 2 - The Evil Swine Return (USA) · `SCUS94454-A4C9FEF8` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tony Hawk's Pro Skater (USA) · `SLUS00860-DF357E77` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tony Hawk's Pro Skater 3 (USA) · `SLUS01419-F9599844` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Tony Hawk's Pro Skater 4 (USA) · `SLUS01485-88631028` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Treasures of the Deep (USA) · `SLUS00430-4FAEC4D2` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Twisted Metal (USA) · `SCUS94304-A4D19270` | [1 rows](../tools/widescreen/research/SCUS94304-A4D19270.ws) | No independent fix found | No independently verified repair | Not individually measured | staged: technical gates pending; models `preset/fallback`. 6000-frame HLE cold smoke; gameplay/edge/Old acceptance pending. |
| Twisted Metal - Small Brawl (USA) · `SCUS94642-D5A1B116` | [1 rows](../tools/widescreen/research/SCUS94642-D5A1B116.ws) | No independent fix found | No independently verified repair | Not individually measured | staged: technical gates pending; models `preset/fallback`. 6000-frame HLE cold smoke; gameplay/edge/Old acceptance pending. |
| Twisted Metal 4 (USA) (Rev 1) · `SCUS94560-49638F7C` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Twisted Metal III (USA) (Rev 1) · `SCUS94249-3229CC61` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Urban Chaos (USA) · `SLUS01019-5C231AA6` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Valkyrie Profile (USA) (Disc 1) · `SLUS01156-20100BC0` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Valkyrie Profile (USA) (Disc 2) · `SLUS01179-20100BC0` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Vandal Hearts (USA) · `SLUS00447-561EEEC3` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Vandal Hearts II (USA) · `SLUS00940-04493FEA` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| WWF Attitude (USA) · `SLUS00831-13015A9C` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| WWF In Your House (USA) (Rev 1) · `SLUS00246-1DF04EC9` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| WWF SmackDown! (USA) · `SLUS00927-CB308C7D` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| WWF SmackDown! 2 - Know Your Role (USA) · `SLUS01234-1BB589E1` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| WWF War Zone (USA) (Rev 1) · `SLUS00495-17F3AFE0` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| WWF WrestleMania - The Arcade Game (USA) · `SLUS00013-C09C395E` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Wild Arms (USA) · `SCUS94608-684F750F` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Wild Arms 2 (USA) (Disc 1) · `SCUS94484-2761DCC6` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Wild Arms 2 (USA) (Disc 2) · `SCUS94498-2761DCC6` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| WipEout (USA) · `SCUS94301-44E415E7` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| WipEout 3 (USA) · `SLUS00865-C39F3D8D` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| World of Dragon Warrior - Torneko - The Last Hope (USA) · `SLUS01181-5136B4F1` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Xenogears (USA) (Disc 1) · `SLUS00664-BD965434` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |
| Xenogears (USA) (Disc 2) · `SLUS00669-F5F22B4C` | None; GTE fallback | No matched companion established | No matched repair established | Not individually measured | not curated: GTE fallback; models `preset/fallback`. No complete revision-matched bundle verified |

## Remaining coverage and limits

- Priority gaps remain for Tekken 3, Tomb Raider 1–4, Driver 1/2, GT1,
  Wipeout 3/XL, Twisted Metal 2/4 and FFVII/IX. No complete revision-matched
  source bundle was found. Driver 2's tested source had no active writes.
- FFVIII's staged battle-only patch needs actual battle validation and
  scene-aware aspect behavior. It must not distort fixed-background fields.
- Crash 1 and Spyro 1 require safe complete alternatives before re-enabling.
  Musashi, Digimon and NFS need complete geometry/2D/effects repairs.
- Runtime acceptance is limited to the scenes described, not every overlay,
  cutscene or campaign. Individual Old cost and new-game hardware gameplay
  checks are still missing where marked. Console cold boot pictures do not
  close those gaps.

Gallery: `devtools/widescreen/index.html`. Exact scripts, simulator states,
inputs, traces, hardware batches and screenshots are under `round3/`; earlier
rounds remain immutable historical evidence. All commits and artifacts are
local. No push, tag or public publication was performed.

## Round 4: rebased on v0.4.0 final (2026-10-19)

Rebased onto `release/v0.4.0-candidate` (`1c7e734`, release-v040-final2:
Off/Auto frameskip, library teardown fix, HLE card fix). The preset struct now
holds both `frameskip` and `widescreen`; the flat-2D and SotN entries keep
widescreen hidden (`e91e239`). With widescreen off, the six-state 600-frame
GPU command traces from the release core and the rebased core are identical.
The test build is `widescreen-11-split` (counter 4099). Old cost checks and New
picture checks for the bundles that haven't been checked on hardware are still
pending: the Old queue is reserved for v0.4.0 and the New is in a user playtest.
The per-console hide decisions will be recorded in `game_presets.h` once the
cost checks have run.
