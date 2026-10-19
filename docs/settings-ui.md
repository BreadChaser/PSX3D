# Settings categories

Implementation commits `05e32fb` and `2f237a3` on `t3/settings-ui`, based on `main` `9417930`. Not merged or pushed.
Evidence: `/home/tony/Development/PS1_3ds/devtools/settings-ui/`.

## Navigation and appearance

One shared navigator replaces the separate global and pause-menu settings lists.
The top level fits on one bottom screen: Display, Performance, Sound, Controls,
Cheats (loaded game only), Saves & general, Advanced, Reset to defaults.
Each opens a separate list. Advanced opens short CPU & compatibility, Speed hacks,
Picture accuracy, Picture position (software only), and Debug (debug only) lists.
Controls contains the existing physical/touch mappings and analog choices.

B returns one level. Each category remembers its cursor separately, with separate
memory for global and game contexts. L/R cycles the main categories from inside
a page (nested Advanced/Controls pages count as their parent category). Touch a
row to select it and again to activate it. Scrollbars identify longer lists.
The existing fonts, dark blue background, four PlayStation accent colours,
animated list, control symbols and menu sounds are reused. All setting labels,
values, warnings and scope information stay on the bottom screen; the paused game
remains visible above without a settings overlay.

## Scope and compatibility

Every screen states `this game` or `all games`. The selected row also states its
actual scope. This matters because the previous pause menu already changed shared
options: core options, smoothing, FPS, stereo, audio and menu preferences remain
**all games**, even when accessed while paused. Making those into per-game options
would change existing persistence semantics, outside this navigation change.
Screen, renderer, rewind and controls retain their existing per-game storage.

`*` marks a game override. X clears the selected screen/renderer/rewind override
or individual physical/touch/hotkey mapping and immediately restores the inherited
value. Legacy `games.ini` screen overrides are detected and removed too. Y resets
all game controls with confirmation (global controls retain X reset).

Reset this game removes its user overrides and legacy screen entry, then resolves
the existing preset/console/global defaults. Global reset restores the console's
factory settings, including core overrides and mappings, while retaining the
library location/selection and other games' overrides. Both require a fresh A
press after a scope-labelled confirmation; B cancels. Saves and cheats are kept.
No keys, file paths, formats, option values, presets, or emulation algorithms change.

## Full option mapping

Ordinary picture choices stay in Display; frame admission and renderer choices
stay in Performance; sound and mappings are grouped by their purpose. Rare
accuracy settings and hacks move under Advanced, separated further so that
warnings and choices do not form another long combined list.

| Frontend option/action | Existing storage | Category | Scope / availability |
|---|---|---|---|
| Screen mode | `screen; legacy games.ini; global stretch + core widescreen` | Display | This game, or all games in library |
| Smoothing | `smooth` | Display | All games |
| 3D | `stereo` | Display | All games; New only |
| Show FPS | `show_fps` | Display | All games; existing detailed view retained |
| Renderer | `renderer` | Performance | This game, or all games in library; reopen to apply |
| Half-height 3D (faster) | `half3d` | Performance | All games; software only |
| Smooth audio when slow | `audio_stretch` | Sound | All games |
| Menu sounds | `sounds` | Sound | All games |
| Library music | `music` | Sound | All games |
| Bottom-screen buttons | `touch_click_left` | Controls | All games; same console/touch visibility |
| Button mappings | `map_*` | Controls / Button mappings | This game, or all games in library |
| Touch right stick | `touch_stick` | Controls / Button mappings | This game, or all games in library; Old only |
| Fast-forward / rewind hotkeys | `hotkey_ff; hotkey_rewind` | Controls / Button mappings | This game, or all games in library; preset/console limits retained |
| Continue where I left off | `autosave` | Saves & general | All games |
| Rewind | `rewind` | Saves & general | This game; supported New profiles only |
| Cheat list / Add code | `existing cheat files` | Cheats | This game only; library has no cheats page |
| Record audio / Test tone | `no persistent setting` | Advanced / Debug | Debug only; existing actions |
| Reset to defaults | `existing reset routines` | Top level | Context-specific; confirmation required |

The following table includes every core option that can be exposed in this 3DS
build. Existing renderer, parent-option and console filters still apply. The
brief warning shown beneath the selected row comes from `options_impact()`.

