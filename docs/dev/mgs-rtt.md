# MGS framebuffer feedback

Metal Gear Solid's Alaska opening redraws the framebuffer through about 226
short textured rectangles per picture. The old GPU decoder could not run on
hardware: it tried to create a render target from the shared linear-memory
atlas, which the frontend rejects. The native model accepted that allocation
and concealed the CPU fallback. On the New 3DS, the matched core-first-2
baseline runs at 23.10% game speed with about 7,800 decoded tiles and 1,504 GPU readbacks per second.
Candidate mgs-rtt-8 is New-accepted / Old-pending: Auto reaches 99.93%,
while frameskip off reaches 81.94%.

## Command stream and dependencies

An HLE boot saved at frame 2080 reproduces the scene. Across 120 captured
vblanks, 13,512 modulated rectangles perform a vertical resampling and blue
tint effect:

- GP0 `0x64`, colour `0x805252`, usually 160×2 pixels, sometimes 160×1.
- E1 `0x300/0x302` samples the left framebuffer; `0x305/0x307` samples the right.
  Drawing alternates between inclusive areas `(0,0)-(319,223)` and
  `(320,0)-(639,223)`. Each buffer is processed in two 160-pixel halves.
- For example, `(0,15,160,2)` is sampled into `(0,14,160,2)`. Source and
  destination rows differ by up to four pixels in either direction.
- Textures are depth-2 direct colour with no texture window. E6 forces the
  mask bit and disables mask checking. The CLUT field is constant `0x800a`
  and unused in this format.

The strips repeatedly modify cells that later strips sample. A stale page
snapshot changes the picture. There are no atlas evictions in this window;
this is source-version churn, rather than CLUT cycling or a cache-key mismatch.
The strict baseline makes 13,317 CPU decode calls for 66,505 cells. Texture
reads cause 13,078 synchronizations and retiring queued atlas readers causes
another 11,464.

## Implementation

The general direct-colour decoder now renders one cell row into a 256×32
RGBA8 VRAM target (32 KiB), then copies contiguous cell runs into the existing
linear atlas with ordered GX transfers. A 256×256 scratch target did not fit
in the console's fragmented VRAM after presentation targets were allocated.
Target allocation failure retains the precise CPU fallback. Failed target
cleanup clears the texture descriptor explicitly: citro3d's `C3D_TexDelete`
does not clear its data pointer.

Wide, short, opaque sprites also have a fused path. It copies the required
8-row native tile bands into a separate 256×16 RGB5A1 texture (8 KiB of linear
memory), then recovers RGB5, applies modulation, rejects colour zero and sets
the mask in the consuming draw. This removes the separate two-pass RGBA8
decode for each strip. It supports direct-colour, unwindowed, unflipped,
fully clipped-in sprites with forced mask and no blending; other draws use
the atlas. Both paths preserve the source snapshot before any overlapping
write. Native tile rows are copied verbatim and sampled with reversed V.

The frontend optionally expands both CPU-side GX queues from 32 to 256
entries (14 KiB extra total). The ten-entry reserve and 80% command-buffer
guard remain. Allocation failure keeps the original queue sizes. This reduces
forced submissions in feedback-heavy frames; expanding the queue alone gave
only a small New speed gain with the row decoder.

CPU reads still settle GPU drawing. GPU snapshots do not discharge CPU
readback debt. Existing atlas keys, allocation and per-cell versions remain.
The native model now rejects linear render targets and distinguishes ordered
GX writes from CPU overwrites of pending texture readers.

## Validation

All 13 heavy and six standard traces preserve 600 full-VRAM hashes each and
their final draw hashes: 11,400 matching frames. The Alaska and underwater
traces preserve 120 full-VRAM hashes each; the later interior preserves 600.
The fused path records its own command data in simulator draw hashes, so the
MGS draw checksum changes representation; the 19 regression checksums do not.
P.E.Op.S. verifies the recorded captures. Heavy captures use disabled
dithering, and their reference replay uses `GPU_DITHER=0`.

Six RTT fixtures run in six modes, including unavailable-target and 32 KiB
scratch limits, with delayed staging/atlas reader guards. The strip fixture
covers every RGB5/STP value, tint, raw sampling, unaligned bands and fallback
clipping/window cases. These fixtures and the renderer regressions also pass
AddressSanitizer and UndefinedBehaviorSanitizer.

