# Real 3D on the GPU renderer (round 3)

New 3DS only. The 3D setting (Settings → Display, hidden on the Old 3DS as before) gains
**Real 3D** next to Screen depth and Off. Branch `t3/gpu-stereo3` on v0.4.1; evidence, tools and
test CIAs in the ignored `devtools/gpu-stereo3/` (its `REPORT.md` has the full numbers and file
list). Round 2 (`t3/gpu-stereo`, old history) was a prototype on the pre-v0.4.0 renderer; this
round is a new implementation of the same idea that leaves the renderer itself untouched.

## How it works

- **PS1 VRAM stays canonical** and is the left eye. Nothing the game can read back (textures,
  render-to-texture, readbacks, VRAM copies) ever sees shifted pixels.
- **Every GPU write to PS1 VRAM is repeated into a right-eye VRAM target**: draws (polygons,
  sprites, uploads, fills), VRAM-to-VRAM copies and the held frameskip picture. This happens in
  link-time wrappers of `C3D_DrawArrays` and `GX_TextureCopy` (`psx3d/source/gpustereo.c`).
  The repeat changes only the framebuffer address and, when anything moves, the vertex buffer
  address; all other GPU state is the renderer's own. Because everything is repeated, the right
  eye is always complete: round 2's black SotN menu eye (it repeated only "display-sized" areas)
  cannot happen.
- **Depth**: the GTE records the depth of every point it projects (existing core code, on only
  while Real 3D is in use). When the main thread hands a polygon to the GPU thread (the existing
  `psx3d_depth_cmd` hook in gpu_async), `gs_capture` looks up its corners and stores their depth
  by VRAM position: the corners and also every vertex the renderer will create when it clips the
  polygon to the drawing area (same triangles, same clipping and rounding as gpu_pica, depth
  interpolated perspective-correctly). Polygons with fewer than two known corners stay flat (2D
  polygons can share one position with a projected point by chance). GTE values up to two frames
  old are accepted: R4 projects in one frame and draws in the next at 30 fps, which is why
  round 2 (one frame) found no depth there.
- **Disparity** (second pass, build 7; see "User feedback" below): on the GPU thread, polygon
  vertices (they alone sit at pixel·16 + (8, 7)) are looked up by position and moved sideways in
  a copy of the batch's vertices. The scene's near depth (5% of the frame's polygon corners
  nearer) sits on the screen and its far depth (5% further) 10 pixels behind it with the slider
  fully up (pixels of a 320-wide picture; wider modes scale with their width, so it is the same
  on the screen), spread by log z in between; nearer than the near depth comes out of the
  screen, at most 2.5 pixels. Both depths follow the scene with hysteresis and ~0.4 s
  smoothing. Sprites, rectangles, lines, text and uploads stay on the screen surface.
- **Steady polygons**: a corner whose disparity steps against both other corners of its triangle
  (more than 0.3 px per px of edge + 1 px) took another object's depth (two vertices on the same
  screen pixel) and gets theirs. Corners without a GTE depth are not stored in the depth table
  (the GPU thread gives them their triangle's depth); the table is four-way and evicts the
  oldest entry.
- **Backdrops**: games that draw their sky or arena wall first as geometry near the camera
  (Tekken 3, CTR and GT2 skies) are found from draw order: the first part of a frame's polygons,
  3/4 octave nearer than what follows. The next frames put such early, near polygons at the back.
- **Edges**: a vertex on the drawing area's left edge is not moved right (on the right edge: not
  moved left), otherwise the right eye leaves a strip at the picture's edge undrawn (seen on the
  console in round 3's first build). Moved outwards, the GPU's scissor cuts it.
- **Slider**: at the bottom nothing is mirrored and the screen is 2D; above 5% depth recording
  and mirroring switch on, the depth scales with the slider; 30 frames at the bottom switch them
  off again (each switch recompiles the dynarec's code, so it doesn't flap).