| Player label | Existing key | Category | One-line guidance |
|---|---|---|---|
| Game region | `pcsx_rearmed_region` | Advanced / CPU & compatibility | Usually leave Auto |
| BIOS | `pcsx_rearmed_bios` | Advanced / CPU & compatibility | Reopen the game to apply |
| Memory card 1 | `pcsx_rearmed_memcard1` | Saves & general | Reopen the game to apply |
| Memory card 2 | `pcsx_rearmed_memcard2` | Saves & general | Reopen the game to apply |
| Read disc ahead | `pcsx_rearmed_cd_readahead` | Performance | Can reduce loading pauses |
| Game CPU speed | `pcsx_rearmed_psxclock` | Advanced / Speed hacks | Lower may slow the game itself |
| PS1 colour shading | `pcsx_rearmed_dithering` | Display | PS1 colour shading |
| Skip frames when slow | `pcsx_rearmed_frameskip_type` | Performance | A smoother sound, fewer pictures |
| Start skipping below | `pcsx_rearmed_frameskip_threshold` | Performance | Higher skips sooner |
| Frames to skip at most | `pcsx_rearmed_frameskip_interval` | Performance | Higher can look choppier |
| Debug: core messages | `pcsx_rearmed_display_info` | Advanced / Debug | Debug only |
| Picture timing | `pcsx_rearmed_alt_flip` | Advanced / Picture accuracy | Usually leave Auto |
| Careful drawing order | `pcsx_rearmed_gpu_slow_llists` | Advanced / Picture accuracy | May be slower |
| Picture position | `pcsx_rearmed_screen_centering` | Advanced / Picture position | Software renderer only |
| Move picture sideways | `pcsx_rearmed_screen_centering_x` | Advanced / Picture position | Manual position only |
| Move picture up or down | `pcsx_rearmed_screen_centering_y` | Advanced / Picture position | Manual position only |
| Adjust picture height | `pcsx_rearmed_screen_centering_h_adj` | Advanced / Picture position | Manual position only |
| Older software drawing | `pcsx_rearmed_gpu_unai_old_renderer` | Advanced / Picture accuracy | Faster, less accurate |
| Transparency effects | `pcsx_rearmed_gpu_unai_blending` | Advanced / Picture accuracy | Usually keep On |
| Lighting effects | `pcsx_rearmed_gpu_unai_lighting` | Advanced / Picture accuracy | Usually keep On |
| Faster lighting | `pcsx_rearmed_gpu_unai_fast_lighting` | Advanced / Picture accuracy | Faster, may alter colours |
| Sound echo | `pcsx_rearmed_spu_reverb` | Sound | Off can improve speed |
| Disc music | `pcsx_rearmed_nocdaudio` | Sound | Usually keep On |
| Disc voices and effects | `pcsx_rearmed_noxadecoding` | Sound | Usually keep On |
| Stick response | `pcsx_rearmed_analog_axis_modifier` | Controls | Changes stick response |
| Analog mode shortcut | `pcsx_rearmed_analog_combo` | Controls | Controller shortcut |
| Accurate CPU cache | `pcsx_rearmed_icache_emulation` | Advanced / CPU & compatibility | More accurate, may be slower |
| Accurate CPU errors | `pcsx_rearmed_exception_emulation` | Advanced / CPU & compatibility | More accurate, may be slower |
| Faster disc loading | `pcsx_rearmed_cd_turbo` | Advanced / CPU & compatibility | May break games |
| Disable automatic game fixes | `pcsx_rearmed_nocompathacks` | Advanced / CPU & compatibility | Usually keep Off |
| Simplify CPU loads | `pcsx_rearmed_nolddelay` | Advanced / Speed hacks | May break games |
| Skip code safety checks | `pcsx_rearmed_nosmccheck` | Advanced / Speed hacks | May crash games or break saves |
| Faster 3D calculations | `pcsx_rearmed_gteregsunneeded` | Advanced / Speed hacks | Faster, may cause glitches |
| Simpler 3D clipping | `pcsx_rearmed_nogteflags` | Advanced / Speed hacks | Faster, may cause glitches |
| Simpler CPU timing | `pcsx_rearmed_nostalls` | Advanced / Speed hacks | Faster, may break games |

Hidden/fixed options remain hidden/fixed: `scale_hires`, `gpu_unai_skipline`,
SPU interpolation/threading, GPU/dynarec threading, RGB32, fractional frame rate,
core FPS, app CPU percentage, the raw frontend stereo/widescreen/renderer options,
RetroArch `show_*` folders, lightgun/mouse/NegCon/rumble/multitap controls and the
automatic dynarec choice. New 3DS reverb and Old CD read-ahead remain locked by the
existing code. No unavailable backend gains a menu entry. The host evidence
contains the complete registered inventory (`host/all-options.tsv`) and visible
console/renderer inventory (`host/inventory.tsv`).

## Verification

- CIA and 3DSX build with devkitARM GCC 16.1, default split layout, title counter
  **3076**, semantic version unchanged at **0.3.1**. Remote identity:
  `settings-ui-2-split`. CIA SHA256:
  `d6b54737507a05390227af65a9e977a7aaecb768868e0647c48d21da1d50b345`.
- ASan/UBSan settings tests compile the actual navigator, config, input, options
  and drawing code. They check every visible core option appears exactly once,
  both console models/renderers/scopes, conditional child options, debug gating,
  cursor retention, parent navigation, row spacing, individual override removal,
  whole-game reset, original core storage scope and UI-key gating.
- Existing bottom-screen host suite passes, including touch/input/presets,
  control symbols and fresh-A/cancel confirmations. Existing developer suite
  passes, including authenticated transport, updater guards and UI-key rejection
  outside settings. Renderer/config persistence regression passes.
- Host render output is labelled offline and kept separately in `host/` and
  `host-bottom/`; it is never presented as real hardware screenshots.
- `.text`: main **2,146,052 B** → candidate **2,144,388 B**, **−1,664 B**.
  The large old settings lists were removed from emu.c/libui.c and replaced by
  one cold UI module. No setting lookup, input poll, core, audio, renderer or
  emulation frame-loop work was added. UI-key polling occurs only inside the
  settings/confirmation modals. Link layout can still differ; no hardware FPS
  improvement is claimed from code size.

## Developer capture commands

`ui settings` opens the real current-context navigator. The new `ui-key` command
accepts a/b/x/y/up/down/left/right/l/r only while settings is open, through the
existing default-off token-gated server; batches reject it. It has no gameplay
input path. Confirmation dialogs keep servicing screenshot and navigation
requests. `ui-key debug-on|debug-off` temporarily exposes the Debug page for
inspection, without persisting config or enabling core debug output; it clears
when the modal closes. See `docs/dev-control.md`.

## Hardware evidence

Hardware capture is pending the shared Old 3DS lock. The prepared driver checks
OLD_UNAVAILABLE before and after acquiring the lock, installs via the existing
self-updater, captures real bottom/top frames, restores exact RC2, then verifies
the library and remote identity before releasing the lock. On uncertain relaunch
it marks OLD_UNAVAILABLE and stops without retries/recovery. New 3DS is untouched.
