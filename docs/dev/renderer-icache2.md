# Renderer I-cache, round two

Hardware acceptance is pending the shared Old queue. No hardware speed gain is claimed.

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

Private evidence lives in `devtools/renderer-icache2/`; `offline-gate.json` is
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
code-quality. It runs compactclip A1 → candidate → compactclip A2, each with
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
