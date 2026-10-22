# Renderer I-cache, round two

heavy-off: mean +0.55%, worst -0.74%; heavy-auto: mean +0.42%, worst -0.19%. No speed row worse than 1%. Renderer cycles/primitive -7.76..+1.97%; I-cache misses/primitive -20.72..+12.42%.

The baseline is published main `c7689cdc` plus accepted compact clipping
`f61f49e2` (local cherry-pick `98638c77`). The candidate keeps one polygon decoder
and one vertex emitter, with three small changes:

- `2eb3295`: skip coarse drawn-tile bookkeeping when valid bounds are already covered.
- `6d3f830`: pass the four draw-mode bytes as one word through `use_state`; unpack only when state changes.
- `9c94d6b`: compute packet stride once, read CLUT/page once, and read colours when finishing vertices.

The decoder first records positions and texture bounds. Its uncommon fallback
initializes colours before calling the existing clipping/blending path. The
ordinary path reads colours directly from the original packet after texture
and state setup. Packet formats, vertex order, GPU coordinates, blend/mask
rules, texture invalidation and readback synchronization retain their existing
semantics. No new runtime setting or per-format decoder was added.

## Size and screening

Production GCC 16.1 sizes, in bytes:

| Function | Compactclip baseline | Candidate |
|---|---:|---:|
| Polygon decoder/emitter | 2868 | 2852 |
| Shared triangle emitter | 2060 | 2072 |
| State check | 204 | 192 |
| State-change helper | 252 | 184 |
| Drawn-area check | 364 | 348 |
| Clipping | 1208 | 1208 |

The core C code region shrinks 96 bytes. The compiler moves renderer functions
and shifts part of core BSS by eight bytes; libraries and the core assembly
retain their addresses. These are natural compiler layout changes. There are
no padding objects or ordering tricks. Comparing the changed core against rc4
with `tools/layout/compare.py` deliberately reports differences; it is not an
unchanged-core proof. The separate frontend-growth/layout proof passes.

The simulator's completed six heavy polygon screens show instructions down
2.96–5.08% and distinct fetched 32-byte lines down 1.62–3.41% per polygon.
Those counts describe executed code footprint, not real I-cache misses or
hardware speed. The exploratory Crash 2 map stops at a pre-existing debug
assertion and is excluded from these ranges. Full correctness runners disable
that assertion in both builds and require all 600 frames.

Two combinations were set aside: a fused finishing loop grew the production
triangle emitter, and a separately called polygon-finishing helper increased
the simulator footprint in CTR, GT2, R4 and SH alley. Earlier rejected
per-format specialization and link ordering were not repeated.

## Correctness and hardware procedure

All offline gates PASS. Private evidence lives in `devtools/renderer-icache2/`; `offline-gate.json` is
written only after every required comparison and exact-reference-CIA smoke
test finishes. It requires all of:

- Nineteen traces, each 600 frames, with identical full-VRAM hashes every frame.
- Thirteen heavy and six standard states, each 600 frames, with identical draw/guest/IRQ hashes and complete serialized states.
- Sixty-eight HLE cold boots, each 600 frames, with the same comparisons.
- Nineteen real-citro3d cases with identical per-frame register write sequences, draw state and guest results.
- Renderer sanitizers, command/presentation regression suites, and frontend-growth layout proofs.

The historical `pica-direct/model/gate.py` does not accept `--writes`. This
round uses its current v050 checker, `devtools/v050/model/gate.py --writes`,
comparing an immutable baseline direct runner with an immutable candidate
direct runner. Both execute the same real citro3d and frontend wrapper code.

The Old session is queued after UI, requeued multiplayer, dynarec-split and
code-quality. The waiter is `psx3d-renderer-icache2-old.service`; it makes no
console contact before those completions and the complete offline gate.
It runs compactclip A1 → candidate → compactclip A2, each with
all thirteen heavy Off, Auto and renderer PMU runs. It then restores exact
rc4. A successful `DONE renderer-icache2` requires the final library status,
defaults=1/preset_pack=3, unchanged config, a final screenshot and complete
measurement tables. A fresh title number is claimed for each install; fixed
makerom and parsed ticket/TMD checks are mandatory. Every CIA is packed from
the v050 worktree, with its RomFS checked against shipped rc4; the restore's
ExeFS must also match. Each exact CIA gets a silent private Azahar smoke test.

## Limits

