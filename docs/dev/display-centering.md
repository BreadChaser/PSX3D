# Display position, borders, 480i and PAL

`psx3d/source/display.c` (`display_fit`) decides where the PS1 picture goes on the 400x240 top
screen, for the GPU picture and for 24-bit movies alike (`video.c`, `draw_picture`).

- gpulib describes the TV field the game's display range sits in (`screen.hres` x `screen.vres`)
  and where the picture starts in it (`screen.x/.y`, its "auto" centring: a picture within 8 px of
  the middle is centred). The field is shown like a TV: hres → the screen mode's box width, one line
  → one row in Original (240 lines). Short pictures get black borders (CTR 216 lines, MGS/Silent
  Hill/Vagrant Story 224) instead of being stretched; parts outside the field are cut.
- 480i (vres 480/512): two picture lines per row. gpu_pica draws every line of a 480i frame
  (`renderer_set_interlace` is a no-op on purpose), so VRAM holds the whole picture: Smooth blends
  each line pair, Smooth off shows one field (sampling starts 1/8 texel past the centre, so 2:1
  pictures never pick lines by float rounding).
- PAL's 256-line field is shown through a 240-line window that keeps a 240-line picture whole (1:1);
  a taller picture is scaled to fit.
- Stretch and Widescreen use the same framing inside their box.

Tests: `psx3d/tests/display` (host, cases from real games' display setups). Evidence and the TV-model
check: `devtools/display-centering/REPORT.md`.
