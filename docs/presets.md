# Game presets

Presets are sparse data in `psx3d/romfs/presets/*.ini`, with separate Old and
New 3DS values. An absent option retains its normal default and user choices.
A preset never hides a setting: every setting stays visible and changeable
([docs/settings.md](settings.md)), and values a preset sets are marked `P`.
The legacy `curated` field is accepted as metadata and has no effect.
Defaults live in `defaults.ini` in the same folder (settings.md section 6);
it is not a preset and is never read from the SD card.

## Format and evidence

```ini
title = Example
version = 2
serials = TEST00001
[old]
nostalls = on
nostalls.source = CTR heavy scenes, Old 3DS A/B/A, no visible change
nostalls.confidence = medium
tested_with = 1
tested_with.source = Old 3DS session 2026-10-21, defaults-history/1.ini
tested_with.confidence = medium
[new]
l3r3 = yes
l3r3.source = Retail manual controls section, USA edition, other editions untested
l3r3.confidence = medium
```

Each file requires nonempty `title`, positive `version` and `serials` at the
top (or in `[common]`). Serials are four uppercase ASCII letters followed by
five digits, without punctuation. A serial must appear in only one file in
a pack. `[old]` and `[new]` override common values independently of section
order. Sections may be revisited, but a key may occur only once per section.
Comments begin with `;` or `#`, including after values. Files use ASCII.

Every set option requires `<option>.source` and `<option>.confidence` in the
same section. Confidence is `high`, `medium`, `low` or `unassessed`; source is
1–127 bytes. These apply to the file's listed serials and that section's
console(s). A console override replaces both value and evidence. Describe
which editions/scenes were checked and where regional behavior is extrapolated.
Distinguish incompatibility from performance policy and an unassessed
restriction. Free-form explanatory comments remain
welcome, but do not substitute for the validated evidence fields.

## Settings and Advanced

The menu is specified in [docs/settings.md](settings.md). Presets never hide
settings (the former **Show all settings** switch is gone). A small `P`
identifies a value the game's preset supplies; the help panel shows its
recorded confidence/source. `*` identifies a per-game override. X on a setting
removes that override. Reset this game removes all of the game's overrides;
**Reset to recommended** (Advanced, only in games tested on this console)
removes the Advanced overrides only. Reset all settings removes global choices
and keeps per-game overrides. Explicit button mappings retain their usual
precedence.

`tested_with = N` (per console, or common) says the file's values were tested
with defaults version N (`tools/presets/defaults-history/N.ini`). Such games
show "This game has tested settings…" on the Advanced page. When a default
later changes, `tools/presets/check.py` (run by `make` and CI) fails until
every tested file pins the old technical value
(`<key>.source = pinned at defaults N`) or is retested. A changed forced value
cannot be pinned: every tested file on that console must be retested.

## Keys

| Key | Values | Old / New default | Meaning |
|---|---|---|---|
| `fast_forward` | — | — | **Retired**: fast forward is always allowed. The checker rejects it; the runtime ignores it in older SD packs |
| `rewind` | `no`, `yes` | no / yes | Rewind availability (Old cannot enable it) |
| `touch_controls` | `auto`, `on`, `off` | auto / auto | Starting touch layout |
| `l2r2` | `no`, `yes` | yes / no | L2/R2 touch need |
| `l3r3` | `no`, `yes`, `analog` | analog / analog | Stick-click touch need |
| `stick` | `no`, `yes`, `analog` | analog / no | Old touch right-stick need |
| `touch_l2`, `touch_r2`, `touch_l3`, `touch_r3`, `touch_x1`, `touch_x2` | see below | inherited needs | Starting binding per slot |
| `frameskip` | `off`, `auto` | auto / auto | Skip drawing when slow |
| `auto_floor` | integer 1–60 | 15 / 15 | Auto target minimum pictures/s when the game supplies them |
| `widescreen` | `hidden`, `on`, `warn`, `off` | on / on | Bundle availability policy |
| `psxclock` | integer 30–100 | auto (57) / auto (57) | PS1 CPU clock percentage (New 3DS presets) |
| `controller` | `digital`, `analog` | digital / digital | Controller the game starts with |
| `alt_flip` | `auto`, `early`, `late` | from `defaults.ini` | Flicker fix |
| `gpu_slow_llists` | `auto`, `off`, `on` | from `defaults.ini` | Safe drawing |
| `cd_turbo`, `nostalls`, `nolddelay`, `gteregsunneeded`, `nogteflags`, `nosmccheck`, `icache_emulation`, `exception_emulation`, `nocompathacks` | `off`, `on` | from `defaults.ini` | Compatibility options (settings.md section 2.1) |
| `tested_with` | integer 1–9999 | none | Defaults version these values were tested with |

