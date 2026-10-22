# Save safety 3 and save backups (harsh review 3: F1, F7)

Branch `t3/save-safety3`, from `origin/main` (v0.5.0). It fixes the save and
state findings F1 and F7 of [the third review](../../devtools/harsh-review/opus-r1.md)
(private) and adds the save backups the owner chose (design
`devtools/ui-mock/SAVE-BACKUP-DESIGN.md`, "Recommended": a ring of automatic
saves, an Undo row, memory card copies). **The emulator core's code is
unchanged and keeps v0.5.0's addresses; so does library code** (only the
frontend moved, see Layout). The one core build change: `Config` is exported
from the core's partial link (`Makefile` `EXTERN_SYMS`), so `statefile.c` can
put the BIOS setting back after a half-restored load.

## Storage

### Memory cards: checksums (F1b)

A card file is good when it has the exact size, the `MC` header **and** the
checksums the PS1 BIOS (and the HLE BIOS, `psxbios.c`) write: byte 127 of the
header frame and of the 15 directory frames is the XOR of the other 127
(`cards.c` `card_deep_ok`). `safefile.c` learned a second level for that:

- `safe_format_t.deep` checks a whole copy. A copy with a good header that
  fails it is **weak**.
- Loading takes the first good copy (`<name>`, `.bak`, `.tmp`); a weak one is
  used only when there is no good one (`safe_info_t.weak`, and the player is
  told). So a card with a good header and a garbage directory no longer wins
  over a good backup; it is kept as `.bad` when the backup is repaired back.
- Writing: the current file becomes the backup only when it is good, or weak
  while the backup is not good either. **A weak file never pushes a good
  backup out**; it is kept as `.bad`.

### Unreadable cards (F1e)

A card that can't be read (no memory) or is damaged without any good copy no
longer lets the game run on the core's empty stand-in in silence. The card is
locked (never written over) and, before the game runs, the player gets
"Memory card problem": A Try again, B Other choices → A Play without saving /
Use an empty card, B Quit. "Use an empty card" keeps the damaged file as
`.bad` at the first save. Remote test runs and replays answer "play on"
without asking.

### Card write order (F1c)

Cards are written before the 4.4 MB Continue state in both exit paths (Exit
and HOME → Close) and at ONSUSPEND (going to the HOME Menu), as sleep already
did. A power cut during the 2–3.5 s state write can no longer cost the game's
own save.

### States: a trailer with a checksum, the layout and the disc (F1b, F7a, F1f)

A state is `retro_serialize_size()` = 4,456,448 bytes; the core writes about
4.38–4.40 MB and the stream zero-fills the rest. PSX3D now puts a trailer
(`state_trailer_t`, `statefile.h`) at the very end of that spare space: magic
`PSX3Dsv1`, the bytes the core wrote, the layout after the SPU block, the disc
in the drive (file name, index, count), why the state was taken, and a
CRC-32. The core never reads there and v0.5.0 ignores it (its checks only
read what the core reads), so states stay loadable both ways.

- **Sealing**: `state_seal()` adds the CRC (standard CRC-32, slice-by-4, about
  0.1 s for 4.3 MB on an Old 3DS) in `write_file()`, i.e. on the auto-save
  writer thread for periodic saves. Rewind steps are never sealed.
- **Checking**: a sealed state is checked whole before the core reads a byte;
  any damaged byte makes it `STATE_DAMAGED`, so the loader takes the backup.
  States without a trailer (all made before) are checked as before.
- **Layout (F7a)**: the size of the records between the SPU block and the
  dynarec's block list (SIO, CD-ROM, counters, MDEC: private to the core) is
  no longer learned from the first state that loads. It is this build's own:
  `STATE_TAIL` = 37018 (the ARM build; the same in every state saved since
  v0.3, measured from all states in `devtools/`), and from PSX3D's first own
  save on, what that save measured (a mismatch is logged: "update
  STATE_TAIL"). A state whose block list is elsewhere (another build's
  records) or whose trailer names another layout is refused as another
  version before the core changes anything. Every pad record of this build's
  size must have `respSize`/`respSizeOld` ≤ 8 (pad.c indexes its 8-byte
  `rxData` with them), and no pad record is shorter than 64 bytes. (Every own save also gets its time
and an id in the trailer, see Save backups.) The
  reviewer's probe (a same-version state with a 16-byte longer tail) is now
  refused before and after PSX3D's first own save, and teaches nothing.