The −25% renderer CPU target remains open. These small code reductions cannot
establish that target from a simulator alone. A result worse than 1% in any
speed row is flagged for review. NFS and Crash 1 include loading in their
short windows; Auto also changes the primitive mix by skipping pictures.
The report keeps frame-level renderer work beside per-primitive PMU costs.

Next structural leads are a single shared packet-view representation for the
decoder/texture lookup, fewer calls and spills on repeated state, and a
compact inside-area slow-path emitter. Each needs the same output gates and
Old hardware; no assumption of safe same-state reordering is made.

A1 compactclip title 5167 → B compactdecode title 5168 → A2 compactclip title 5169; final exact rc4 title 5170.

heavy-off — game speed percent:

| Scene / slot | A1 | B | A2 | B vs mean A | A bracket spread |
|---|---:|---:|---:|---:|---:|
| game=Crash_Bandicoot state=5 | 88.56 | 90.04 | 88.83 | +1.52% | 0.31% |
| game=Crash_Team_Racing state=5 | 79.01 | 79.78 | 79.01 | +0.97% | 0.00% |
| game=Ridge_Racer_Type_4 state=5 | 82.92 | 82.43 | 82.56 | -0.37% | 0.44% |
| game=Castlevania_-_Symphony_of_the_Night state=5 | 98.40 | 97.78 | 98.62 | -0.74% | 0.22% |
| game=Silent_Hill state=5 | 83.43 | 83.57 | 83.50 | +0.13% | 0.08% |
| game=Silent_Hill state=6 | 74.84 | 74.75 | 74.87 | -0.14% | 0.04% |
| game=Silent_Hill state=7 | 74.24 | 75.08 | 74.35 | +1.06% | 0.16% |
| game=Gran_Turismo_2 state=5 | 54.40 | 54.66 | 54.34 | +0.53% | 0.12% |
| game=Tekken_3 state=5 | 67.19 | 67.91 | 67.13 | +1.11% | 0.10% |
| game=Final_Fantasy_VII state=5 | 100.34 | 100.28 | 100.30 | -0.04% | 0.04% |
| game=Klonoa_-_Door_to_Phantomile state=5 | 90.30 | 89.69 | 90.24 | -0.64% | 0.07% |
| game=Crash_Bandicoot_2_-_Cortex_Strikes_Back state=5 | 76.85 | 78.97 | 76.87 | +2.74% | 0.03% |
| game=Need_for_Speed_-_High_Stakes state=5 | 35.85 | 36.27 | 35.94 | +1.06% | 0.23% |

heavy-off — whole main thread I-cache misses (thousands per emulated frame):

| Scene / slot | A1 | B | A2 | B vs mean A |
|---|---:|---:|---:|---:|
| game=Crash_Bandicoot state=5 | 21.661 | 20.332 | 21.568 | -5.93% |
| game=Crash_Team_Racing state=5 | 36.250 | 35.515 | 36.287 | -2.08% |
| game=Ridge_Racer_Type_4 state=5 | 31.127 | 32.175 | 31.135 | +3.35% |
| game=Castlevania_-_Symphony_of_the_Night state=5 | 20.897 | 21.774 | 20.923 | +4.13% |
| game=Silent_Hill state=5 | 52.533 | 52.787 | 52.412 | +0.60% |
| game=Silent_Hill state=6 | 43.625 | 44.764 | 43.639 | +2.59% |
| game=Silent_Hill state=7 | 45.120 | 44.959 | 45.143 | -0.38% |
| game=Gran_Turismo_2 state=5 | 50.044 | 50.106 | 50.230 | -0.06% |
| game=Tekken_3 state=5 | 38.262 | 38.117 | 38.383 | -0.54% |
| game=Final_Fantasy_VII state=5 | 18.012 | 18.168 | 18.018 | +0.85% |
| game=Klonoa_-_Door_to_Phantomile state=5 | 32.477 | 33.687 | 32.577 | +3.57% |
| game=Crash_Bandicoot_2_-_Cortex_Strikes_Back state=5 | 32.511 | 30.649 | 32.434 | -5.61% |
| game=Need_for_Speed_-_High_Stakes state=5 | 77.455 | 76.906 | 77.371 | -0.65% |

heavy-auto — game speed percent:

