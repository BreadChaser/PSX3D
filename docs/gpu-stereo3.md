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
- **Disparity**: on the GPU thread, polygon vertices (they alone sit at pixel·16 + (8, 7)) are
  looked up by position and moved sideways in a copy of the batch's vertices:
  `d = 8 px · slider · clamp(1 − focus/z, −0.35, 1)` — up to 8 PS1 pixels behind the screen and
  2.8 in front. Sprites, rectangles, lines, text and uploads stay on the screen surface. The focus
  follows 0.8 × the geometric mean depth of the frame, with hysteresis and ~0.4 s smoothing.
  Vertices on the drawing area's left/right edge (where the renderer clipped) keep their place:
  otherwise the right eye leaves a 1–3 pixel strip at the picture's edge undrawn (seen on the
  console in round 3's first build), and the depth goes to the screen there anyway.
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
  2 and 4 (the candidate, `gpu-stereo3-4`): every scene within about ±1% (build 4: off worst
  −0.78%, Auto worst −0.72%); requesting 3D on the Old changes nothing.
- **New 3DS**, 13 heavy scenes: 100% game speed and unchanged pictures per second with Real 3D
  at slider 0, ½ and full, frameskip off and Auto, except GT2 heavy at 98.9% (frameskip off,
  full 3D; 100.4% with Auto). Main-thread frame time grows by 1.5–5 ms (GT2: 9.4 → 14.3 ms).
  Widescreen + 3D (R4, GT2, CTR, SH): 99.9%. Both eyes captured on the console in every scene
  (`shot eyes`): depth in every 3D game including R4; SotN menus and fights have two identical,
  complete eyes; the frameskip held picture has its right eye too.

- **Mono output identical**: with 3D on, the left eye (canonical VRAM) is identical to mono on
  every frame of 22 traces (13 heavy, 6 standard, SotN title/menus) — full-VRAM hash per frame and
  the draw hash. With 3D off nothing in the renderer path changes (code comparison above).
- **GX queue / command buffer** (the v0.4.1 GX queue fix's harness: real citro3d 1.7.1 recording,
  our real renderqueue.c, a libctru-like queue that panics when full): with the real
  `gpustereo.c` mirroring at full strength, the 13 heavy + 7 standard states, 4 widescreen cases,
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
- The asymmetric pair (left eye = original view) puts the stereo camera slightly to the right of
  the original one. Objects at the left/right picture edges can be cut differently per eye.
- Comfort and the look on the console are for the user to judge (test steps in
  `devtools/gpu-stereo3/TRY-ON-CONSOLE.md`).