Touch values: `hide b y select start up down left right a x l1 r1 l2 r2 l3 r3
ff ff_hold rewind`. Button needs supply starting bindings; explicit user
mappings beat them. `analog` supplies a touch control only in analog mode.

Widescreen `on` offers an existing bundle, `warn` offers it with a speed warning,
`off` offers it per game only, with a note that it lowers the speed a lot (the
all-games Widescreen choice does not switch it on), and `hidden` withholds it. The patch format remains
[the widescreen bundle format](../tools/widescreen/README.md).

`auto_floor` is sparse: no shipped entry changes the default 15. Lower values
trade pictures for emulation speed; higher values can improve smoothness while
slowing emulation. Games that draw fewer pictures cannot meet the target.
There is no menu row; a developer per-load `o:auto_floor=12` overrides it
without saving. Status reports `auto_floor`.
Measurements/proposals require the owner's choice before shipping a new value.
See [the implementation and validation report](dev/presets-v2.md) for the
hardware results and their configuration checks.

`psxclock` applies on the New 3DS (the build defines
`PSX3D_PS1_OVERCLOCK_PRESETS`); measurements: [PS1 overclock](dev/ps1-overclock.md).
When two discs of one game need different values they get separate files
(Gran Turismo 2: Arcade has a measured clock, Simulation has none). It cannot
replace a clock fix in the core database. The checker rejects those serials,
and the runtime suppresses the preset if the core reports its own override.
See [the overclock research](dev/ps1-overclock.md).

## Regions

A preset file lists the USA serial and the PAL (`SLES`/`SCES`) and Japanese
(`SLPS`/`SCPS`/`SLPM`) serials of the same game, taken from redump data
(libretro-database) and DuckStation's game list. Rules:

- Region-independent values (controller type, touch needs, the 2D Screen policy)
  are shared by every serial of a file.
- Speed values (`psxclock`, hold-backs) and widescreen bundles belong to one
  executable (bundles match the serial and the executable hash), and PAL runs at
  50 Hz. A game that has such values keeps them in its USA file; its other
  regions get a separate `<game>-other-regions.ini` with `widescreen = hidden`
  and no `tested_with`, so their speed settings count as untested.
- A serial that has no listed counterpart is simply absent: nothing is guessed
  from the title.

**LibCrypt (PAL).** Some European discs (for example Ape Escape and CTR:
Crash Team Racing, `SCES`/`SLES` serials) carry the LibCrypt copy protection.
Without the disc's `.sbi` file the game can stop or misbehave, and PSX3D shows
"LibCrypt protected game with missing SBI detected". Put `<disc image name>.sbi`
(same name as the `.cue`, for example `Game.cue` and `Game.sbi`) in the same
folder. In a multi-disc set the core looks for `<disc image name>_1.sbi`,
`_2.sbi` and so on. A subchannel dump (`.sub`) works as well.

## Layer order and persistence

Technical settings use:

`defaults < user all-games < preset < user per-game`

Personal-taste settings use:

`defaults < preset < user all-games < user per-game`

| Group | Settings |
|---|---|
| Technical | CPU speed, frameskip, Controller, compatibility options, rewind availability, widescreen availability policy, touch needs |
| Personal taste | Screen size, smoothing, button/touch mapping, hotkeys, FPS counter |

The current preset schema sets technical values and button starting points;
it does not set screen, smoothing or FPS preferences. Adding a new taste key
must implement that second order. Existing touch mappings use it today.
All-games technical choices cannot beat an explicit preset: use a per-game
override. Options the preset does not set
continue to respect all-games and per-game values.

