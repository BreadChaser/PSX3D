# PSX3D settings — locked design

Status: **spec, awaiting the user's approval** (phase 1). Nothing here is
implemented yet. Once approved, this file is the contract: the menu must match
it (see [Spec-matches-menu test](#7-spec-matches-menu-test)), and no setting may
be added, moved, renamed or removed without changing this file in the same
commit.

Base: `release/v0.5.0-candidate` (`4bc03da`, presets-v2 merged). It replaces
[settings-ui.md](settings-ui.md) and the visibility/"Show all settings" parts of
[presets.md](presets.md) and [presets-v2](dev/presets-v2.md).

## 1. Principles (user decisions, 2026-10-21)

1. PSX3D runs on exactly two fixed consoles, Old 3DS and New 3DS. An option
   with one best value per console is **forced in code**. It is not a setting.
2. Settings exist only for **taste and comfort**. Technical options go into one
   category, **Advanced**.
3. Every remaining setting is visible and changeable. Nothing is hidden or
   locked by a preset. The presets-v2 **Show all settings** toggle is removed.
   Only physical limits hide a row: Old 3DS has no stereo 3D, no rewind and no
   bottom-screen side choice. Widescreen is offered only when a game has a fix.
4. Defaults are versioned data (`defaults.ini`). Presets record which defaults
   they were tested with, and a build check stops silent changes (§6).
5. A test proves the real menu equals this document (§7).
6. **Dithering ("PS1 colour shading") is forced On on both consoles** (user,
   2026-10-21). It is the original PS1 look, and Off only causes banding.
   Measured cost on the Old 3DS is about 0 (v0.4.0 A/B: Crash −0.4%, others ±0.3%).
7. Cheats live in the pause menu only. They are not part of Settings.

## 2. Menu tree

Open Settings with **X** in the library (scope: all games) or from the pause
menu (scope: this game). Both use the same tree. The header shows the scope
("All games" or the game title). Rows marked *library only* or *game only*
appear in one scope only.

```
Settings
├─ Display
├─ Sound
├─ Controls
│   └─ Button mappings  (sub-page, unchanged from v0.5.0)
├─ Saves
├─ Advanced
│   └─ (Compatibility group at the bottom of the same page)
└─ Reset this game  |  Reset all settings
```

Navigation stays as it is today: A opens or changes, B goes back, L/R cycles
categories, Left/Right changes values, and X on a row clears this game's
override. The `*` marker means "changed for this game", and `P` means
"value from this game's tested settings" (preset).

### 2.1 Machine-readable menu (checked by the test in §7)

Format: `page | label | key | values (first = shown order) | Old default | New default | consoles | scope | layer`.
`consoles`: both/old/new. `scope`: all = library and game; lib = library only;
game = game only. `layer`: taste or tech (§5). Values use the labels the
player sees.

```settings-menu
Display | Screen | screen | Original, Stretch, Widescreen | Original | Original | both | all | taste
Display | Smoothing | smooth | On, Off | On | On | both | all | taste
Display | 3D | stereo | Off, Screen depth, Real 3D | - | Screen depth | new | all | taste
Display | FPS counter | show_fps | Off, FPS, Detailed | Off | Off | both | all | taste
Sound | Game volume | volume | 100%, 90%, 80%, 70%, 60%, 50%, 40%, 30%, 20%, 10% | 100% | 100% | both | all | taste
Sound | Menu sounds | sounds | On, Off | On | On | both | lib | taste
Sound | Library music | music | On, Off | On | On | both | lib | taste
Controls | Button mappings | > | - | - | - | both | all | taste
Controls | Controller | controller | Digital, Analog | Digital | Digital | both | all | tech
Controls | Analog mode shortcut | analog_combo | Off, L1+R1+Select, L1+R1+Start, L1+R1+L3, L1+R1+R3, L3+R3 | L1+R1+Select | L1+R1+Select | both | all | taste
Controls | Stick response | analog_axis_modifier | Square, Round | Square | Square | both | all | taste
Controls | Bottom-screen buttons | touch_click_left | Right side, Left side | - | Right side | new | all | taste
Saves | Memory card 1 | memcard1 | Game save, Per disc, Shared, Off | Game save | Game save | both | all | taste
Saves | Memory card 2 | memcard2 | Per disc, Shared, Off | Shared | Shared | both | all | taste
Saves | Rewind | rewind | Off, On | - | Off | new | game | taste
Advanced | Frameskip | frameskip | Off, Auto | Auto | Auto | both | all | tech
Advanced | Game CPU speed | psxclock | Auto, 30 … 100 | Auto | Auto | both | all | tech
Advanced/Compatibility | Flicker fix | alt_flip | Auto, Early, Late | Auto | Auto | both | all | tech
Advanced/Compatibility | Safe drawing | gpu_slow_llists | Auto, Off, On | Auto | Auto | both | all | tech
Advanced/Compatibility | Faster disc loading | cd_turbo | Off, On | Off | Off | both | all | tech
Advanced/Compatibility | Simpler CPU timing | nostalls | Off, On | Off | Off | both | all | tech
Advanced/Compatibility | Simplify CPU loads | nolddelay | Off, On | Off | Off | both | all | tech
Advanced/Compatibility | Faster 3D calculations | gteregsunneeded | Off, On | On | Off | both | all | tech
Advanced/Compatibility | Simpler 3D clipping | nogteflags | Off, On | On | Off | both | all | tech
Advanced/Compatibility | Skip code safety checks | nosmccheck | Off, On | Off | Off | both | all | tech
Advanced/Compatibility | Accurate CPU cache | icache_emulation | On, Off | On | On | both | all | tech
Advanced/Compatibility | Accurate CPU errors | exception_emulation | Off, On | Off | Off | both | all | tech
Advanced/Compatibility | Disable automatic game fixes | nocompathacks | Off, On | Off | Off | both | all | tech
```

Rows marked as open questions in [REVIEW.md](../devtools/settings-spec/REVIEW.md)
(Game volume, Controller, Rewind placement, Auto picture floor, Fast forward)
are shown here in their **proposed** form. The approval round settles them, and
this block is edited before phase 2.

### 2.2 Row notes

- **Screen.** Original (renamed from 4:3), Stretch, Widescreen. In a game,
  Widescreen is listed only when the game has a revision-matched widescreen
  bundle and its preset is not `hidden` (2D games, no fix). The preset
  `widescreen` key keeps its meaning, but `off` no longer hides the choice:
  - `on`: offered; follows the all-games Screen choice.
  - `warn`: offered with the note "Widescreen can lower the speed on Old 3DS.";
    follows the all-games choice.
  - `off`: offered with the note "Widescreen lowers the speed a lot on Old 3DS.";
    the all-games Widescreen choice does **not** apply. Only an explicit
    per-game choice turns it on. This replaces the old "Show all settings"
    override with a plain per-game choice.
  - `hidden`: not offered (it cannot work).
  In the library, Widescreen means "use widescreen in games that have a fix".
- **3D** (New only). Screen depth is the default. Real 3D is labelled
  experimental in its help text.
- **FPS counter** (renamed from "Show FPS"). Detailed stays: it is the player's
  speed readout, not a debug tool.
- **Game volume** (new). Game audio gain before the DSP, not the system volume
  slider. Applied through the DSP channel mix at no CPU cost. Menu sounds and
  library music are unaffected.
- **Controller** (new; replaces analog guessing). Digital or Analog DualShock.
  Games that switch the pad themselves still do. The preset key `controller`
  sets the default for games that support analog but do not switch the pad.
  Unlike other control rows it uses the technical layer order (§5), because
  the right value depends on the game. Touch L3/R3 and the Old touch stick
  continue to follow analog mode.
- **Bottom-screen buttons** (New only): Right side or Left side.
- **Memory cards.** Same values and meaning as today (card 2 "Shared" is
  PSX3D-managed since the safety round). These take effect on the next game start.
- **Rewind** (New only, game only). Unchanged behaviour: it needs free memory
  and shows "Unavailable" when that runs out. The preset key `rewind` keeps
  only its Old 3DS meaning (impossible there). On New it is a plain choice,
  default Off.
- **Frameskip** (renamed from "Skip frames when slow"). Off or Auto; Auto keeps
  the 15 pictures/s floor.
- **Game CPU speed.** Auto = 57 (the real PS1). New 3DS presets set measured
  values (CTR 75, GT2 Arcade 80, SH 100, NFS 80). Help text: "Higher can raise
  the picture rate on New 3DS; on Old 3DS any change usually slows the game."
- **Compatibility** group. These are PCSX's hacks and accuracy switches, ordered
  from "usually harmless" to "experts only". Each keeps its existing help text
  and impact line (options.c `notes[]`). Their defaults are the general correct
  value per console. Presets set them per game when a test proves a gain (for
  example the speed-trades candidates: CTR and R4 Simpler CPU timing, and
  Crash 1 Faster disc loading). Those values are not applied until tested
  under §6.

### 2.3 Advanced page header

The first lines of the Advanced page, above the rows. The text is exact
(checked by the test):

| Context | Text |
|---|---|
| In a game **with** tested settings on this console | **"This game has tested settings. Changing them can slow it down or break it."** Rows set by the preset show `P`. Bottom row: **Reset to recommended**. |
| In a game **without** tested settings on this console | Small line: **"This game hasn't been tested yet; these are the standard settings."** No warning and no Reset to recommended row. |
| Library (all games) | Small line: **"Games with tested settings keep their tested values for these options."** |

"With tested settings" means that the game's preset file has `tested_with` for
this console (§6). A file that only carries a widescreen policy, or that was
never tested against the defaults, gets the "hasn't been tested yet" line,
although its policy still applies.

**Reset to recommended** (with confirmation) removes this game's overrides of
Advanced options only. Display, Sound, Controls and Saves overrides stay. The
existing top-level **Reset this game** / **Reset all settings** keep their
current behaviour, minus the removed keys.

## 3. Complete option inventory and fate

The table covers every option that exists in `release/v0.5.0-candidate`. Core
options are listed as the core registers them on 3DS (strings in the v0.5.0
pre2 ELF). "Shown today" means visible in the v0.5.0 menu with presets-v2
defaults.

Fates:
- **Setting**: stays as a menu row (§2).
- **Force**: value fixed in code, no row, saved values ignored.
- **Force/console**: different fixed values per console.
- **Dev**: developer switch or debug only; not in the player menu.
- **Remove**: option or row deleted outright.

### 3.1 Core options (`pcsx_rearmed_*`)

| Key | Current label | Shown today | Old default | New default | Fate | Reason / evidence | Preset sets it today |
|---|---|---|---|---|---|---|---|
| region | Game region | yes (CPU & compat.) | auto | auto | **Force** auto | The disc decides. Forcing NTSC on a PAL disc is a timing hack, not a choice we curate. | no |
| bios | BIOS | yes | auto | auto | **Force** auto | HLE is primary (user 2026-10-19). Auto already uses a real BIOS file when one exists, and no player needs to override that. | no |
| show_bios_bootlogo | — | no (hidden by `show_` prefix) | disabled | disabled | **Force** disabled | Boot logo only exists with a real BIOS; it delays start. | no |
| memcard1 | Memory card 1 | yes (Saves) | libretro | libretro | **Setting** (Saves) | User's taste: per-game, per-disc or shared saves. | no |
| memcard2 | Memory card 2 | yes (Saves) | shared | shared | **Setting** (Saves) | as above | no |
| cd_readahead | Read disc ahead | New only (Performance) | forced 256 (0 under HBL) | 12 | **Force/console** Old 256/0 (unchanged), New 12 | Old: CTR CD reads 1.3→0.35 ms/frame (`eb5a64e`). New: the core default 12 is never worse, and a larger value only uses memory. Nobody can judge this. | no |
| drc | — | no | enabled (auto via Luma) | same | **Force** | Chosen by SVC access. The slow-mode notice explains when it is off. | no |
| drc_thread | — | no | disabled | disabled | **Force** | already forced (`locked[]`) | no |
| psxclock | Game CPU speed | yes (Speed hacks) | auto | auto | **Setting** (Advanced) | Real per-game value (New overclock presets). | yes: 4 files, New (CTR, GT2 Arcade, SH, NFS) |
| dithering | PS1 colour shading | yes (Display) | enabled | enabled | **Force** enabled | User decision 2026-10-21; cost about 0 measured; Off bands SH fog. "Always" (force) also goes. | no |
| gpu_thread_rendering | — | no | forced disabled | forced enabled | **Force/console** (unchanged) | Old: syscore 30% slices made drawing wait (25 vs 36 fps). New: GPU worker +2.7–3.0% in SH, CTR, R4 (Crash even), 2026-10-18. | no |
| frameskip_type | Skip frames when slow | yes (Performance; hidden when a preset sets it) | auto | auto | **Setting** (Advanced, "Frameskip") | Real per-game choice; Off/Auto only (user 2026-10-19). | key exists, no file sets it |
| frameskip_threshold | — | retired | 33 | 33 | **Remove** (already retired) | Off/Auto only | no |
| frameskip_interval | — | retired | 2 | 2 | **Force** (internal) | Used only for fast-forward / legacy policy | no |
| display_fps_v2 | — | no | disabled | disabled | **Force** | PSX3D has its own counter | no |
| display_info | Debug: core messages | debug only | disabled | disabled | **Dev** (switch `debug`) | Troubleshooting only | no |
| fractional_framerate | — | no | auto | auto | **Force** | Frontend paces frames | no |
| alt_flip | Flicker fix | yes (Picture accuracy) | auto | auto | **Setting** (Compatibility) | Per-game fix | no |
| rgb32_output | — | no | disabled | disabled | **Force** | Frontend draws RGB565 | no |
| scale_hires | — | no | enabled | enabled | **Force** | 240-line screen | no |
| gpu_slow_llists | Safe drawing | yes (Picture accuracy) | auto | auto | **Setting** (Compatibility) | Per-game fix | no |
| show_overscan | — | no (hidden by `show_` prefix, by accident) | disabled | disabled | **Force** disabled | Overscan would not fit the 400-px screen with Original framing. Recorded here so it is a decision, not an accident. | no |
| screen_centering, _x, _y, _h_adj | — | no (removed) | auto | auto | **Force** auto | Removed earlier; GPU renderer centres. | no |
| neon_*, gpu_peops_*, gpu_unai_* | — | no | — | — | **Remove** (not built) | No such renderers on 3DS (`hidden_prefix`); the unai prefix is a leftover from the Software renderer removal. | no |
| show_gpu_peops_settings, show_input_settings | — | no | disabled | disabled | **Force** | RetroArch menu folding | no |
| spu_reverb | Sound echo | Old only (Sound) | disabled | forced enabled | **Force/console** — see REVIEW Q1 | New: forced on today. Old: off for speed, never measured. Measure on Old; if < 1% cost, force On on both. | no |
| spu_interpolation | — | no | forced simple | forced gaussian | **Force/console** (unchanged) | Gaussian costs real time at 268 MHz. | no |
| nocdaudio | Disc music | yes (Sound) | enabled (music on) | enabled | **Force** on | Off just removes music (dynarec-speed2 survey: removes audio, no real gain). | no |
| noxadecoding | Disc voices and effects | yes (Sound) | enabled | enabled | **Force** on | Off removes voices/music; same survey. | no |
| spu_thread | — | no | forced enabled (syscore granted) / disabled under HBL | forced disabled | **Force/console** (unchanged) | Old GT2 27.0→27.8, Crash +3% (`e5cd050`); New: it would share core 2 with the GPU. | no |
| analog_axis_modifier | Stick response | yes (Controls) | square | square | **Setting** (Controls) | Feel of the stick: taste. | no |
| vibration | — | no | — | — | **Force** off | No rumble motor | no |
| analog_combo | Analog mode shortcut | yes (Controls) | l1+r1+select | l1+r1+select | **Setting** (Controls) | Taste; some games use the default combo. | no |
| multitap | — | no | disabled | disabled | **Force** | One player per 3DS (gameshare round may revisit) | no |
| negcon_*, input_sensitivity, crosshair*, konamigun*, guncon* | — | no | core defaults | same | **Force** | No such controllers or lightguns on 3DS | no |
| icache_emulation | Accurate CPU cache | yes (CPU & compat.) | enabled | enabled | **Setting** (Compatibility) | Some games need it; some run faster without | no |
| exception_emulation | Accurate CPU errors | yes | disabled | disabled | **Setting** (Compatibility) | as above | no |
| 3ds_appcputime | — | no | syscore % (30) | 35 (unused) | **Force/console** (unchanged) | `MaxCpu` 30%: 50/80% starve the system (2026-10-17) | no |
| cd_turbo | Faster disc loading | yes | disabled | disabled | **Setting** (Compatibility) | Per-game: Crash 1 loads faster; NFS −7% | no |
| nocompathacks | Disable automatic game fixes | yes | disabled | disabled | **Setting** (Compatibility) | Experts only | no |
| nolddelay | Simplify CPU loads | yes (Speed hacks) | disabled | disabled | **Setting** (Compatibility) | Tekken gain now exact in the dynarec (`ea1bc94`); keep for odd games | no |
| nosmccheck | Skip code safety checks | yes | disabled | disabled | **Setting** (Compatibility) | Breaks SH boot (dynarec-speed2) | no |
| gteregsunneeded | Faster 3D calculations | yes | **enabled** (o3ds table) | disabled | **Setting** (Compatibility), per-console default | Old speed default since 2026-10-15; to be re-measured as part of defaults v1 (REVIEW Q6) | no |
| nogteflags | Simpler 3D clipping | yes | **enabled** | disabled | **Setting** (Compatibility), per-console default | as above | no |
| nostalls | Simpler CPU timing | yes | disabled | disabled | **Setting** (Compatibility) | Per-game trade: CTR +2.2/+6.3%, R4 +2.1%, NFS −7%, SH garage −4% (speed-trades) | no |
| psx3d_widescreen | — | no (driven by Screen) | 0 | 0 | **Force** (internal) | Widescreen only through bundles | no (`widescreen` policy key instead) |
| psx3d_stereo | — | no (driven by 3D) | disabled | from 3D row | **Force** (internal) | | no |

### 3.2 Frontend settings (`g_cfg`, `config.ini`, `games/*.ini`)

| Key | Current label (category) | Old default | New default | Fate | Reason | Preset sets it today |
|---|---|---|---|---|---|---|
| screen (+ stretch, core widescreen) | Screen mode (Display) | Original | Original | **Setting** "Screen" | taste | `widescreen` policy: 35 files |
| smooth | Smoothing (Display) | On | On | **Setting** | taste | no |
| stereo | 3D (Display, New) | Off (fixed) | Screen depth | **Setting** (New only) | taste | no |
| show_fps | Show FPS (Display) | Off | Off | **Setting** "FPS counter" | taste | no |
| touch_click_left | Bottom-screen buttons (Controls, New) | — | Right | **Setting** (New only) | taste | no |
| audio_stretch | Smooth audio when slow (Sound) | On (5%) | On (5%) | **Force** on | Net win on both; pitch-preserving version in progress (`t3/audio-pitch`), which removes the only downside. | no |
| sounds | Menu sounds (Sound, library) | On | On | **Setting** | taste | no |
| music | Library music (Sound, library) | On | On | **Setting** | taste | no |
| rewind | Rewind (Saves & general, New, game) | — | Off | **Setting** (Saves, New, game only) — REVIEW Q3 | memory-hungry comfort feature | `rewind` key: Old meaning only |
| fast_forward | Fast forward (Performance, Show-all only) | Allowed | Allowed | **Remove row**; FF always allowed — REVIEW Q5 | Every Old `fast_forward = no` entry is "unassessed", not verified breakage. The hotkey row already lets a player turn it off. | yes: 4 files (Old, unassessed: Ace Combat 3, Ape Escape, Omega Boost, Supercross Circuit) |
| o.auto_floor / psx3d_auto_floor | Auto picture floor (Performance, Show-all only) | 15 | 15 | **Dev** + preset key — REVIEW Q4 | User: per-game floors not decided, "need more testing"; not on the user's Advanced list | key exists, no file sets it |
| show_all_settings | Show all settings (Advanced) | Off | Off | **Remove** | User decision 3 | — |
| bottom_off | — (config only) | 1 | 1 | **Force** (already: load sets 1) | backlight off when nothing is shown | no |
| debug | — (config only) | 0 | 0 | **Dev** | shows Debug tools | no |
| Record audio, Test tone | Debug category | — | — | **Dev** (only with `debug`) | troubleshooting | no |
| touch_stick | Touch right stick (Button mappings, Old) | from preset `stick` | — | **Setting** (in Button mappings) | taste + preset starting point | `stick` key |
| input maps, touch slots, hotkeys | Button mappings | factory | factory | **Setting** (sub-page unchanged) | taste | `touch_*`, `l2r2`, `l3r3`, `touch_controls`: 6 `l3r3` files |
| (new) volume | — | 100% | 100% | **Setting** "Game volume" — REVIEW Q2 | user list | — |
| (new) controller | — | Digital | Digital | **Setting** "Controller" | CLAUDE.md item 13, user list | new preset key `controller` |
| games_dir, last_dir, last_title, settings_format, touch_slots | — | — | — | internal, unchanged | library state / format metadata | — |
| "Continue where I left off" | (removed 2026-10-19) | always on | always on | already forced | user | — |

### 3.3 Pause menu

These entries stay as they are and are not settings: Resume, Save state,
Load state, Swap disc (multi-disc only), Cheats, Settings, Reset game, and
Exit to library. **Record test clip** is a developer tool. Phase 2 checks that
it only appears with the developer remote/debug switch, and moves it behind
that switch if it does not.

### 3.4 Developer switches (`switches.txt`, remote `load`/batch)

These stay developer-only. They never appear in the menu and are never saved
(devswitch.c restores them before local play):
`remote token=`, `stats`, `noskip`, `frameskip=`, `fs1`, `fs3`, `clock=`, `cdra=`, `state=`,
`slider=`, `stereo=`, `pmu=`, `gldbg=`, `vramdump=`, `autosavetest=`, `record`/`record_seconds=`,
`replay=`, `nosputhread`, `nogputhread`, `acap`, `prof`, `aa`, `sbs`, `showright`, `gpu_async`,
`gpu_flush`, `pmuphase`, `pmuclass`, `rtfs*`, `ff`, `floataudio`, `frontend`, `fsmart`,
`wsbundle`/`wsscreen=`, `oldmode`, `o:<core option>=<value>` (any core option per load,
including forced ones, for A/B tests), and `o:auto_floor=N`.

Rule: a forced option may still be overridden **per load** with `o:` for tests.
That keeps the A/B workflow intact and is invisible to players.

## 4. Removed settings: what happens to saved values

- `config.ini` `core.<key>` and `games/*.ini` `o.<key>` entries for forced or
  removed options are **ignored** on load and dropped on the next save (the
  sparse save never writes them). A player who had set "Disc music Off" gets
  music back. This is intended.
- `show_all_settings`, `audio_stretch` and `fast_forward` / `preset_fast_forward`
  are dropped the same way.
- A per-game `preset_widescreen=1` (written by the old Show-all override)
  becomes an explicit per-game Screen = Widescreen.
- Old 3DS players who set `gteregsunneeded`/`nogteflags` keep their choice:
  those remain settings.
- `settings_format` goes to `3`. A migrated file is rewritten only when a
  setting changes, as today.

## 5. Layer order (from presets-v2, unchanged except where noted)

- **Taste** (`layer = taste` in §2.1): `defaults < preset < user all-games < user per-game`.
  Presets supply taste values only as starting points (touch bindings).
- **Technical** (`layer = tech`): `defaults < user all-games < preset < user per-game`.
  A game's tested value beats an all-games choice. The library Advanced header
  says so (§2.3). A per-game change beats the preset and shows `*`.
- **Forced** values are not layered. Only the developer `o:` switch can override
  them, per load.

## 6. Versioned defaults and `tested_with`

### 6.1 `defaults.ini`

New file `psx3d/romfs/presets/defaults.ini`, shipped in romfs. It holds every
setting in §2.1 and every forced value in §3, per console:

```ini
defaults_version = 1
[old]
frameskip = auto
psxclock = auto
gteregsunneeded = on
nogteflags = on
...
[new]
frameskip = auto
gteregsunneeded = off
...
[forced.old]
dithering = on
spu_reverb = off          ; pending REVIEW Q1
spu_interpolation = simple
cd_readahead = 256
...
[forced.new]
...
```

- The runtime reads its defaults **from this file**, replacing the scattered
  C tables: `options.c` `locked[]`, the `o3ds[]` table, `finish_register()`
  overrides, `config.c set_defaults()`, and the `OPTIONS` table in
  `tools/presets/check.py`. One source of truth. The parser is the
  existing bounded preset reader (`-Os`, startup only, no frame-loop cost).
- Keys use the preset vocabulary: short names with `on`/`off`/`auto` values.
  `preset_schema.def` gains every §2.1 technical key, so presets can set any
  Advanced option (they cannot today).
- Each released `defaults_version` is archived unchanged in
  `tools/presets/defaults-history/<N>.ini`, which is not shipped. The check
  requires `defaults.ini` to equal `defaults-history/<defaults_version>.ini`.
  Changing a default therefore means bumping `defaults_version` and adding a
  new history file.

### 6.2 Presets record what they were tested with

- A preset file gets `tested_with = <N>` per console, in `[old]` / `[new]` or
  in common for both. It means: "the values in this file were tested on this
  console with defaults N; every option it doesn't set was at its defaults-N
  value."
- `tested_with` also drives the Advanced header (§2.3). The legacy `curated`
  field is retired: the checker warns about it and the runtime ignores it, as
  today.
- Presets stay sparse. The checker's existing rule still applies: no key equal
  to the current default, unless it pins (below).

### 6.3 Build check (`tools/presets/check.py`, run by `make` and CI)

For every preset file `P` and console `C` with `tested_with = N` while the
current version is `M > N`:

1. Diff `defaults-history/N.ini` against `M.ini` for console `C`, including
   `[forced.C]`.
2. For each **technical** option that changed: `P` must either set it
   explicitly in `C` (a *pin*, allowed to equal the old default and marked
   `<key>.source = pinned at defaults N`), or `P` must be retested, meaning
   `tested_with` is raised to `M`. Otherwise the check **fails** and names the
   file, console, option, and old → new value.
3. A changed **forced** value cannot be pinned. Every preset on that console
   must be retested (`tested_with = M`). This is deliberately strict, because
   forced changes affect every game.
4. A changed **taste** default never fails: presets make no taste claims.

The check also fails when:
- `tested_with` is greater than `defaults_version`, or no history file exists
  for it;
- `defaults.ini` keys do not exactly match the §2.1 keys plus the forced list
  of §3;
- a history file changed after it was released (compared with the last
  release tag, like the pack VERSION rule).

Bumping `tested_with` also requires the normal per-file `version` and pack
`VERSION` bump. Test fixtures (`tests/presets/`) cover: a changed default with
a pinned preset (pass), unpinned (fail), retested (pass), a changed forced
value (fail until retested), and a taste change (pass).

### 6.4 First version

`defaults_version = 1` records the values in the §2.1 and §3 tables. Existing
files get `tested_with = 1` only where their values came from a real test
session with defaults equal to version 1:
- the New psxclock entries (ps1-overclock measurements, defaults unchanged since);
- the 35 widescreen policy entries **do not** get it: they are policies, not
  compatibility tests;
- the Old `fast_forward = no` entries are "unassessed" and do not get it.

The exact list is part of phase 2. A file without `tested_with` keeps working:
it just shows the "hasn't been tested yet" line.

## 7. Spec-matches-menu test

`tests/settings/spec.c` plus `tools/settings/spec_check.py`, run in
`tests/run` and CI:

1. The host harness that `tests/settings/navigation.c` already uses builds the
   real `settings_ui.c`, `options.c`, `config.c`, `presets.c` and `input.c` with
   the core's real option table (`libretro_core_options.h` registered through
   `options_register_v1`).
2. It walks every page in eight contexts: {Old, New} × {library, game with
   tested preset, game without preset, game with `widescreen = off`}. For each
   row it prints `page | label | key | all values (by cycling Left/Right and
   restoring) | default | scope | P/* markers`, plus the Advanced header line.
3. `spec_check.py` extracts the ```` ```settings-menu ```` block and the §2.3
   header table from this file. It expands them per console and scope, then
   diffs them against the dump. Any extra, missing, moved, renamed or
   re-valued row fails the test with the difference.
4. Coverage of the core: every option the core registers on 3DS must appear in
   the §3.1 table with a fate. A new core option, for example after a core
   change, fails until it is classified here. Every forced key in §3 must be
   absent from the dump.
5. `defaults.ini` keys must equal the spec's keys (§6.3), so the three
   (spec, menu, defaults) cannot drift apart.

The test does not check help texts or drawing; screenshots stay a manual
review step (remote `ui` capture).

## 8. Implementation notes for phase 2 (non-binding)

- Delete code: `show_all_settings`, `F_ALL`, `F_FF`, `F_FLOOR` rows, the
  Debug/Speed hacks/Picture accuracy/CPU & compatibility/Performance
  categories, the dithering "Always" value, and `hidden_prefix` entries for
  renderers that are not built.
- Keep layout rules: frontend-only changes; run `tools/layout/check.sh` and
  `compare.py`. An Old A/B/A is needed if hot frontend code moves. Settings code
  is cold, but `options_get` is called per option update. Keep it a table lookup.
- Old/New speed: forcing values that equal today's defaults changes nothing.
  The changes that alter defaults for some players are reverb (pending Q1) and
  readahead on New (already 12). Neither changes the test consoles' defaults.
- Update `docs/presets.md` (remove Show all settings, add `tested_with`,
  `controller` and the Compatibility keys) and `docs/settings-ui.md` (replace
  it with a pointer to this file).
