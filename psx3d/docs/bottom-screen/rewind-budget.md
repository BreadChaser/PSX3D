# Old 3DS rewind assessment

Decision: keep the current rewind engine **New 3DS only** as a technical
limit. Old 3DS hides the rewind remap target, hotkey, side button and option.
Even an enabled preset cannot bypass that engine limit. Curated New presets
can also disallow rewind per game; unlisted New games expose it, off until
enabled in Settings. UI uses plain names without console suffixes.
No consoles were accessed for this assessment; no new hardware timings or
free-heap measurements were taken. This is a code/memory-budget check
supplemented by existing logs.

## Memory

- `core/frontend/libretro.c:retro_serialize_size()` returns **0x440000 =
  4,456,448 bytes = 4.25 MiB = 4,352 KiB**. An existing New 3DS rewind log
  (`devtools/log_rw.txt:132`) independently reports state 4352 KB.
- `psx3d/source/rewind.c` allocates two complete state buffers (**8.5 MiB**)
  and worst-case delta scratch `2*state + 64` (**8.5 MiB + 64 bytes**).
  Workspace alone is **17 MiB + 64 bytes**, before history or the worker stack.
- Current history is **24 MiB** on New 3DS, for about **41 MiB** total.
  Reducing history to **4 MiB** still requires **21 MiB**; **2 MiB** requires
  **19 MiB**, before the frontend/core's existing heap allocations.
- The released CIA's `psx3d/meta/app.rsf` gives Old 3DS **64 MiB** total.
  The built ELF's text/data/BSS occupy approximately **22.9 MiB**; main stack
  is **1 MiB**. `main.c:__system_allocateHeaps()` splits the remaining roughly
  40.1 MiB into about **13.4 MiB linear**, **8 MiB reserve** for fixed mappings,
  and only **18.7 MiB ordinary heap**. That is the entire malloc arena,
  **not free space** after launching a game. It also serves existing game,
  audio, cover, state and frontend allocations. The reserve/linear heaps are
  not interchangeable with the malloc buffers used by rewind.
- Existing logs from a different GPU-renderer build corroborate the scale:
  `devtools/o3pmu_Ridge_Racer_Type_4_00_08.log:10` reports heap 19MB / linear
  13MB (integer MiB); other Old logs report 18MB. They are historical context,
  not measurements of this software-renderer binary or its current free heap.

Even the proposed 2-4 MiB history exceeds the **entire** heap with the current
workspace design. Merely reducing captures/history cannot fix this. A future
Old 3DS implementation would need a different buffer/codec design and explicit
free-memory checks, not enabling the present core-2 worker on core 0.

## Main-CPU capture cost estimate

The same existing Ridge Racer log, line 12, records Old 3DS ordinary-heap
throughput of **119.41 MB/s read**, **350.96 MB/s write**. Source in the existing
GPU-renderer checkout's `main.c` confirms decimal MB/s (`/ 1e6`), measured
using sequential volatile word loops over 256 KiB. These are a rough bandwidth
reference; optimized memcpy, game state structure, caches and encode branches
can change the real cost. They are **not measured serialization timings**.

For each 4.25 MiB capture, a simple traffic budget gives:

| Work | Traffic calculation | Estimate |
|---|---|---|
| Copy a state | 4,456,448 / 119.41e6 + 4,456,448 / 350.96e6 | **50 ms** |
| Read both states for delta encoding | 8,912,896 / 119.41e6 | **75 ms** |
| Combined capture + scan | Before XOR writes, allocation, history copy, GPU/SPU freeze, dynarec metadata | **125 ms** |

On New 3DS, capture is on the game thread and delta compression uses core 2.
Old 3DS would run both on its already CPU-limited game core. At two captures
per second, this traffic-only estimate consumes about **25%** of one CPU-second;
at one per second about **12.5%**, with large capture hitches; at the current
nominal six per second about **75%**. The current interval is every ten emulated
frames, so a slow game naturally captures less often, but that does not remove
the hitch or workspace requirement. Exact cost requires hardware measurement;
none was attempted because console access was explicitly prohibited.

The decisive constraint is memory, with CPU estimates providing an additional
reason to avoid enabling a reduced-history variant without a new design and
hardware validation. New 3DS behavior/capture rate/history remain unchanged.