New `config.ini` saves contain only the user's changes, plus library paths and
format metadata (`settings_format=3`, `touch_slots=1`). Values of removed or
forced settings are ignored on load and never written again. Resolved defaults,
preset touch bindings and internal core synchronization are never copied into
it. A deliberate choice equal to the factory default is retained. Game files
store overrides; ordinary frontend/mapping choices equal to the inherited
all-games value are removed, while explicit technical option choices persist.

**Migration:** old versions wrote a dense global file with no intent metadata.
Every represented, supported legacy choice is conservatively treated as
explicit, including values equal to factory defaults. This preserves a user's
real choice instead of guessing and deleting it. Such a migrated file can
remain dense until the user resets all settings (controls can be reset
separately). This includes touch files without `touch_slots`: represented slot bindings
and right-stick choices remain explicit. Retired settings keep their existing
behavior. Newly saved files never introduce unrelated defaults.

## Versions and SD updates

`romfs/presets/VERSION` is a positive pack version. Each file has its own
version, at most the pack version. Bump a file version when changing it and
bump the pack whenever any file changes. The checker enforces this against
the last release tag or `--base REF`.

Players get new packs automatically (next section). A validated pack can
also be copied by hand to `sdmc:/3ds/PSX3D/presets/` while PSX3D is not
running. Remote `put` does not write presets or VERSION.

- Missing/invalid SD VERSION, or an SD pack older than built-in: ignore SD.
- Per serial, the higher file version wins; a tie uses built-in.
- Validate each complete file before publishing any of its serials. Any
  invalid line, metadata, serial, duplicate key/serial, evidence or out-of-pack
  version rejects the entire file, on both layers. Built-in fallback remains.
- Filenames are processed in ascending byte order. A same-layer cross-file
  serial conflict rejects the entire later file. The checker rejects such a
  pack for distribution. File-level fallback is deterministic, but activation
  is **not** transactional across the pack.
- Bounds shared with the checker: 256 `.ini` files per pack (overflow disables
  that layer); 2048 indexed serials across both layers; 26 serials per file;
  512 complete lines per file; 255 bytes per line including newline. A final
  line without newline is accepted within that bound. Numeric versions are
  1–999999. Read/allocation failure never publishes a partially parsed file.
- The rejection log identifies the file. The chosen game log includes its
  source path and version. Developer status reports `preset`,
  `preset_version`, `preset_pack`. The browser shows the pack version.

Parsing happens once at startup. Disc identification uses the serial index;
no SD access or parsing occurs in the frame loop. The loader stays at `-Os`.
Core-first linking protects core code addresses; frontend changes still need
the Old A/B/A gate.

## Automatic updates

Presets update without an app release; the player does nothing (no setting,
no button). Code: `psx3d/source/presetupdate.c`; test:
`psx3d/tests/presetupdate/run.sh` (with mutations).

**When.** At most once a day (time of the last finished check in
`sdmc:/3ds/PSX3D/presets-update.txt`), only while the library is idle, on a
cover-download worker thread with the same network lock and pauses as the box
art. A game start, HOME, sleep or exit stops it within ~10 ms; it never runs
while a game is loaded. Offline (no Wi-Fi, server unreachable) it looks again
after 30 minutes of library time, without writing the stamp. Every other
outcome, including errors, waits for the next day: no retry storm.

**What.** The app fetches
`https://github.com/BreadChaser/PSX3D/releases/download/presets/presets-d<D>-s<S>.json`,
where D is the app's `defaults_version` (defaults.ini) and S is
`PRESET_SCHEMA_VERSION` (`presetupdate.h`, bump it when preset_schema.def gains
keys or values an older parser would reject). Keying the file name by both
lets several app generations get their own packs from one release. The
manifest is flat JSON, at most 4 KiB:

```json
{"format": 1, "pack_version": 4, "schema_min": 1, "schema_max": 1,
 "defaults_version": 1, "file": "presets-d1-s1-v4.pack", "size": 21051, "sha256": "…"}
```

