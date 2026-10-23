# Animated themes for v0.6

Eight menu themes use the approved round-three scenes and Dreamstale icons.
Scenes and icons release their menu resources before gameplay. All New scene
geometry is preserved; Old uses lighter geometry in the heavier effects.

The Old menu CPU target is about3ms top/1.5ms bottom. The latest physical
candidate (themes-v06-3) reduced seven top backdrops below2.2ms mean; Phosphor
was2.868/1.445ms top/bottom, worst3.360/1.770ms. Library cadence was41–55/s.
These are candidate results, not a measured rc9 comparison.

This follow-up (themes-v06-4) reduces Old Phosphor bottom sampling from300 to192
positions, retaining all four moving light lobes and scanlines. New and Old top
geometry are unchanged. It also moves the menu-only CPU timing getter outside
the pinned frontend block: retaining that getter had shifted five renderqueue
functions by24bytes. The complete protected frontend now keeps rc9 page offsets.

Menu frame recording waits for the prior GPU queue, and presentation waits for
VBlank. Saved High contrast diagnostics show about4.4ms CPU plus12.8ms GPU;
icon CPU is about0.09ms and full-heap flush0.43–0.48ms. This supports serial CPU/GPU
submission as a limit but does not isolate individual GPU costs for text, covers
or scenes. Presentation behavior is unchanged.

For a fair baseline, shipped rc9 already exposes library frame intervals through
`covers stats-reset` and `covers`: `frame_avg_us`, `frame_max_us`, frame count and
slow-frame counts. Reset, wait12s without other remote requests, then read
`covers`. Rate is1e6/frame_avg_us on every leg. This measures library frame starts,
not LCD scanout; the existing counter excludes gaps of1s or longer.

The next menu session compares installed exact rc9 A1, every theme on the frozen
candidate, then frozen exact rc9 A2. Acceptance is cadence within about2draws/s of
the bracketing baseline and backdrop CPU within budget. Original must be selected
and saved through Settings before restoration. No raw config replacement is used.

In-game colours are **not themed yet**. They follow menu acceptance in a separate
commit, cached once at game start with pinned-safe storage and no per-frame theme
lookups. That commit needs its own short heavy Old A/B/A and must be omitted if
it costs performance. Detailed evidence and session tooling are kept locally in
`devtools/themes-v06/REPORT.md`.