- **Memory**: right-eye target 1 MiB VRAM, held right picture 1 MiB linear, shifted-vertex ring
  576 KiB linear, depth table 512 KiB heap, all allocated the first time 3D comes on in a game and
  freed when it ends. The supersampled top-screen targets leave no VRAM for the right eye, so
  while a game uses Real 3D the top screen uses native targets (one short hitch when 3D first
  comes on); when the game ends all screen targets are made again in startup order, so the
  library gets its supersampled targets back (first build: it didn't, the bottom target was in
  the way; verified on the New 3DS over two 3D games, same free VRAM as before).
- **Movies (24-bit)** have no right eye and use screen depth; until the right eye is ready
  (first frames) screen depth is shown too.

## User feedback on build 4, and the second pass (build 7)

The user tried build 4 on the New 3DS: **"the 3D looks pretty flat"** and **"the polygons on the
player model in Silent Hill seem to flicker weirdly"**. Both were found in the simulator with
per-triangle disparity dumps of the depth traces (`STEREO_DUMP` in
`tools/gpu-compare/stereo-sim.c`; tables and pictures in `devtools/gpu-stereo3/round2/`):

- **Flat**: build 4 put the screen plane at 0.8 × the frame's mean depth and clamped anything
  nearer at −2.8 px. In Silent Hill the camera is close behind Harry, so all of Harry, the floor
  around him and nearby walls sat on that clamp as one flat layer (18–39% of all shifted
  vertices in the Silent Hill scenes, and every one of Harry's vertices in the street and
  alley); the rest used only 4–6 of the 8 pixels behind, bunched at the back by 1/z. Build 7
  spreads each scene between its own near and far depth by log z, counting only polygons
  inside the drawing area (geometry behind the camera had pulled the near depth in). Visible
  spread (p5 → p95 disparity, full slider, PS1 pixels of a 320-wide picture): Silent Hill
  street 7.6 → 10.5, alley 7.8 → 10.0, café 8.5 → 10.1, garage 4.6 → 5.8, Tekken 3 5.3 → 11.6,
  FF7 battle 4.5 → 7.8 (`round2/depthstats.md`). Harry now has about 1 px of depth front to back
  instead of none, and stands just behind the screen with the scene behind him.
- **Flicker**: polygon corners find their depth by screen position. When one of Harry's
  vertices lands on the same pixel as a vertex of the wall or floor behind him (the GTE records
  the last point projected there), the triangles at that corner took the wall's depth for that
  frame, so single triangles jumped back and forth (in build 4 visible as light triangles on the
  otherwise clamped Harry, e.g. 2.5% of his triangles per frame in the café). The triangle check
  replaces such a corner. Harry's region, triangles with a depth step the eyes can't fuse per
  frame: café 25.7 → 9.8, alley 7.5 → 0.7; triangles off their neighbours by more than 1 px:
  alley 5.9 → 2.6 (`round2/harry.txt`; what remains are real silhouettes against the scene
  behind and zero-area triangles). Consecutive café frames before/after:
  `round2/sim/cafe-harry-depthmap-build4-top-build7-bottom.png`.

Build 5 (first version of this pass) and 6 were superseded: 6 pushed the frontend 152 bytes past
the 4 KiB boundary in front of the core (the core moved +12 KiB); 7 marks the once-per-frame code
cold (size-optimised) and is back at v0.4.1 + 8 KiB.

## Old 3DS: zero cost

The same CIA runs on both consoles, so the code ships in the Old build, but it never runs there
(`gpustereo_mode()` is 0 on the Old 3DS whatever the config or switches say). The core is
byte-for-byte the v0.4.1 code: the two hooks are wrapped in the core's partial link
(`EXTRA_WRAP_SYMS`), their empty originals stay linked, and the only changed instructions are the
10 call sites that now branch to the wrappers (6 × `C3D_DrawArrays`, 2 × `GX_TextureCopy`, the two
hooks). The whole core and all libraries move by exactly 8 KiB (two instruction-cache ways, so
every cache set stays the same); the dynarec's assembly does not move. With 3D off, the wrappers
cost one load and one predictable branch per draw call.

## Checks

See `devtools/gpu-stereo3/REPORT.md` for the numbers.

