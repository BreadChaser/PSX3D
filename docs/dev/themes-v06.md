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

Entry42 measured shipped rc9 at29.92/s in both A1/A2 windows (361 intervals,
all over25ms and none over40ms). Original48.97, Midnight teal41.51, PS1 grey49.77,
Sunset49.50, High contrast54.70 and Phosphor42.78 all pass the cadence gate.
Phosphor backdrop CPU is2.684/1.101ms mean,3.026/1.337ms worst top/bottom.
Eclipse/Ember were skipped by a transient candidate-side OCR error before Eclipse;
cleanup restored exact rc9 with identical config. Driver retries OCR without input.
Only those two matched-counter checks remain, combined with the colour-pass A1.

The rc9 windows contain zero cover uploads/downloads/pending work: this is stable
library cadence, not startup cover loading. The old dense hill mesh and serial
CPU/GPU/VBlank path plausibly account for two-refresh pacing; no component profile
was captured, so exact attribution is unmeasured. Both versions use the same counter,
selected title, warmup and uninterrupted window.

The separate gameplay colour pass copies colours once at launch into a dedicated
page-aligned NOLOAD section after established BSS. Touch artwork, action accents,
FPS/stats and OSD read this cache with no per-frame theme lookup or allocation.
PS1 face hues remain semantic; other action accents follow the theme. Original
retains its controller colours and inactive bottom stays black. Menu theme selection
refreshes action colours immediately. Existing function page offsets are pinned
individually across the touched objects. This pass is pending its short heavy Old
A/B/A; omit it if it costs Old performance. Detailed evidence and session tooling
are kept locally in `devtools/themes-v06/REPORT.md`.
