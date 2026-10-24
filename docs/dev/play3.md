# Owner play build 3

`release-v052-play3` combines the accepted v0.5.2 speed integration with the
approved menu themes. No gameplay drawing or emulation source changes relative
to the speed build.

Theme code is the net result of `c5d9f1d1..92cad9c3`: restored Original
`e3d3782f`, approved-scene motion `5b39167f`, play1 integration dependency
`d3344e84`, cached projection `e4045ffa`, Eclipse replacement `98ce19cf`, and
four additional themes plus the live selector `dcc89689`. Documentation-only
and historical theme-build identities are excluded. There are twelve themes;
Display > Theme opens a list, previews on movement, accepts with A and restores
the committed choice with B.

Offline checks pass: build, all 30 host suites without skips (settings spec
rerun after integrating its documentation contract), twelve palettes with 39
tokens each, layout compare against the speed ELF, all pinned gameplay frontend
cache sets, and the three-build growth/new-libm proof. Core and library code
retain their addresses; relocations to page-shifted data are allowed by the
existing checker. Silent Azahar Old/New smokes ran through the wrapper on
workspace 8; both models boot the library, the selector reaches Sienna, and
an additional screenshot-checked Old-model navigation run restores Original
from Sienna. These are emulator checks, not hardware performance claims.

Packaging uses fixed makerom from this worktree's cwd and validates ticket/TMD
versions. The new checkout packs RomFS differently from the frozen play1 CIA,
but extraction verifies all 2,052 files have identical paths and SHA256 hashes.
Each physical-console package is checked against the frozen play3 RomFS.

Hardware evidence, per-theme Old cadence and backdrop CPU intervals, installed
title numbers and config preservation are recorded in the ignored main-checkout
`devtools/play3/REPORT.md`. The owner's later instruction takes precedence over
the initial Original-restoration request: restore the owner's latest theme through Settings
and preserve the fresh pre-session config byte for byte. The Old requires
read-only five-minute polls and ten minutes of stable library/config state
before claiming its lock. Any unexpected screen/theme change ends all input. New keeps the owner's
FPS and widescreen choices.

Hardware closure 2026-10-24: first-attempt installs succeeded on New5271 and
Old5272. New is verified in the library with byte-identical config. Old passed
the ten-minute owner-inactivity gate and measured baseline High contrast54.27,
Original26.04,Midnight teal41.51 and PS1 grey49.55 menu draws/s. Original costs
18.025ms top CPU versus2.159/2.165ms for Midnight/PS1 grey. The sweep stopped
after an unexpected Theme-picker screen during the Sunset transition; no further
inputs, only read-only status/config/screenshot capture and lock release. Cause
is unproven. Old saved config at stop is PS1 grey, initial High contrast; final
screenshot is Ember preview. Nine themes and final restoration are incomplete.
Do not claim idle actual Library or identical Old config. No install retry.