- **Old 3DS A/B/A** against v0.4.1 on the 13 heavy scenes, frameskip off and Auto, for builds 1,
  2 and 4: every scene within about ±1% (build 4: off worst −0.78%, Auto worst −0.72%);
  requesting 3D on the Old changes nothing. Second pass: build 5 (A1 B A2) off worst Crash 2
  −1.05%, Auto −0.73%; build 7 (the candidate, `gpu-stereo3-7`; A2 B A3) off worst Crash 2
  −1.18%, Crash −0.89%, the rest −0.8…0%; Auto worst CTR −0.94%, Crash 2 −0.85%. Build 7's core
  code and read-only data sit at the same addresses as build 4's; Crash 2 at about −1% in builds
  4, 5 and 7 alike is the cost of the core's +8 KiB move, at the edge of the gate
  (`devtools/gpu-stereo3/round2/old7-*.md`).
- **New 3DS**, build 7: pending at the time of writing. The user's Silent Hill game was
  running on the New all session, and a self-update would have ended it, so the session
  (`devtools/gpu-stereo3/round2/new-waiter.sh`: one install, load + Real 3D + eye captures, the
  13 heavy scenes at full and half 3D, gpu-stereo3-4 put back on any failure) only starts once
  the New sits idle in the library. Results then in `devtools/gpu-stereo3/hw/new-7`.
- **New 3DS**, build 4, 13 heavy scenes: 100% game speed and unchanged pictures per second with Real 3D
  at slider 0, ½ and full, frameskip off and Auto, except GT2 heavy at 98.9% (frameskip off,
  full 3D; 100.4% with Auto). Main-thread frame time grows by 1.5–5 ms (GT2: 9.4 → 14.3 ms).
  Widescreen + 3D (R4, GT2, CTR, SH): 99.9%. Both eyes captured on the console in every scene
  (`shot eyes`): depth in every 3D game including R4; SotN menus and fights have two identical,
  complete eyes; the frameskip held picture has its right eye too.

- **Mono output identical** (rechecked for build 7 on 23 traces): with 3D on, the left eye
  (canonical VRAM) is identical to mono on every frame of 22 traces (13 heavy, 6 standard, SotN title/menus) — full-VRAM hash per frame and
  the draw hash. With 3D off nothing in the renderer path changes (code comparison above).
- **GX queue / command buffer** (the v0.4.1 GX queue fix's harness: real citro3d 1.7.1 recording,
  our real renderqueue.c, a libctru-like queue that panics when full): with the real
  `gpustereo.c` mirroring at full strength (rerun for build 7, same result), the 13 heavy +
  7 standard states, 4 widescreen cases,
  the Crash 2 cold boot (also widescreen), MGS, Grandia and Oddworld boots (3600 frames) and all
  63 HLE cold boots complete with no queue panic and no command overflow.
- **Pictures**: offline replay of the captures with real GTE depths shows depth in Crash, Crash 2,
  CTR, FF7 battle, GT2, Klonoa, NFS, R4, all Silent Hill scenes and Tekken 3; SotN (fight, menus,
  title) is flat with both eyes identical. At most 0.08% of right-eye pixels are not explained by
  a horizontal shift of the left eye (no cracks or garbage).

## Known limits

- Depth comes from exact GTE screen positions; geometry a game transforms without the GTE, or
  reuses from more than two frames back, stays flat.
- 2D backdrops (sky sprites, prerendered backgrounds) stay on the screen surface while 3D scenery
  can be behind them.
- Backdrop detection works from draw order; a game whose 3D backdrop is not drawn first, or is
  drawn as farther geometry anyway, keeps its own depths. When the detection switches on or off
  (R4 heavy: a few times a minute) the sky changes depth once.
- The depth range adapts per scene, so the same object can sit at a different depth in two
  scenes (the nearest part of each scene is always at the screen).
- The asymmetric pair (left eye = original view) puts the stereo camera slightly to the right of
  the original one. Objects at the left/right picture edges can be cut differently per eye.
- Comfort and the look on the console are for the user to judge (test steps in
  `devtools/gpu-stereo3/TRY-ON-CONSOLE.md`).
