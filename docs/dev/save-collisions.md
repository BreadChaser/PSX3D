# Save names that belong to one game (harsh review M2)

Branch `t3/save-collisions`, from `t3/safety` (f24ad7b). Answers item 5 of
the review's Top 10 ([round 1](harsh-review-r1.md) M2, scope set in
[round 2](harsh-review-r2.md)): collisions are **prevented**, not only
reported. Frontend only: the core's code keeps safety-2's addresses.

## The problem

Memory card 1 (`saves/<name>.srm`), states and the Continue save
(`states/<name>.stN`, `.sta`, thumbnails), per-game settings
(`games/<name>.ini`) and the cheat list are all named after disc 1's file
name without folder and extension. `roms/Game A/game.bin` and
`roms/Game B/game.bin`, or two `track01.bin`, therefore used the same files:
one game's Continue save was offered to the other, and each overwrote the
other's states. The library and the folder browser could also pick
different names for the same disc (disc 2 of a group, a disc named by a
playlist).

## What PSX3D does now

`psx3d/source/saveid.c`. Every save name has an owner record next to its
memory card, `saves/<name>.id` (text):

```
# PSX3D: the game these saves belong to
version=1
size=632083536          disc 1's data file (the .bin a .cue names, the .chd ...)
first=SCUS94900         serial when started from disc 1
serial=SCUS94900        every serial it started with (all discs seen)
disc=/roms/crash bandicoot (usa)/crash bandicoot (usa).bin   for people only
```

**Same game** means: the same disc 1 serial when both are known; otherwise
the same disc 1 data size, or a serial the owner has started with. So a game
moved to another folder keeps its saves (size), and so does a game converted
from `.bin` to `.chd` (serial). Paths are never compared. A serial is four
letters and five digits; the core's `SLUS99999` stand-in and names made from
the EXE do not count.

**When a game starts** (`emu.c` run_game):

1. Before loading: the name is the library's (disc 1's file name; the
   folder browser uses the library's name for any disc of a library game,
   see below). If the owner record's size differs, a name of this game's own
   that matches its size is used right away, so its own settings load.
2. After loading, before any memory card, state, Continue save or cheat is
   read: the serial the core read confirms the owner. If the name belongs to
   another game, this game uses `<name> [<serial>]` (`<name> [<8 hex>]`, a
   hash of the size, without a serial; `-2` ... `-99` if even that is
   taken, then one with a hash of where the disc is; each recorded like the
   first). Made the first time, found again every time after. If the name
   changed after loading, the per-game settings are switched to the right
   file before the game runs.
3. The first time a separate name is made, the player sees: "Saves kept
   apart from another game with the same file name". The log says which.

The other game's files are never read, written, moved or renamed.

**Saves from before this version** have no `.id`. They belong to the first
game started with that name, which records itself. One exception keeps a
legacy collision fair: if the library shows another game with the same file
name, this game started from disc 1 has a real serial, and the memory card
holds saves of other games only (save codes like `BASLUS-00594` on its
directory), the name is left for the other game, unless every other game
with that name already has a name of its own. Unique file names (the common
case) are adopted unconditionally: zero migration, the same files as before.

**Library and folder browser agree.** For a disc started from the browser,
PSX3D looks it up in the library: one of a game's discs, a file a game's
playlist names, or a file a cue sheet next to it names. Found: the library's
name (disc 1's), so disc 2 of a group shares the group's memory card,
settings and states. Not in the library: the file's own name, and
`X (Disc 2)` next to `X (Disc 1)` takes disc 1's name, as the library does.

## Cost

Per game start: one small file read, a stat of the disc data file and a
read of the cue sheet/playlist (≤16 KiB) before loading; after loading a
second read, and a small crash-safe write only when the record changes (the
first start of each game after the update, a new serial). A folder read of
`saves/` only when the owner's size differs. Nothing per frame. `saveid.o`
is built `-Os` (6.1 KB code, 20 KB bss).

## Limits

- **Multi-disc via the browser.** Disc 2 started alone shares the group's
  name, as required, including its Continue save and states. A state saved
  while disc 1 was inserted, loaded with only disc 2 present, cannot be
  told apart (the core stores the disc index, not the disc). The library
  (playlist) is the reliable way to play multi-disc games; started there,
  the core restores the right disc. A follow-up could start such browser
  launches through the library's playlist at the chosen disc
  (`set_initial_image`).
- Legacy collisions: when both games' saves sit on one shared `.srm` (the
  old behaviour mixed them), the second game starts with an empty card under
  its new name; its old blocks stay on the first game's card (untouched).
- A cut-off first `.id` write (power loss in that moment) leaves no record;
  the next start records again.
