# Game presets

PSX3D is curated: a game's preset decides which features it gets on each
console and how its technical settings start out. A game without a preset
shows every option, like a normal emulator; only console limits hide things
there (for example, rewind needs a New 3DS).

Presets are data, one readable file per game, in `psx3d/romfs/presets/`. No
C code changes to add or change a game.

## A preset file

```ini
title = Gran Turismo 2
version = 1
serials = SCUS94455, SCUS94488
; Arcade (SCUS94455) and Simulation (SCUS94488) discs.
[old]
; Widescreen costs Old 3DS speed: offered with a warning.
widescreen = warn
[new]
psxclock = 80   ; inert until overclock presets are enabled
```

- **One file per game**, named after the title in lower case with hyphens
  (`gran-turismo-2.ini`). It covers both consoles and every regional release
  and disc of that game: `serials` lists them all. Split a region into its own
  file only when it genuinely needs different values, and say why in a comment.
- **Required keys** at the top: `title`, `version`, `serials`.
- **Sections**: keys at the top apply to both consoles; `[old]` and `[new]`
  hold console-specific differences. `[common]` returns to the top section.
- **Sparse**: list only keys that differ from the console default. An absent
  key is the console default. `tools/presets/check.py` rejects a key whose value
  equals the default for its section (top = both consoles, or a console
  section), or a console section that repeats the top value.
- **Comments** start with `;` or `#`, also after a value. Put the source and
  confidence of every decision in a comment next to it.
- Serials are the disc ID without punctuation: four capital letters, five
  digits (`SLUS00067`). A serial may appear in only one file.

## Keys