| Scene / slot | A1 | B | A2 | B vs mean A | A bracket spread |
|---|---:|---:|---:|---:|---:|
| game=Crash_Bandicoot state=5 | 97.87 | 97.89 | 97.71 | +0.11% | 0.17% |
| game=Crash_Team_Racing state=5 | 89.75 | 90.60 | 90.19 | +0.70% | 0.49% |
| game=Ridge_Racer_Type_4 state=5 | 89.91 | 89.94 | 89.34 | +0.35% | 0.63% |
| game=Castlevania_-_Symphony_of_the_Night state=5 | 99.98 | 100.01 | 100.00 | +0.02% | 0.02% |
| game=Silent_Hill state=5 | 87.68 | 87.55 | 87.63 | -0.12% | 0.05% |
| game=Silent_Hill state=6 | 87.15 | 87.57 | 87.23 | +0.43% | 0.09% |
| game=Silent_Hill state=7 | 77.85 | 78.78 | 78.01 | +1.09% | 0.20% |
| game=Gran_Turismo_2 state=5 | 55.06 | 55.13 | 54.88 | +0.29% | 0.33% |
| game=Tekken_3 state=5 | 92.07 | 92.68 | 92.00 | +0.71% | 0.08% |
| game=Final_Fantasy_VII state=5 | 100.16 | 100.16 | 100.23 | -0.04% | 0.08% |
| game=Klonoa_-_Door_to_Phantomile state=5 | 100.03 | 99.61 | 99.56 | -0.19% | 0.47% |
| game=Crash_Bandicoot_2_-_Cortex_Strikes_Back state=5 | 92.58 | 94.36 | 92.58 | +1.92% | 0.01% |
| game=Need_for_Speed_-_High_Stakes state=5 | 36.03 | 36.09 | 36.04 | +0.16% | 0.02% |

heavy-auto — whole main thread I-cache misses (thousands per emulated frame):

| Scene / slot | A1 | B | A2 | B vs mean A |
|---|---:|---:|---:|---:|
| game=Crash_Bandicoot state=5 | 19.634 | 18.758 | 19.584 | -4.34% |
| game=Crash_Team_Racing state=5 | 33.208 | 32.891 | 33.159 | -0.88% |
| game=Ridge_Racer_Type_4 state=5 | 28.652 | 29.231 | 28.867 | +1.64% |
| game=Castlevania_-_Symphony_of_the_Night state=5 | 20.325 | 20.832 | 20.338 | +2.46% |
| game=Silent_Hill state=5 | 50.803 | 51.092 | 50.860 | +0.51% |
| game=Silent_Hill state=6 | 38.791 | 39.433 | 38.877 | +1.54% |
| game=Silent_Hill state=7 | 43.518 | 43.556 | 43.572 | +0.03% |
| game=Gran_Turismo_2 state=5 | 49.509 | 49.886 | 49.560 | +0.71% |
| game=Tekken_3 state=5 | 31.358 | 31.332 | 31.440 | -0.21% |
| game=Final_Fantasy_VII state=5 | 17.618 | 18.408 | 17.679 | +4.30% |
| game=Klonoa_-_Door_to_Phantomile state=5 | 29.924 | 30.638 | 29.944 | +2.35% |
| game=Crash_Bandicoot_2_-_Cortex_Strikes_Back state=5 | 28.512 | 27.652 | 28.566 | -3.11% |
| game=Need_for_Speed_-_High_Stakes state=5 | 77.603 | 77.655 | 77.605 | +0.07% |

Renderer PMU per primitive (cycles / I-cache misses / instructions):