- **Disc (F1f)**: a state stores the disc's file name. On loading, the name is
  looked up in this launch's disc list (`emu.c` `disc_names`: the library's
  playlist lines, or the one disc started from the browser) and the state's
  index (`misc_save_data.CdromFrontendId`, in the state's hardware page) is
  set to that disc for the load, then put back (the caller's buffer and its
  checksum stay as they were). A disc that isn't in the list refuses the load
  ("That save is for a disc that is not part of this game here"); a state
  without a name keeps its index.

### Half-restored loads and rewind (F7b, F7c)

With the fixed layout and the checksum a damaged state is caught before the
core restores anything. If a record the check does not know is ever cut off
(the stream still refuses it), the game is restarted as before, now with the
BIOS setting it ran with (`LoadState` sets `Config.HLE` from the state first).
A rewind step that can't be loaded drops the history (`rewind_step()` returns
-1) and the game restarts when it was left half-restored; it never plays on.

### Save names fail closed (F7d)

- A save name another game owns (its record has another disc size and none of
  this game's names was found) gets no per-game settings while the game
  loads: `config_game_begin("<name> [?]")` (`?` can't name a FAT file), the
  real name's settings once `saveid_confirm` decided.
- A disc whose size can't be read is only "the same game" when its serial is
  in the record (or the record has no serials).
- An owner record that can't be written is reported ("Could not record which
  game owns these saves").

### Messages players can't miss (F1d)

Messages are queued (6), so "Save was damaged" is not wiped out by "Loaded
slot 2" in the same moment. Alerts about the player's data
(`emu_osd_alert`) stay 5.5 s and are also drawn as a band across the top of
the game picture, where the player looks. The bottom line has its own dark
strip above the touch pads (depth 0.9; the pads are 0.51–0.58).

## Save backups (owner's design)

`psx3d/source/history.c`. One folder per game, `sdmc:/3ds/PSX3D/backups/<save
name>/`, one file per entry, `<seq>-<kind>-<time><ext>` (+ `.thm` for a
state's picture), e.g. `00000012-start-1792650315.sta`. Entries are
self-describing (no index), put in place by one rename (states that already
exist) or written crash-safe (`safe_write`), so a power cut never leaves a
half entry under a final name.

**The 3DS can't read file times**: libctru's SD card `stat()` leaves
`st_mtime` at 0 (Azahar fills it in, which hid this in the first emulator
run). So nothing here uses file times: an entry's time is in its name (for a
state renamed in, the time its trailer records), and a state's trailer has
`saved` (time) and `id` (unique per save). Undo finds its save again by that
id, wherever it moved (the Continue point → its backup → the ring).

| Kept | When | Limit |
|---|---|---|
| ring: `timer`, `exit` | the Continue point's older versions (they fall off `.sta.bak`, renamed, no extra write) | 10 entries per game, all kinds together |
| ring: `start` | the Continue point when the player chooses Start game (and its backup) | |
| ring: `load`, `reset`, `restore` | the game as it is, before Load state, Reset game, putting a card back, going back to an older save, Undo | |
| ring: `save`, `delete` | a slot's save before it is saved over / deleted (renamed) | |
| card: `session` | the card as loaded when the game starts | newest 1 per card |
| card: `day` | the first card of each day | 7 days |
| card: `pin` | before the game writes a card with no saves left or half of them gone; the card before a put-back | 10 |

Limits: all games' ring entries together stay under **300 MB** (PSX3D never
asks the SD card for free space). Over it, games not played for 30 days keep
only their newest 2 first, then the oldest entries of any game go; the entry
just added is never dropped. Card copies (128 KB each) don't count.

**Safety copies** ("the game as it is now"): on New 3DS a fresh state is
written into the ring (~1 s). On Old 3DS a Continue auto-save up to 5 minutes
old is used instead of writing 4.4 MB (2–3 s), the owner's choice, unless it
is the very save being undone.

**Undo / Redo** (pause menu): after Load state, Save over a slot, Reset game,
Start game, deleting a slot or going back to an older save, the pause menu
has a row "Undo load" (etc.) with its age. A game-state undo first keeps the
game as it is (so Redo works), then loads the ring entry. Slot undo swaps the
slot's file with its ring entry (renames). One level: the next risky action
replaces it. The target is found again by its trailer id if the Continue
point it points at moved on (to `.bak`, then into the ring).

**Load state** has three pages (L/R): Slots, Auto-saves (the ring as the same
5×2 grid with pictures; the label says why, e.g. "Slot 3 before you saved
over it", and when), Cards (the copies of each card the game uses, with how
many saves each copy holds against the card now). A on an auto-save goes
back to it (the game as it is now is kept first); A on a card copy asks, then
puts it back and restarts the game (games keep their own view of the card's
directory). **Continue?** shows "Y an older auto-save" when the ring has
entries. Deleting a slot (X, X) moves it into the ring. The UI keeps the
existing pause/load-state style (not the new touch button bars).

## Tests

All on the host with AddressSanitizer and UBSan (`tools/build/host-tests.sh`).

| Suite | Checks | New here |
|---|---|---|
| `psx3d/tests/save` safefile | 270 | |
| — memory cards (`cards.c`) | 58 | checksum-valid fixtures; weak card vs good backup (loaded from the backup, kept as `.bad`, backup never pushed out, also when the file goes bad mid-session); weak card alone; damaged card locks until answered, retry, accept; session/day copies, pin before a format, put-back |
| — states (`statefile.c`, model core) | 4730 | reviewer's 16-byte-longer tail before/after the first own save (refused, nothing learned); interpreter-style shift; trailer layout tag; respSize/respSizeOld; sealed state: 300 single-bit flips anywhere refused before the core reads; no room for a trailer; disc mapping (-2..3, other disc refused, caller's buffer and CRC unchanged); a core with an extra record cut off → partly, `Config.HLE` back |
| — save backups (`history.c`) | 117 | adopt/take, times in names (fake clock), ring limit and thumbnails, card limits (the first copy of a day stays), budget (idle game first, then oldest, never the newest), strangers in the folder, crash-safe add |
| — mutations | 37 | each deliberate break of `safefile.c`, `cards.c`, `statefile.c` must fail a suite |
| `psx3d/tests/saveid` | 270 + 18 mutations | provisional name before load, unknown size decided by serial, unwritten record reported |
| `tests/settings` rewind worker | | a failing step returns -1, drops the history, a new history starts at the next capture |

