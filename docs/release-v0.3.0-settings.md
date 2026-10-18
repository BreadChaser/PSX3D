# v0.3.0 settings and switches

Paths below are relative to `sdmc:/3ds/PSX3D/`. No GPU profile is enabled in
this release. Software is the factory default for both console families.

## Frontend configuration

`config.ini` uses `key=value`. Factory defaults apply when a key is absent.

| Key | Values / factory behavior |
|---|---|
| `renderer` | `sw` (default), `gpu`; normal **Renderer: Software / GPU (experimental)** setting; apply by closing and reopening game |
| `show_fps` | 0 Off (default), 1 FPS footer, 2 Detailed replaces touch layout |
| `smooth` | 1 default; 0 sharp scaled pixels |
| `stereo` | 0 off, 1 screen depth (default), 2 experimental real 3D (software, debug-only); slider activates it |
| `half3d` | Old 1 / New 0; software 3D polygons only, HUD/text/2D retain full height |
| `stretch` | 0 default (4:3); 1 stretched. Widescreen uses the internal core key below |
| `audio_stretch` | 0 off; 1..50 maximum slowdown; 50 default on both; menu on selects 50; lowers pitch |
| `sounds`, `music` | Menu effects/library music, each 1 default |
| `bottom_off` | 1 default and forced at load; auto backlight only when controls/stats do not require it |
| `autosave` | 1 default; Continue where I left off |
| `touch_slots` | Format marker 1; written automatically; migrates legacy default bindings |
| `touch_stick` | -1 inherit (default), 0 hidden, 1 enabled; Old virtual right stick only |
| `touch_click_left` | 0 Right (default), 1 Left; global New click-column placement |
| `hotkey_ff`, `hotkey_rewind` | Two named physical buttons joined with `+`, or `off`; default `SELECT+R`, `SELECT+L`; uses listed menu pair spellings |
| `map_A`, `map_B`, `map_X`, `map_Y`, `map_L`, `map_R`, `map_ZL`, `map_ZR`, `map_START`, `map_SELECT` | Physical remaps; default Circle, Cross, Triangle, Square, L1, R1, L2, R2, Start, Select |
| `map_TOUCH_L2`, `map_TOUCH_R2`, `map_TOUCH_L3`, `map_TOUCH_R3`, `map_TOUCH_X1`, `map_TOUCH_X2` | Independent slot bindings; -2 inherits preset/default, -1 Hide; default inherited L2/R2/L3/R3/FF toggle/rewind hold, subject to console and preset permissions |
| `last_dir`, `games_dir`, `last_title` | Library folder/selection strings; factory last_dir `sdmc:/`; reset preserves these |
| `debug` | 0 default, 1 developer features (real 3D, audio recording/test tone); renderer selection is always visible |
| `core.<key>` | Effective core-option override; full inventory below, subject to console locks |

Remap values are libretro joypad IDs: 0 Cross, 1 Square, 2 Select, 3 Start,
4 Up, 5 Down, 6 Left, 7 Right, 8 Circle, 9 Triangle, 10 L1, 11 R1,
12 L2, 13 R2, 14 L3, 15 R3; 16 FF toggle, 17 FF hold, 18 rewind hold.
Physical unmapped = -1; touch Hide = -1; touch inheritance = -2.
Rewind is New-only and must be enabled for that game. Presets may hide FF or
rewind entirely; explicit PS1 touch bindings remain editable.

`games/<game>.ini` stores explicit `renderer=sw|gpu`, `screen=0|1|2`
(4:3/stretch/widescreen), `rewind=0|1` (default 0), and overrides for
`touch_stick`, all `map_*` and both `hotkey_*` keys. Other frontend settings and
`core.*` overrides remain global in this version. `touch_controls` is a legacy
0 Auto / 1 On / 2 Off key, migrated to independent bindings on load and removed
on control save. Legacy `map_TOUCH_SELECT` is removed. The pre-0.2 `games.ini`
`<game>=<screen>` fallback is read until a game reset removes its entry.

Global Reset to defaults restores Software and console defaults while keeping
library location, game overrides and saves. Game Reset to global defaults
removes game overrides and restores global settings plus the current preset;
the running renderer changes only after closing/reopening. Controls reset
removes control overrides while retaining renderer/screen/save settings.

The compiled per-console preset fields are `fast_forward`, `rewind`,
`touch_controls`, `needs_l2r2`, `needs_l3r3`, `needs_right_stick`, `renderer`,
and six `touch_bindings`. Renderer values INHERIT/DEFAULT inherit the global
choice; SOFTWARE/GPU choose that console's renderer, below explicit game and
switch overrides. No seeded profile selects GPU; the table contains a commented
example only. Disc serials are normalized from the core's identification log.

## Core keys

The table lists every registered key in the actual 3DS dual build, including
fixed/hidden keys. Defaults below come through production `options_get`, for
Old installed CIA (30% syscore allowance) and New. Old HBL instead disables CD
read-ahead and the SPU helper because it has no syscore time. Test overrides
can override most locks; Old GPU threading is always forced off. Values must
match the core's registered list; see the patched core option header.

