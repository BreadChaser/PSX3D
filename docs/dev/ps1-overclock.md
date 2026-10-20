# PS1 CPU overclock (New 3DS presets, research)

Full measurements and evidence: `devtools/ps1-overclock/REPORT.md` (local tooling folder).

## What the clock does
`pcsx_rearmed_psxclock` ("Game CPU speed") sets the core's cycle multiplier: `auto` = 175
(about 57%), N = 10000/N. Timers, vblank, sound, CD and MDEC keep emulated time; a higher value
only lets more PS1 instructions run per vblank. The core's own per-game multipliers (database
overrides) apply only while the option is `auto`.

## Presets
`game_preset_options_t.psxclock` (0 inherits) sets a per-console default. Order: developer
`clock=` > the player's per-game Game CPU speed > preset > global setting. Entries for the
screened New 3DS games are behind `PSX3D_PS1_OVERCLOCK_PRESETS` (off):

| Game | New 3DS | Why |
|---|---|---|
| CTR (SCUS94426) | 75 | heavy race 26–27 → 29–30 pictures/s |
| Gran Turismo 2 Arcade (SCUS94455) | 80 | 25.6 → 30.1; logic is frame-locked, so this removes the PS1 slowdown |
| Silent Hill (SLUS00707) | 100 | garage 24 → 30, café 22 → 30 |
| NFS High Stakes (SLUS00826) | 80 | 44 → 58; delta-time logic |

Never set a clock for games whose loop is not tied to the vblank (FFVII field runs twice as fast
at 100, also Einhänder, FF Origins/Tactics, Rayman, Resident Evil and others in the report) or
for core database override titles. The Old 3DS gets slower with any overclock.

## Developer switch `occlock=auto|off` (default off)
Automatic clock prototype (`autoclock.c`): raises 57 → 80 → 100 while a game mixes on-cadence
and late flips and the pacer still idles, latches, and drops back when the host is busy or the
game starts flipping faster than before. It changes only the cycle multiplier
(`autoclock_core.c`, `psxCpu->ApplyConfig()`); each change still flushes the whole dynarec cache,
about a second of game time in a heavy scene. Fixed per-game values are the recommendation.