- If `saves/` cannot be written, no record exists and nothing can be
  protected (saves fail there anyway).
- A different game with the same file name and exactly the same disc 1
  data size counts as the same game when there is no disc 1 serial to tell
  them apart: no serial at all, or the owner was first started from its
  disc 2 (the record then knows only disc 2's serial; requiring a serial
  match there would split the owner's own disc 1 launch from its saves).
  Found by the review round below; kept as a documented limit.
- Two discs of different games with the same serial count as one game.
- Renaming a file still starts a fresh save name (the old files stay); no
  migration UI.
- Covers and the generated playlist file are still named after disc 1's file
  name (cosmetic; the playlist is rewritten at each start).

## Tests

| Suite | What runs | Result |
|---|---|---|
| `psx3d/tests/saveid/run.sh` | real `saveid.c` + `safefile.c` on fixture folders against a stand-in library, ASan/UBSan/LeakSanitizer: the reviewer's Game A/Game B `game.cue`/`game.bin` (library, browser .cue and .bin, outside the library), generic `track01.bin`/`game.bin` with and without serials, renamed files (new name, name taken by another game, renamed back, case), moved and converted games, multi-disc groups (browser disc 2 first, library playlist, playlist games, loose `(Disc 2)` next to `(Disc 1)`, two editions with the same disc names), legacy saves without `.id` (unique name, card of another serial, twins with card evidence for one/both/none/a third game, damaged card), damaged/interrupted/unwritable records, taken alias names, every numbered name taken, 20 games with one name, serial spellings, memory card directory reading | 257 checks |
| `psx3d/tests/saveid/mutations.py` | 15 deliberate breaks of `saveid.c` | all caught |
| save, library, host, dev, settings, presets suites | existing | pass |

Review: an independent GPT-6 Luna review of the change found two cases:
the record made by a disc-2-first launch (above, kept as a limit) and an
unrecorded last-resort name once all numbered names were taken (fixed in
`bc0ab8c`, test and mutation added).

Azahar (exact CIA, private profile): Crash adopts its existing saves (`.id`
written, files unchanged); with a fake `.id` naming another game and a
Continue save of "that game" present, Crash goes straight into the game (no
Continue prompt), saves as `Crash Bandicoot (USA) [SCUS94900]`, the OSD
message is logged, all original files byte-identical; the next start finds
the own name before loading; with the real `.id` back the Continue prompt
of its own saves appears again.

Layout: `tools/layout/check.sh` PASS; `tools/layout/compare.py` against
safety-2: every core and library function at the same address. The frontend
moved (emu.c +96 bytes; video and later frontend files +6 KB), hence an Old
A/B/A, below.

## Old 3DS (2026-10-20, session `devtools/save-collisions/hw/old-1`)

Build `save-collisions-2` (executable `36bb207`, CIA ad2f2a06…, title
version 4100). Waited for `lock.old` behind three other workers, started only
with the Old idle in the library on release-v041-rc4, one install per build,
no retries. Only spare slot 9 was written; slot 2 never touched.

**Collision on the console** (remote `put` writes only into `saves/` and
`states/`, there is no game upload in this build, so the colliding game is
simulated with an owner record, as in Azahar), Crash Bandicoot (USA):

| Step | Result |
|---|---|
| first start, no `.id` | adopted: `.id` with Crash's real size 632083536 and SCUS94900; slot 9 loads; slot 9 byte-identical |
| fake `.id` (another game owns the name) + load slot 9 | refused: "Slot 9 is empty" (the new name has none); log "belongs to another game (SLUS00001, 1234 bytes)", "saves as Crash Bandicoot (USA) [SCUS94900] (new)", OSD line; Crash's own per-game screen setting dropped to the default under the new name; original slot 9 byte-identical |
| cold boot, save slot 9, load slot 9 | written as `Crash Bandicoot (USA) [SCUS94900].st9` (+ thumbnail); original slot 9 byte-identical |
| real `.id` back, load slot 9 | the original slot 9 loads; byte-identical |

Name check and record after loading took 0.13 s including the first record
write (log timestamps).

**Speed**, A/B/A safety-2 → save-collisions-2 → safety-2, the 13 heavy
scenes (slots 5–7, load only), speed % / pictures per second:

| Scene (off) | A1 speed% / pict/s | B (save-collisions-2) | A2 | B vs mean(A1,A2) |
|---|---:|---:|---:|---:|
| Crash Bandicoot s5 | 86.0 / 20.5 | 86.6 / 20.6 | 86.0 / 20.4 | +0.74% |
| Crash Team Racing s5 | 77.5 / 23.0 | 78.0 / 23.1 | 77.5 / 23.0 | +0.66% |
| Ridge Racer Type 4 s5 | 79.9 / 24.3 | 80.1 / 24.4 | 79.6 / 24.2 | +0.41% |
| Castlevania - Symphony of th s5 | 97.5 / 59.1 | 97.8 / 59.2 | 97.5 / 58.9 | +0.32% |
| Silent Hill s5 | 80.2 / 19.3 | 80.3 / 19.3 | 80.1 / 19.3 | +0.13% |
| Silent Hill s6 | 69.9 / 21.2 | 69.9 / 21.2 | 69.9 / 21.2 | +0.01% |
| Silent Hill s7 | 70.7 / 16.8 | 70.7 / 16.7 | 70.6 / 16.7 | +0.07% |
| Gran Turismo 2 s5 | 51.6 / 13.6 | 51.8 / 13.7 | 51.6 / 13.6 | +0.41% |
| Tekken 3 s5 | 65.3 / 39.5 | 65.6 / 39.8 | 65.3 / 39.6 | +0.38% |
| Final Fantasy VII s5 | 100.1 / 15.2 | 100.1 / 15.2 | 100.0 / 15.2 | +0.04% |
| Klonoa - Door to Phantomile s5 | 86.5 / 52.4 | 86.5 / 52.4 | 86.4 / 52.3 | +0.07% |
| Crash Bandicoot 2 - Cortex S s5 | 73.2 / 22.3 | 73.3 / 22.2 | 73.1 / 22.1 | +0.15% |
| Need for Speed - High Stakes s5 | 37.0 / 11.8 | 36.9 / 11.7 | 37.1 / 11.8 | -0.26% |

worst off: -0.26%

| Scene (auto) | A1 speed% / pict/s | B (save-collisions-2) | A2 | B vs mean(A1,A2) |
|---|---:|---:|---:|---:|
| Crash Bandicoot s5 | 97.0 / 14.6 | 97.4 / 14.6 | 96.9 / 14.5 | +0.38% |
| Crash Team Racing s5 | 88.6 / 15.2 | 89.1 / 15.1 | 88.5 / 15.2 | +0.60% |
| Ridge Racer Type 4 s5 | 87.9 / 17.5 | 88.5 / 17.6 | 88.2 / 17.4 | +0.55% |
| Castlevania - Symphony of th s5 | 100.0 / 53.4 | 100.0 / 53.6 | 100.0 / 53.2 | +0.04% |
| Silent Hill s5 | 84.3 / 15.1 | 84.5 / 15.1 | 84.1 / 15.3 | +0.33% |
| Silent Hill s6 | 81.4 / 15.1 | 81.7 / 15.2 | 81.5 / 15.2 | +0.36% |
| Silent Hill s7 | 73.5 / 15.2 | 73.3 / 15.3 | 73.5 / 15.2 | -0.23% |
| Gran Turismo 2 s5 | 52.1 / 12.3 | 52.4 / 12.2 | 52.1 / 12.3 | +0.70% |
| Tekken 3 s5 | 90.6 / 15.1 | 90.7 / 15.2 | 90.6 / 15.2 | +0.12% |
| Final Fantasy VII s5 | 100.0 / 15.2 | 100.0 / 15.2 | 100.0 / 15.2 | -0.00% |
| Klonoa - Door to Phantomile s5 | 100.0 / 15.1 | 99.5 / 15.3 | 99.4 / 15.2 | -0.19% |
| Crash Bandicoot 2 - Cortex S s5 | 90.2 / 16.0 | 90.9 / 16.2 | 89.9 / 16.1 | +0.96% |
| Need for Speed - High Stakes s5 | 37.5 / 11.0 | 37.5 / 10.9 | 37.5 / 11.0 | -0.02% |

worst auto: -0.23%

Mean off +0.24%, Auto +0.28%: all 26 rows within the ±1% gate (the frontend
code after `emu.c` moved; core and libraries did not).

The session's first driver run stopped after B with "disk quota exceeded"
(the host's `/tmp` tmpfs user quota, filled by other workers' simulator
folders; the driver served the CIA from a temporary copy there). Resumed with
its temporary files on the home disk (A2 and the restore); between the two,
the Old stayed idle on the candidate and no other session ran on it.

Final: Old on release-v041-rc4, library idle, verified (session
a66d8b560d6). Left on its SD card: `.id` records of the games the benches
started (release builds ignore them; they name the right owners) and
`Crash Bandicoot (USA) [SCUS94900].id/.st9/.st9.thm` from the test (unused
while Crash owns its name). The New 3DS was not contacted.