ARM simulator (real core under qemu, `devtools/save-safety3/simtools`):

- **Gate**: `origin/main`'s `statefile.c` vs this branch's, same core: 13
  heavy + 6 standard states × 600 frames and 68 HLE cold boots × 1200
  frames: 92,400 per-frame draw hashes and 1,540 serialize/reload
  checkpoints identical, the final serialized state identical except the
  trailer (zero in the base). Every real state (no trailer) loads through the
  fixed `STATE_TAIL` check. Crash 2 heavy stops on the known debug assert at
  frame 575 in both.
- **Real `LoadState` fuzz**: 300 damaged copies of each of the 6 standard
  states, each ending right before an inaccessible page, as old-format files
  and as sealed states written by this build: no fault, no failure, no
  half-restored load; sealed: 260 of 300 refused as damaged before the core
  reads anything (the rest are cuts that only drop the trailer, or flips in
  the zeroed gap after the data, which load correctly).

Azahar (headless, the exact CIAs): four scripted runs through Save over,
Undo/Redo of save, load, reset, start game and delete, the Continue prompt,
the Auto-saves and Cards pages, New and Old mode (the 5-minute reuse rule).
They found the missing file times and a Redo-after-Undo-delete bug, both
fixed.

## Layout

`tools/layout/compare.py` against the shipped v0.5.0 ELF: core code (assembly
and C) and **library code at the same addresses**, core data/bss shifted by
whole pages. Two builds linked a new library function (`localtime_r`,
`strtoull`) and moved 227 / 274 library symbols; days are now computed from
the local 3DS clock directly and entry times parsed by hand. Frontend code grew ~26 KB (text) and moved, including hot
frontend code (renderqueue, gpu_cache, audio, video wrappers), so an Old 3DS
A/B/A against rc4 is required before merging. BSS +12 KB (the lists the
browser and the card copies use are on the heap only while needed).

## Not done / open

- The owner's library "Saves" page (design §5) and the button-bar version of
  these screens (ui-impl owns the bars).
- DexDrive/VGS card files still go to the core's in-place writer (F1g).
- `keep_damaged` still replaces an older `.bad` (F1g).
- Old 3DS memory with the periodic auto-save buffer held during a manual save
  (F7e): safety copies call `autosave_wait()` first, but a manual save right
  after opening the pause menu can still find less memory.