The pack is downloaded only if `pack_version` is higher than the pack in use
(SD or built-in), the app's schema is in `schema_min..schema_max` and
`defaults_version` equals the app's. The pack (≤ 512 KiB, written to the SD
card, never held in RAM) must have exactly `size` bytes and the SHA256. It is
unpacked into `presets.new/` (format in `tools/presets/pack.py`; only
`VERSION` and lower-case `*.ini` names, not `defaults.ini`, no duplicates,
VERSION must equal `pack_version`). The main thread then loads that folder with
the normal parser and installs it only if every file is accepted:
`presets/` → `presets.old/` (one generation kept), `presets.new/` →
`presets/`. A power cut between those renames is undone at the next start
(the previous pack comes back). The library shows **Game settings updated**
once; values apply from the next game start, never to a running game. Any
failure keeps the current presets and is only logged (`presets update: …`).
The validation costs one preset parse (≈0.2 s on the Old 3DS, once per new
pack, in the library).

**Security.** Presets only choose values from the fixed whitelist in
`preset_schema.def`, so the worst a forged pack can do is pick bad settings
for a game. Transport is HTTPS to github.com (certificate roots in
`romfs/certs.pem`; GitHub's download host uses ISRG Root X1), and the SHA256
in the manifest protects the pack. No signature: whoever can publish on the
GitHub repository could also publish a CIA. If packs ever carry code (cheats
or widescreen patches are PS1 memory writes), add an ECDSA P-256 signature
over the manifest (mbedTLS has it) with the public key in the app.

**Developer.** Startup switch `presets_url=http://<numeric LAN IP>:<port>/`
(also makes a check due now). Remote: `presets-update` (status),
`presets-update now [url=http://…/]`, `presets-update reset` (idle library:
deletes downloaded packs and the stamp, built-in presets, no further check
this session). Status reports `settings.presets_update` and `preset_pack`.

### Publishing a pack (owner)

Nothing is published by the tools; the owner runs these steps.

1. Change the preset files, bump each changed file's `version` and
   `romfs/presets/VERSION` (higher than every pack already out, including the
   one built into the current release), then `python3 tools/presets/check.py`.
2. `python3 tools/presets/pack.py --out /tmp/presets-pack` → prints the pack
   version, size and SHA256 and writes `presets-d1-s1.json` and
   `presets-d1-s1-vN.pack`.
3. First time only: create the release **without making it "latest"**, or
   `releases/latest/download/PSX3D.cia` (README, QR code) would point at it:
   `gh release create presets --repo BreadChaser/PSX3D --target main --latest=false --title "Game settings (automatic updates)" --notes "Data for PSX3D's automatic game-settings updates. Nothing to download by hand." /tmp/presets-pack/presets-d1-s1-vN.pack /tmp/presets-pack/presets-d1-s1.json`
4. Later packs: upload the pack first, the manifest last (an app reading the
   new manifest must find its pack):
   `gh release upload presets --repo BreadChaser/PSX3D /tmp/presets-pack/presets-d1-s1-vN.pack` then
   `gh release upload presets --repo BreadChaser/PSX3D --clobber /tmp/presets-pack/presets-d1-s1.json`
5. Check: `curl -sL https://github.com/BreadChaser/PSX3D/releases/download/presets/presets-d1-s1.json`
   shows the new version; `curl -sL …/presets-d1-s1-vN.pack | sha256sum`
   matches; `gh release view --repo BreadChaser/PSX3D --json tagName` (latest)
   still names the app release.
6. Undo a bad pack: publish a new, higher version with the old contents
   (consoles never go back to a lower version). Old pack assets can be deleted
   once their manifest no longer names them.

A pack whose version equals the built-in one is harmless (consoles report
"current"); it only becomes an update once its version is higher.

## Checks and adding a game

```bash
python3 tools/presets/check.py
bash tests/presets/run.sh
```

The runtime and checker share key vocabulary/bounds through
`psx3d/source/preset_schema.def`; a shared malformed-file corpus executes both.
Tests preserve the 200 v0.4.1 capability views for set values and exercise
version fallback, deterministic conflicts, sparse saves/migration, per-option
visibility, Advanced overrides, layer order and resets.

To add a game, create a lower-case hyphenated `.ini` file with title, version
and serials. Add only differing values with scoped evidence for each option;
checker authoring rules reject redundant defaults/common repeats. Explicit
runtime values are accepted even when equal to a default, so an update can
pin a policy deliberately. Bump VERSION and run the checks.