| Core key (prefix `core.` in config.ini) | Old CIA default | New default | Menu Old / New |
|---|---|---|---|
| `pcsx_rearmed_region` | `auto` | `auto` | shown / shown |
| `pcsx_rearmed_bios` | `auto` | `auto` | shown / shown |
| `pcsx_rearmed_show_bios_bootlogo` | `disabled` | `disabled` | hidden / hidden |
| `pcsx_rearmed_memcard1` | `libretro` | `libretro` | shown / shown |
| `pcsx_rearmed_memcard2` | `shared` | `shared` | shown / shown |
| `pcsx_rearmed_cd_readahead` | `256` | `256` | hidden / shown |
| `pcsx_rearmed_drc` | `enabled` | `enabled` | hidden / hidden |
| `pcsx_rearmed_drc_thread` | `disabled` | `disabled` | hidden / hidden |
| `pcsx_rearmed_psxclock` | `auto` | `auto` | shown / shown |
| `pcsx_rearmed_dithering` | `disabled` | `disabled` | shown / shown |
| `pcsx_rearmed_gpu_thread_rendering` | `disabled` | `enabled` | hidden / hidden |
| `pcsx_rearmed_frameskip_type` | `auto` | `disabled` | shown / shown |
| `pcsx_rearmed_frameskip_threshold` | `33` | `33` | shown / shown |
| `pcsx_rearmed_frameskip_interval` | `2` | `3` | shown / shown |
| `pcsx_rearmed_display_fps_v2` | `disabled` | `disabled` | hidden / hidden |
| `pcsx_rearmed_display_info` | `enabled` | `enabled` | shown / shown |
| `pcsx_rearmed_fractional_framerate` | `auto` | `auto` | hidden / hidden |
| `pcsx_rearmed_alt_flip` | `auto` | `auto` | shown / shown |
| `pcsx_rearmed_rgb32_output` | `disabled` | `disabled` | hidden / hidden |
| `pcsx_rearmed_scale_hires` | `enabled` | `enabled` | hidden / hidden |
| `pcsx_rearmed_gpu_slow_llists` | `auto` | `auto` | shown / shown |
| `pcsx_rearmed_show_overscan` | `disabled` | `disabled` | hidden / hidden |
| `pcsx_rearmed_screen_centering` | `auto` | `auto` | shown / shown |
| `pcsx_rearmed_screen_centering_x` | `0` | `0` | shown / shown |
| `pcsx_rearmed_screen_centering_y` | `0` | `0` | shown / shown |
| `pcsx_rearmed_screen_centering_h_adj` | `0` | `0` | shown / shown |
| `pcsx_rearmed_show_gpu_unai_settings` | `disabled` | `disabled` | hidden / hidden |
| `pcsx_rearmed_gpu_unai_old_renderer` | `disabled` | `disabled` | shown / shown |
| `pcsx_rearmed_gpu_unai_blending` | `enabled` | `enabled` | shown / shown |
| `pcsx_rearmed_gpu_unai_skipline` | `disabled` | `disabled` | hidden / hidden |
| `pcsx_rearmed_gpu_unai_lighting` | `enabled` | `enabled` | shown / shown |
| `pcsx_rearmed_gpu_unai_fast_lighting` | `enabled` | `disabled` | shown / shown |
| `pcsx_rearmed_spu_reverb` | `disabled` | `enabled` | shown / hidden |
| `pcsx_rearmed_spu_interpolation` | `simple` | `gaussian` | hidden / hidden |
| `pcsx_rearmed_nocdaudio` | `enabled` | `enabled` | shown / shown |
| `pcsx_rearmed_noxadecoding` | `enabled` | `enabled` | shown / shown |
| `pcsx_rearmed_spu_thread` | `enabled` | `disabled` | hidden / hidden |
| `pcsx_rearmed_show_input_settings` | `disabled` | `disabled` | hidden / hidden |
| `pcsx_rearmed_analog_axis_modifier` | `square` | `square` | shown / shown |
| `pcsx_rearmed_vibration` | `enabled` | `enabled` | hidden / hidden |
| `pcsx_rearmed_analog_combo` | `l1+r1+select` | `l1+r1+select` | shown / shown |
| `pcsx_rearmed_multitap` | `disabled` | `disabled` | hidden / hidden |
| `pcsx_rearmed_negcon_deadzone` | `0` | `0` | hidden / hidden |
| `pcsx_rearmed_negcon_response` | `linear` | `linear` | hidden / hidden |
| `pcsx_rearmed_input_sensitivity` | `1.00` | `1.00` | hidden / hidden |
| `pcsx_rearmed_crosshair1` | `disabled` | `disabled` | hidden / hidden |
| `pcsx_rearmed_crosshair2` | `disabled` | `disabled` | hidden / hidden |
| `pcsx_rearmed_konamigunadjustx` | `0` | `0` | hidden / hidden |
| `pcsx_rearmed_konamigunadjusty` | `0` | `0` | hidden / hidden |
| `pcsx_rearmed_gunconadjustx` | `0` | `0` | hidden / hidden |
| `pcsx_rearmed_gunconadjusty` | `0` | `0` | hidden / hidden |
| `pcsx_rearmed_gunconadjustratiox` | `1.00` | `1.00` | hidden / hidden |
| `pcsx_rearmed_gunconadjustratioy` | `1.00` | `1.00` | hidden / hidden |
| `pcsx_rearmed_icache_emulation` | `enabled` | `enabled` | shown / shown |
| `pcsx_rearmed_exception_emulation` | `disabled` | `disabled` | shown / shown |
| `pcsx_rearmed_3ds_appcputime` | `30` | `35` | hidden / hidden |
| `pcsx_rearmed_cd_turbo` | `disabled` | `disabled` | shown / shown |
| `pcsx_rearmed_nocompathacks` | `disabled` | `disabled` | shown / shown |
| `pcsx_rearmed_nolddelay` | `disabled` | `disabled` | shown / shown |
| `pcsx_rearmed_nosmccheck` | `disabled` | `disabled` | shown / shown |
| `pcsx_rearmed_gteregsunneeded` | `enabled` | `disabled` | shown / shown |
| `pcsx_rearmed_nogteflags` | `enabled` | `disabled` | shown / shown |
| `pcsx_rearmed_nostalls` | `disabled` | `disabled` | shown / shown |