| Scene / slot | Cycles A → B | I-cache misses A → B | Instructions A → B |
|---|---:|---:|---:|
| game=Crash_Bandicoot state=5 | 2082.21 → 1958.12 (-5.96%) | 9.23 → 7.57 (-17.91%) | 1019.63 → 964.70 (-5.39%) |
| game=Crash_Team_Racing state=5 | 2766.22 → 2652.88 (-4.10%) | 12.93 → 11.45 (-11.42%) | 1229.90 → 1183.77 (-3.75%) |
| game=Ridge_Racer_Type_4 state=5 | 5054.81 → 5040.13 (-0.29%) | 25.16 → 27.49 (+9.25%) | 2154.11 → 2088.57 (-3.04%) |
| game=Castlevania_-_Symphony_of_the_Night state=5 | 6037.92 → 6127.71 (+1.49%) | 36.81 → 40.13 (+9.02%) | 2289.33 → 2269.59 (-0.86%) |
| game=Silent_Hill state=5 | 5058.99 → 5158.42 (+1.97%) | 29.57 → 32.23 (+8.99%) | 2016.08 → 1995.66 (-1.01%) |
| game=Silent_Hill state=6 | 4741.58 → 4719.57 (-0.46%) | 26.70 → 28.64 (+7.28%) | 1891.46 → 1830.24 (-3.24%) |
| game=Silent_Hill state=7 | 4676.21 → 4451.62 (-4.80%) | 26.96 → 24.85 (-7.83%) | 1949.31 → 1855.42 (-4.82%) |
| game=Gran_Turismo_2 state=5 | 4532.02 → 4396.83 (-2.98%) | 23.81 → 23.28 (-2.20%) | 1923.77 → 1847.23 (-3.98%) |
| game=Tekken_3 state=5 | 2288.97 → 2190.99 (-4.28%) | 9.64 → 9.03 (-6.40%) | 1080.85 → 1018.25 (-5.79%) |
| game=Final_Fantasy_VII state=5 | 1973.94 → 1965.84 (-0.41%) | 9.71 → 9.33 (-3.91%) | 865.65 → 862.39 (-0.38%) |
| game=Klonoa_-_Door_to_Phantomile state=5 | 7413.58 → 7512.90 (+1.34%) | 41.64 → 46.81 (+12.42%) | 2771.95 → 2702.07 (-2.52%) |
| game=Crash_Bandicoot_2_-_Cortex_Strikes_Back state=5 | 2809.30 → 2591.36 (-7.76%) | 13.66 → 10.83 (-20.72%) | 1253.24 → 1189.80 (-5.06%) |
| game=Need_for_Speed_-_High_Stakes state=5 | 21236.03 → 21202.35 (-0.16%) | 55.52 → 59.51 (+7.20%) | 10402.12 → 10292.99 (-1.05%) |

Renderer work per emulated frame (profiling adds overhead):

| Scene / slot | PICA wall ms A1 / B / A2 | Renderer Mcycles A1 / B / A2 | Primitives/frame A1 / B / A2 |
|---|---:|---:|---:|
| game=Crash_Bandicoot state=5 | 4.873 / 4.542 / 4.879 | 1.380 / 1.305 / 1.376 | 660.634 / 666.507 / 662.696 |
| game=Crash_Team_Racing state=5 | 5.679 / 5.456 / 5.679 | 1.504 / 1.435 / 1.499 | 542.786 / 540.937 / 542.786 |
| game=Ridge_Racer_Type_4 state=5 | 5.583 / 5.608 / 5.584 | 1.658 / 1.652 / 1.658 | 327.696 / 327.696 / 328.442 |
| game=Castlevania_-_Symphony_of_the_Night state=5 | 9.074 / 9.207 / 9.046 | 1.916 / 1.934 / 1.897 | 315.885 / 315.627 / 315.688 |
| game=Silent_Hill state=5 | 3.623 / 3.620 / 3.619 | 1.058 / 1.088 / 1.074 | 210.661 / 210.853 / 210.659 |
| game=Silent_Hill state=6 | 7.067 / 7.077 / 7.041 | 1.981 / 1.976 / 1.982 | 418.698 / 418.698 / 417.084 |
| game=Silent_Hill state=7 | 6.547 / 6.207 / 6.544 | 1.844 / 1.748 / 1.843 | 394.449 / 392.666 / 394.031 |
| game=Gran_Turismo_2 state=5 | 11.789 / 11.581 / 11.779 | 3.021 / 2.924 / 3.005 | 664.402 / 665.128 / 665.252 |
| game=Tekken_3 state=5 | 8.905 / 8.546 / 8.888 | 2.452 / 2.353 / 2.466 | 1074.306 / 1074.145 / 1074.297 |
| game=Final_Fantasy_VII state=5 | 4.051 / 4.026 / 4.051 | 0.963 / 0.953 / 0.958 | 486.564 / 484.854 / 486.564 |
| game=Klonoa_-_Door_to_Phantomile state=5 | 6.049 / 6.093 / 6.026 | 1.713 / 1.731 / 1.704 | 230.416 / 230.389 / 230.446 |
| game=Crash_Bandicoot_2_-_Cortex_Strikes_Back state=5 | 8.607 / 7.992 / 8.512 | 1.765 / 1.596 / 1.727 | 623.374 / 615.871 / 619.641 |
| game=Need_for_Speed_-_High_Stakes state=5 | 28.748 / 28.561 / 28.898 | 7.911 / 7.950 / 8.013 | 374.944 / 374.944 / 374.944 |

Final exact rc4 restore: title 5170, library idle, defaults=1, preset_pack=3; unchanged config.ini.
