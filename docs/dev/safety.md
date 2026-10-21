# Save and load safety (harsh review C1, C2, M1, M3, m5)

Branch `t3/safety`, from `t3/core-first` (6083ee9) plus the save-safety round
(e3c662d..aa55303, identity commits left out). It answers the first four items
of the review's Top 10 ([round 1](harsh-review-r1.md), corrected in
[round 2](harsh-review-r2.md)) and m5. **The emulator core's code is unchanged
and keeps core-first-2's addresses**: every fix lives in the frontend, the
core's stream table is replaced at run time, and two of its functions are
wrapped at link time.

## C1 — persistence

`psx3d/source/safefile.c` is the one primitive for memory cards, states and
thumbnails (settings use its text variant).

- **A good copy** is a file of exactly the expected size whose header the
  core would accept: `MC` for a memory card (what the PS1 BIOS checks),
  `STv4 PCSX` for a state (after an optional RetroArch header), the thumbnail
  magic and size. That is all "good" means; the rest of the content is not
  verified.
- **Writing** (`safe_write`): `<name>.tmp` written in 64 KiB chunks, every
  write and the close checked, size checked; then the current file becomes
  `<name>.bak` if it is good, or `<name>.bad` if it is not (a damaged file is
  never deleted and never becomes the backup), then the `.tmp` is renamed into
  place. FAT cannot rename over a file, hence the order. If a `.tmp` is the
  only good copy left (a cut-off first save, a failed repair), it is put in
  place first, so no write ever truncates the last good copy.
- **Reading** (`safe_load`) returns a new buffer holding the best good copy:
  `<name>`, else `.bak`, else a good `.tmp`; sources the caller could not use
  can be skipped. A rejected read therefore never touches the caller's data
  (the live memory card), and reads change nothing on the SD card. Other sizes
  are never read in part ("tolerant" reading is gone).
- **Repair** never rewrites its source: a `.tmp` is renamed into place, a
  `.bak` is copied through `<name>.rep` and stays as it is.
- **Discovery** (`safe_stat`): the state browser and the Continue prompt see a
  slot whose only copy is a good `.tmp`.

`psx3d/source/cards.c` (moved out of `emu.c`) handles every memory card:

- Card 1 is normally the core's SAVE_RAM, kept as `saves/<game>.srm`.
- Cards the core keeps in files of its own — **card 2 (on by default, the
  shared `pcsx-card2.mcd`)** and card 1 in "per disc"/"shared" mode — used to
  be written by the core in place, 128 bytes per frame, unchecked, and read
  whatever size was there. The core's `LoadMcd` is now wrapped
  (`--wrap=LoadMcd` at the core's partial link): PSX3D reads the card itself
  (best good copy), then clears the core's file name, so the core keeps the
  card in memory only, exactly as it already does for card 1, and PSX3D
  writes it like the `.srm`. Consequence: the HLE BIOS's `format` call does
  nothing on these cards, as it already did on card 1. Files with a DexDrive
  or VGS header stay with the core as before.
- A card is written only when it changed (checked every 180 frames). A card
  that could not be written stays **due**: it is never treated as written,
  is tried again every 30 s (at once on leaving, sleep, HOME and the pause
  menu), and the player sees "Could not save the memory card. Is the SD card
  full?". A card loaded from its backup whose repair failed stays due the
  same way (the review's "shadow after failed repair"). A damaged card
  without a good copy is not loaded: the game starts with an empty card
  ("Memory card file damaged: started with an empty card") and the file is
  kept, as `.bad` once the card is written. A card that could not be read for
  lack of memory is never written over.
- States report a failed save ("Could not write slot N"); a repaired load says
  whether the repair worked. A failed periodic Continue auto-save is retried
  after a minute and reported; sleep resets the auto-save timer only after a
  written save.

Not covered: power-loss behaviour inside the 3DS FS/SD controller (the
tests model operations, see below); the HOME Menu's RAM-only Continue
snapshot if the console sleeps while the HOME Menu is open; a damaged byte
inside an otherwise good-looking card or state.

## C2 — state parsing

The core reads and writes a state through its `SaveFuncs` table; the
libretro glue's own stream knew a buffer and a position but no length.
`psx3d/source/statefile.c` installs PSX3D's streams in that table (`emu_init`):
each knows its buffer's length, refuses any read, write or seek outside it
(negative and overflowing offsets included) **before touching memory**, and
remembers the refusal. `retro_serialize`/`retro_unserialize` are wrapped at the
final link (every PSX3D caller: states, Continue, rewind, replays): they honour
the size they are given, a save that did not fit fails, and before the core
restores anything a state is checked:

- header (optional RetroArch header), then the SPU block's size field (the
  only variable size before it) against the core's limits and the length;
- after the SPU block: the dynarec's block list (`ariblks`, size, data) and
  pad.c's 8 `PadDataS` records with their `saveSize` seeks, all inside the
  buffer. The size of the fixed records in between (SIO, CD-ROM, counters,
  MDEC) is learned from the first good state that has a block list; it is
  fixed for a build.

A state that fails the check, or that the core refuses itself (another
version), leaves the running game byte for byte as it was. A damaged record
the check could not see (only possible before the first good state of a run
taught the layout) is still refused by the stream; the core has then begun
restoring, so PSX3D restarts the game ("That save is damaged: the game was
restarted") rather than play on in a half-restored game that could save its
memory card. Loading tries the file, its backup, then a good `.tmp`, one
4.4 MB buffer at a time (the Old 3DS has ~3.5 MB of headroom in a game, so a
snapshot-and-restore copy of the running game is not affordable there).

The core's sources (`core/frontend/libretro.c`, `core/libpcsxcore/misc.c`)
are deliberately unchanged: changing them moved 1,539 of the core's 1,638 C
functions (+424 bytes at the start of the core), which would need its own
Old A/B/A; doing it from the frontend keeps every core address. The layout
constants come from the core's own headers (`statefile.o` is built with
`-I core/include`). The core's original stream functions are no longer used
by PSX3D.

## M1 — rewind

The worker swapped each capture in as the new base even when the memory for
its step could not be allocated; the next step back then applied an older
difference to the wrong base (0 → 1, lost 1 → 2, back from 3 gives a state the
game never had). A lost step now drops the history, which starts again at the
capture. The busy flag uses acquire/release.

## M3 + m5 — library

`library.c` reads every `.cue` (`FILE` lines) and `.m3u` (one path per line)
and hides exactly the files they name (relative, absolute, `sdmc:`, `.`/`..`,
either slash, case ignored, chains playlist → cue → bin). Unnamed discs next to
a playlist or a cue stay visible. The per-folder cap is 4096 entries (heap);
folders too deep, unreadable folders, the game and disc caps are counted,
logged and shown after a rescan ("Library updated: 52 games, 3 items skipped
(see log)"). One helper frees everything a game owns (the scan group leaked
before). The multi-disc playlist is written with the crash-safe text helpers;
if it cannot be written the previous one stays and the game is not started
with disc 1 only: "Could not write the disc list. Is the SD card full?".

## Tests

All on the host with AddressSanitizer and UBSan unless noted.

| Suite | What runs | Result |
|---|---|---|
| `psx3d/tests/save/run.sh` — safefile | real `safefile.c` with injected faults: a power cut after every step (first write, replace, replace over a damaged file, repairs), short writes at every chunk, a full card, failing closes, a rename failing at each of the replace's renames, read errors, every wrong size, the review's probes (3-byte file, sole `.tmp` repair with failing writes, `.tmp`-only discovery) | 270 checks |
| — cards | real `cards.c` + `safefile.c`: new card, wrong size, backup, failed repair stays due, failing write retried and reported, replay sessions, card 2 via the `LoadMcd` wrapper, header-format and `none` cards | 30 checks |
| — states | real `statefile.c` streams and wrappers driven by a model of `LoadState`/`SaveState`'s calls (sizes from the core headers): every section boundary ±1, bad SPU/block-list/pad sizes, seeks, RetroArch header, 1,500 fuzzed states | 3,978 checks, no partly restored |
| — mutations | 18 deliberate breaks of `safefile.c`, `cards.c`, `statefile.c` | all caught |
| `tests/settings/run.py` — rewind | rewind.c's own worker on a real thread with failing allocations, against a model of the valid history (the review's case, 200 random histories, a full ring) | pass; the old rewind.c fails it |
| `psx3d/tests/library/run.sh` | real `library.c` on fixture folders, LeakSanitizer | 495 checks |
| host, dev, settings suites | existing | pass |

ARM simulator (real core under qemu, `devtools/safety/simtools`):

- **Gate**: core-first core with its own stream vs. the same core with
  PSX3D's streams and wrappers: 13 heavy + 6 standard states × 600 frames and
  63 HLE cold boots × 1200 frames, draw hashes, checkpoints and serialized
  states identical (Crash 2 heavy stops at frame 575 on a debug assertion in
  `psxbios.c` in both builds alike).
- **Real `LoadState` fuzz**: 300 damaged copies of each of the 6 standard
  states, each ending right before an inaccessible page: no fault, every
  refusal leaves the serialized game identical, no state needed a restart.
  The same harness with the core's own stream faults on the first cut-off
  state.

Old 3DS results: see the report in `devtools/safety/REPORT.md`.