The real citro3d/GX-queue model passes MGS, Crash 2, Grandia and Oddworld HLE
cold boots for 3,600 frames at both queue capacities. Corresponding draw hashes
match and neither queue nor command-buffer guard overflows. The layout growth
checks pass. Renderer changes still need the real Old 3DS A/B/A acceptance
gate; New measurements cannot establish Old instruction-cache behavior.

Physical-console results, original screenshots, complete VRAM comparisons,
immutable CIAs, SHA-256 files and the detailed report are kept in ignored
`devtools/mgs-rtt/`. Candidate `mgs-rtt-8` (counter 4100) passed its exact-CIA
headless Azahar library and game checks. Its fresh 19×600 regression frames
and 840 MGS frames match; all 36 RTT fixture runs pass ASan/UBSan. Eight
real-citro3d/GX HLE boot runs pass for this source at both queue capacities.

Build 7 passed the New check but failed the real Old gate: off deltas reached
−2.61%, Auto −3.28%, with under 0.93% baseline drift. Build 8 keeps optional
feedback code after the frontend in `.pica_rtt.text` and resource descriptors
after existing BSS in `.pica_rtt.bss`, within the startup zero range. The common
sprite path checks dimensions before calling feedback code. Existing hot
texture/emit/sprite addresses stay within four bytes of core-first-2; existing
BSS offsets and library addresses are preserved. No padding or dummy tables
are added. Layout growth checks pass; this is not a claim of overall layout
equality or of Old acceptance.

The completed build-8 New A/B uses core-first-2 as A, so both sides use the
same shader. The current play build includes a separate texel-edge bias fix.

| MGS Alaska | core-first-2 | mgs-rtt-8 |
|---|---:|---:|
| Off game speed | 23.098% | 81.942% |
| Auto game speed | 23.056% | 99.925% |
| Off distinct pictures/s | 7.591 | 25.123 |
| Auto distinct pictures/s | 7.591 | 24.672 |
| Off readbacks/s | 1503.56 | 0 |
| Off renderer syncs/s | 2868.41 | 24.58 |
| Off emulation ms/frame | 64.932 | 19.381 |
| Auto audio underruns/s | 11.69 | 0 |

Off remains below full speed, with 5.08 audio underruns/s. The underwater
sample improves 21.20→77.07%; interior remains near 98%. All 13 other heavy
scenes pass with frameskip off and Auto: relative deltas −0.190…+0.020%.
Complete rows and screenshots are in `devtools/mgs-rtt/hw/new-8/`.

New was restored to the user's current `stereo-seams-1` play build at
10:30:42 UTC on 2026-10-21 (session `19352cc5b778`, library, no active batch).
The final screenshot was inspected. Before/after configurations are
byte-identical; FPS-on and widescreen are preserved. The driver verified
`stats` was absent and never wrote switches.txt. Restore CIA SHA-256:
`d3f21ed27527c67844a00cd1fe54b119ea5b50feb782f92a35a20793749b10ea`.

Real Old build-8 numbers remain pending: the Old 3DS was unavailable for this round.
The eventual
gate is core-first-2 A / build-8 B / core-first-2 A, heavy13 off+Auto within
±1%, followed by release-v041-rc4 with pure defaults. Build 7's failed results
are historical and do not establish build 8's Old performance.

The candidate CIA is `devtools/mgs-rtt/artifacts/PSX3D.cia`, also retained
in immutable `artifacts-8/`; SHA-256
`029dc672c21166607b977477dd3891dd0c5c7a59c2e66a764416c5ab0b43ad4d`.
The package is explicitly marked `NEW_ACCEPTED_OLD_PENDING`.

Build 8's Alaska and interior hardware VRAM dumps are byte-identical. Underwater
changes 14 low-contrast edge pixels in the complete 1 MiB dump, with no mask
changes or large picture errors. Its hardware-reference mean channel error
remains 2.029, compared with 3.413 in the unchanged interior. These are real
PICA precision differences absent from the native model; they are recorded
in the report and must not be described as byte-identical underwater output.

The measured candidate is based on core-first and uses 2D 4:3 output. It has
not been combined with the separate stereo3 branch. The optional scratch
fallback also covers future presentation layouts with less contiguous VRAM.