Additional frontend/internal keys: `psx3d_renderer` is the menu option alias for
`renderer`, exposing Software and GPU (experimental), not a separate saved
`core.*` selection. `core.pcsx_rearmed_psx3d_widescreen=0|52429` supplies the
GTE scale (0 off, 52429 = 0.8 Q16); `core.pcsx_rearmed_psx3d_stereo=disabled|enabled`
is synchronized by slider/debug/software selection. These two options are
read internally and are not registered menu rows. No `gte_color`, `xa_decode`,
`spu_dma` or predictive/presentation optimization option was imported.

## Startup diagnostic switches

Arguments and whitespace-separated `switches.txt` words use the same parser.
The file is read after launch arguments, so later explicit values win.
Switches do not save menu settings. Remove overrides when testing defaults.

| Switch | Effect |
|---|---|
| `renderer=sw`, `renderer=gpu` | Highest-priority renderer selection for testing |
| `stats`, `prof` | Interval diagnostics; detailed event profiling |
| `pmu=AA,BB` | Two hexadecimal kernel PMU events; no CP15/backdoor writes |
| `game=Title_with_underscores` | Autostart matching library title |
| `state=N`, `state0` | Load numbered state / slot 0 |
| `noskip`, `fs1`, `fs3` | Disable frameskip; fixed interval 1; Old test interval 3 (keeps chosen policy) |
| `clock=N`, `cdra=N` | Core PS1 clock option / CD read-ahead sectors |
| `o:name=value` | Up to eight core-option test overrides; parser supplies `pcsx_rearmed_` prefix, e.g. `o:gpu_thread_rendering=disabled` |
| `gputhread`, `nogputhread` | Restore New default renderer worker / disable; Old always direct |
| `nosputhread` | Disable Old CIA sound helper |
| `gpuasync`, `nogpuasync` | PICA overlapping GPU submissions on (default) / off; inert for Software |
| `gpuflush`, `nogpuflush` | PICA targeted cache cleaning on (default) / off; inert for Software |
| `gldbg=N` | PICA diagnostic bitmask, table below; inert for Software |
| `vramdump=N` | Dump GPU VRAM after N frames to `vram.bin`; GPU renderer only |
| `acap` | Hash ten seconds of original core audio starting after 60 frames |
| `o3ds` | Pretend Old hardware; config_save suppressed in this test mode |
| `slider=F`, `showright`, `sbs` | Test slider, right-eye display, stereo side-by-side |
| `aa`, `noaa` | Old PICA gameplay supersampling; disable all top supersampling |
| `nonet`, `csnd`, `nosound`, `noirrst`, `nolcd`, `nocovers` | Disable networking; force CSND; disable sound/C-stick initialization/LCD control/covers |
| `minimal` | Set nonet, nosound, noirrst, noaa, nolcd and nocovers |

Active `gldbg` bits: 32 whole VRAM; 1024 skip GPU draw calls; 2048 skip primitive
commands; 4096 decode primitives without drawing; 8192 suppress frame splits;
16384 invert readback Y; 32768 disable STP mask pass; 65536 disable additive
batching; **131072 restore reference renderer CPU paths** (keeper isolation).
Old comments mentioning bits 1/2 do not represent active PICA behavior.
`nopredict`, `nopresent`, `nosmooth`, and optimization options from excluded
combined stages are not switches in this release. Turn audio smoothing off
through its normal setting or `audio_stretch=0` in `config.ini`.

Build-only controls: frontend plain `make` always builds dual; `GPU=` is rejected.
Core diagnostic builds accept `CTR_GPU=dual|pica|unai`, `USE_ASYNC_GPU=0|1`;
`PICA_SIM_HASH`, `PICA_TIMERS`, fake-SDK/runner environment flags are host/developer
instrumentation, not persisted product settings. CIA tools use BANNERTOOL and
MAKEROM as documented in the release report.
