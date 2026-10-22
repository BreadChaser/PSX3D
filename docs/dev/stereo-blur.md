# Real 3D: blur, cracks and jumping parts (stereo-blur)

New 3DS only. Branch `t3/stereo-blur` on issue #4's screen-depth fallback and the draw-order
layers. Evidence (simulator eyes, triangle dumps, metrics, gallery) is in the ignored
`devtools/stereo-blur/` (`REPORT.md`).

## What the owner saw, and why

- **Blurry picture in 3D games** (Crash title, "a lot of 3D games"): 2D backdrops (painted
  scenery and skies drawn as sprites or polygons without GTE depth, before the 3D scene) stayed
  on the screen plane while the 3D scene in front of them sat up to 10 px *behind* it. The eyes
  can't fuse an object that is drawn over its background but seen behind it. Hardware capture
  of the Crash title: logo +12 px, jungle backdrop 0 px.
- **Crack lines between polygons** (Crash demo): the depth table was keyed by VRAM position
  only. A vertex shared by triangles got different disparities in each: a corner without a
  GTE depth took each triangle's own mean, a lookup could fall back to a neighbouring pixel,
  and the per-triangle outlier check rejected a corner in one triangle but not the next.
- **Parts jumping about** (Crash demo logo): a 2D polygon's corner looked up the depth of
  whatever 3D vertex sat on the same pixel (or the next one), different from frame to frame.
- **Soft picture with screen depth** (logos, 2D): the picture moved by part of a pixel and was
  resampled by the linear filter, differently per eye.
- Ruled out: the native (non-supersampled) top screen targets used in 3D are not softer than
  the supersampled ones (modelled: the supersampled path is the softer one).

## What changed

- `gpustereo_core.c`: every corner of every triangle gpu_pica will draw (clipped fans too) has
  an entry tagged with its triangle's other two corners; the GPU thread finds exactly its own
  triangle. A second, untagged entry per position holds the vertex's depth (or an estimate) for
  corners without one, recorded once per frame, so every triangle sharing a vertex moves it
  alike. 2D polygons are entered without depth and stay flat, unless two corners at least are
  3D polygons' vertices (a mesh the GTE record missed). A corner whose GTE depth belongs to
  another object projected to the same pixel is vetoed for every polygon on that pixel this
  frame (capture time, 0.15 px per px). Table: 128K entries (1 MB, heap, while 3D is on).
- Passes (GPU thread): 2D primitives drawn in a pass before its first 3D polygon are the
  backdrop and move to the back as a whole, by whole pixels; the 3D scene ends at 90% of the
  depth range in front of them. 2D drawn after the first 3D polygon stays on the screen (HUD,
  text). The draw-order backdrop count no longer takes in geometry joined to what was drawn
  before it.
- `video.c`: screen depth moves the picture by whole pixels.
- `gpustereo_layers.c`: deeper layers for 2D games (14 px instead of 8 at the back, spacing 60%
  by rank).

## Checks

- Host tests `stereo-flat`, `stereo-exact` (ASan/UBSan).
- Simulator, 14 scenes: left eye (PS1 VRAM) identical before/after in every frame; crack and
  jump metrics in `devtools/stereo-blur/REPORT.md`.
- `tools/layout/compare.py` against v0.5.0 rc4 and `tools/layout/check.sh`: PASS (core and
  library code at the same addresses). Frontend code after `gpustereo_core.c` moves: Old 3DS
  A/B/A at integration (nothing new runs there).
