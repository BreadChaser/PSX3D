# Draw-order 3D for flat scenes (prototype)

New 3DS only, an addition to Real 3D. Branch `t3/draworder-3d`; evidence, simulator tools,
previews and the honest list of what still looks wrong are in the ignored
`devtools/draworder-3d/` (`REPORT.md`, gallery `index.html`).

## Idea

Real 3D gives depth only to polygons the GTE projected; 2D games (SotN, Mega Man X5/X6,
fighting games), menus and logos stay flat. The PS1 has no depth buffer: games paint back
to front, so the order of drawing is a depth map of its own. This turns it into layers:
far background deepest, scenery and characters in between, text and HUD on the screen
surface.

## Where it lives

`psx3d/source/gpustereo_layers.c`, linked after every other frontend object (`LATE_OFILES`)
and hooked in with link-time `--wrap` (psx3d/Makefile): `gs_shift` (the right eye's vertex
shift), `gs_core_frame/alloc/clear/free`, `gpustereo_status` (remote status) and
`pica_picture` (which buffer the frontend shows). No existing source file changed, so no
existing frontend, core or library code moves (`tools/layout/compare.py` against v0.5.0
PASS). On the Old 3DS 3D never turns on, so none of it runs there.

Everything runs on the GPU thread, in draw order, from the vertices gpu_pica hands to
citro3d, after Real 3D's own shift.

## Rules

- **Buffers and passes.** Each VRAM rectangle the frontend shows is a buffer (up to 4). A
  pass into a buffer starts with its first draw after the buffer was last shown (double and
  single buffering, 30 fps games over two host frames). Drawing into a rectangle not shown
  yet uses a default table (each later layer a quarter step forward).
- **Primitives and segments.** A primitive is a triangle or the two halves of a quad
  (neighbouring tiles share edges too; chained, they took one another's level). A segment is a run of primitives of one
  kind (polygon, sprite, untextured) from one 256x256 part of the texture atlas: text, a
  tile map, a sprite's parts. CPU uploads and fills (no vertex colour) are not part of the
  drawing order: they open no pass and mark nothing.
- **Levels.** A 4x4-pixel cell grid over VRAM records what each pass drew (one pixel less at
  each edge, so touching tiles don't overlap). A primitive's level is one above the highest
  level of another segment it covers, and never below the level the pass has reached; the
  pass's level moves on only when a segment ends (a segment's own pieces don't lift each
  other). A segment keeps the level it reached by these rules last pass (found by its kind
  and texture position, which stay put while a scene pans), so a tile map that steps up
  halfway doesn't split: split planes show cracks in the right eye.
- **Big primitives** (half the picture or more): at the start of a pass they are the base;
  later ones (fades, flashes, fog, light beams) take no step of their own and mark nothing.
- **Depth per level.** At the end of a pass, from what is visible in the buffer: only levels
  showing at least 1.5% of the picture are steps (small pieces ride with the level below);
  a level's depth is 60% by area (the share of the picture the levels below it show) and
  40% by rank among the steps. The top step and anything above it is the screen surface,
  the base `LAY_MAX` (8 px of a 320-wide picture) behind. A picture of one step (an
  uploaded movie, a still) sits half deep, anything on it on the screen. One table for all
  buffers, smoothed (15% per pass). Shifts are whole pixels so sprites stay sharp.
- **Real 3D or layers.** Per shown picture, the share of triangles Real 3D moved decides:
  30% or more → Real 3D, under 10% → layers, between: keep going where it was; the blend
  moves 10% per picture. Settled on Real 3D, the layers do no work at all.
- **Edges.** A vertex on the buffer's left edge is not moved (moved right, it would leave a
  strip the right eye never draws).

## Checks (simulator)

- Left eye identical to main (draw hash) in 12 scenes; in settled 3D games the right eye is
  pixel-identical to Real 3D's (Crash, GT2, CTR, Tekken, Silent Hill).
- Flicker (static parts changing depth between frames, 61 frames): 0% median in SotN menu and
  fight, Mega Man X5/X6; Capcom vs SNK Pro 12%.
- `tools/layout/compare.py` against v0.5.0: PASS, no existing code moves.

## Limits

- Painted backgrounds made of many overlapping pieces (Capcom vs SNK Pro) are cut into depth
  slices and re-balance as sprites animate.
- Pictures copied or uploaded into VRAM (movies, some logos) and text on black have nothing to
  separate and stay flat: Screen depth (issue #4) is the fallback; `gs_picture_flat()` says when.
- A crack of 1 px can remain where a tile meets a neighbour more than one level nearer.
- Static screens drawn before 3D turns on keep a flat right eye until they are redrawn.
- Scenes with few GTE polygons (FF VII battle) get a mix of Real 3D and layers.
- Memory: about 320 KB of heap while 3D is on in a game; no GPU memory.
