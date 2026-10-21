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
  Frontend hot-path changes still require the Old gate below.
- Exact final CIA installed and booted in private Azahar profile, hidden workspace:
  New-model library 25 seconds (78 preset serials) and Crash state0 40 seconds. No abort;
  state load enters gameplay. Interpreter-only smoke; real-BIOS discovery then
  state restores HLE. Does not establish hardware speed or cold HLE correctness.

Logs are under devtools/presets-v2; final runs use *-final / *-final7 names.

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

The first Old A1/B/floor session was rejected: remote staging of config.ini
was overwritten by old in-memory settings during orderly self-update shutdown.
Live Settings reset and post-install visual/config checks are now required.
Corrected measurements are pending; no non-default floor is shipped.