| Key | Values | Old default | New default | Meaning |
|---|---|---|---|---|
| `title` | text | | | Readable title (top only) |
| `version` | 1, 2, … | | | File version (top only), see [Versions](#versions) |
| `serials` | list | | | Every disc serial of the game (top only) |
| `curated` | `yes` | no | no | The preset owns the game's option list: "Skip frames when slow" is hidden and set by `frameskip` (top only) |
| `fast_forward` | `no` `yes` | yes | yes | Fast forward available |
| `rewind` | `no` `yes` | no | yes | Rewind available (Old 3DS can never rewind) |
| `touch_controls` | `auto` `on` `off` | auto | auto | Touch buttons: by need, the full console layout, or none |
| `l2r2` | `no` `yes` | yes | no | Game needs L2/R2 touch buttons (New has ZL/ZR) |
| `l3r3` | `no` `yes` `analog` | analog | analog | Game needs L3/R3; `analog` = only while the game uses analog mode |
| `stick` | `no` `yes` `analog` | analog | no | Old 3DS touch right stick |
| `touch_l2` `touch_r2` `touch_l3` `touch_r3` `touch_x1` `touch_x2` | `hide` `b` `y` `select` `start` `up` `down` `left` `right` `a` `x` `l1` `r1` `l2` `r2` `l3` `r3` `ff` `ff_hold` `rewind` | from the needs | from the needs | Starting binding of a touch slot |
| `frameskip` | `off` `auto` | auto | auto | Frameskip of a curated game (needs `curated = yes`) |
| `widescreen` | `hidden` `on` `warn` `off` | on | on | `on`: offered when the game has a widescreen bundle; `warn`: offered with "Widescreen can lower the speed"; `off`: held back, Settings says "Widescreen is off on Old 3DS to keep the speed up"; `hidden`: never offered ("This game has no widescreen fix"). `warn`/`off` need a bundle |
| `psxclock` | 30–100 | auto | auto | PS1 CPU clock in % (57 = auto). See [PS1 clock](#ps1-clock) |

The widescreen codes stay in `psx3d/romfs/widescreen/<SERIAL>-<exe hash>.ws`
([format](../tools/widescreen/README.md)); the preset only decides whether
they are offered. A game with a bundle and no preset gets `on`.

## PS1 clock

`psxclock` entries are data only for now: they are parsed and shown in the
host tests, but the app applies them only when built with
`-DPSX3D_PS1_OVERCLOCK_PRESETS` (off). A clock entry does not make a game
curated. Measurements: [PS1 overclock](dev/ps1-overclock.md).

The core sets the clock of some games itself (`cycle_multiplier_overrides` in
`core/libpcsxcore/database.c`: Brave Fencer Musashi, Digimon World, Legend of
Legaia and others). An explicit clock would replace that fix, so a preset never
sets `psxclock` for those serials: `check.py` rejects it, and the app drops a
preset clock when the core reports its own override for the disc.

## Which value wins

Settings come in two groups. Each setting has one fixed group.

**Technical** settings decide whether a game runs right and fast:
console default < your all-games setting < game preset < your setting for this game.

**Personal taste** settings are how you like to play: the preset only changes
the starting point, and your all-games choice beats it:
console default < game preset < your all-games setting < your setting for this game.

A preset can also **hide** or **hold back** an option in either group (a
capability): then it is not offered at all and nothing overrides it.

| Group | Settings |
|---|---|
| Technical | Game CPU speed (`psxclock`), Skip frames when slow (`frameskip`), speed hacks and compatibility options (Simpler CPU timing, Simplify CPU loads, Skip code safety checks, Faster 3D calculations, Simpler 3D clipping, Flicker fix, Safe drawing, Accurate CPU cache/errors, Faster disc loading, Read disc ahead) |
| Technical, capability | Fast forward and rewind available, Widescreen offered / warned / held back, touch control needs (`touch_controls`, `l2r2`, `l3r3`, `stick`), curated option list |
| Personal taste | Screen (4:3 / Stretch / Widescreen choice), Smooth picture, PS1 colour shading (dithering), button and touch mapping (`touch_*` slots, touch stick, click side), hotkeys, sounds, music, audio stretch, Sound echo, disc music/voices, FPS counter, 3D mode |

Notes:

- The touch-control needs pick the **starting** binding of each touch slot.
  The slot binding itself is button mapping (taste): a slot you mapped, for
  all games or this game, beats the preset. This is how v0.4.1 already worked.
- For a curated game, "Skip frames when slow" is hidden (capability) and the
  preset value applies; your saved frameskip choices are ignored for that game,
  as in v0.4.1.
- Today's presets set only technical keys and touch-slot starting points. If a
  preset ever sets a taste value such as the screen mode, note that
  `config.ini` currently writes every frontend value (smooth, show_fps, stereo,
  stretch, sounds, music, audio_stretch, maps) on save, so it cannot tell "you
  chose this" from "default". Make those entries sparse before shipping a taste
  preset, so an untouched setting does not hide the preset's value.

Your settings files only ever store your own changes: the per-game file
(`sdmc:/3ds/PSX3D/games/<game>.ini`) stores a value only when it differs from
your all-games value, and nothing copies a preset value into either file. A
new preset version therefore takes effect for everything you did not change.

## Versions

- `romfs/presets/VERSION` holds the pack version, one number. Bump it whenever
  any preset changes.
- Each file has its own `version`. Bump it whenever that game's entry changes.
- `check.py` compares with the last release tag (or `--base REF`): a file that
  changed needs a higher `version` than it had there, any change needs a higher
  pack `VERSION`, and the pack `VERSION` must be at least every file version.

## Updates without a new app (SD layer)

PSX3D also reads `sdmc:/3ds/PSX3D/presets/`, same format with its own
`VERSION`. There is no download code yet; a later updater, the user or the
remote `put` command can drop files there.

- An SD pack whose `VERSION` is missing or lower than the built-in pack is
  ignored entirely, so an app update is never overridden by stale SD files.
- For each serial the file with the higher `version` wins; on a tie the
  built-in file is used. SD files can also add games that have no built-in
  file.
- An SD file with any bad line (unknown key or value, bad section), or whose
  `version` is above its pack `VERSION`, is rejected whole; the game's
  built-in file is used. A bad line in a built-in file is logged and only that
  line is ignored.
- The log says which file was used for each game:
  `preset: SLUS00067 romfs:/presets/castlevania-symphony-of-the-night.ini version 1`.

## Where it shows

- The folder browser shows the pack version next to the app version
  ("Version 0.4.1 (game presets 1)").
- The developer `status` JSON has `preset` (`builtin`, `sd` or `none`),
  `preset_version` (the game's file version) and `preset_pack`.

## How it runs

At startup `game_presets_init()` reads both `VERSION` files and parses every
preset file once into a serial index. When the core identifies the disc
(the `CD-ROM ID:` log line, also after a disc swap) the serial is looked up;
nothing is read or parsed in the frame loop. The loader is compiled with `-Os`
(it runs once). The frontend links after the core (`psx3d/core-first.ld`), so
the loader's size cannot move the core.

## Checks

```bash
python3 tools/presets/check.py     # every file: keys, values, sparse, serials, versions, bundles
bash tests/presets/run.sh          # check.py, then the host tests below
```

`tests/presets/identical.c` compares every serial of the v0.4.1 table and
every widescreen bundle, on both consoles, against a frozen copy of the
v0.4.1 code (`tests/presets/legacy_v041.h`, `v041-ws-models.txt`): fast
forward, rewind, touch needs and slots, frameskip, curated, whether Widescreen
is offered and which note Settings shows. It also tests the SD rules (SD newer,
SD older, older SD pack, malformed SD file, SD-only game, bad built-in line,
serial listed twice).

## Adding a game

1. Copy a similar file, set `title`, `version = 1` and all its `serials`.
2. Add only what differs from the console defaults, with a comment giving the
   source and confidence.
3. Bump `VERSION`, run `bash tests/presets/run.sh`.
