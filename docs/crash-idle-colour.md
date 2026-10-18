# Magenta Crash during the idle animation

On both 3DS consoles, and with both renderers, Crash Bandicoot sometimes
shows a magenta body and head while standing still on the first beach.
This is the game's own Wumpa-fruit idle animation, not an emulation or
rendering defect. No renderer correction was made.

## What the game does

Leave Crash standing still and he throws a Wumpa fruit, it bursts on his
head, he turns magenta and then spins to shake the juice off. External
descriptions of the 1996 game:

> "Crash picks up a Wumpa Fruit and throws it up in the air. He then looks up
> and scratches his head. The Wumpa Fruit then splatters all over his face,
> covering him in purple juice. Crash then spins to remove it."
> — Crash Bandicoot Wiki, *Idle animations*, section "Crash Bandicoot"
> (<https://crashbandicoot.fandom.com/wiki/Idle_animations>, retrieved 2026-10-18)

> "In the first game, if you happen to leave Crash standing for too long,
> Crash will accidentally hit himself with a Wumpa fruit, revealing purple
> juice."
> — Crashmania, *Wumpa Fruit*
> (<https://www.crashmania.net/en/encyclopedia/wumpa-fruit/>, retrieved 2026-10-18)

## Host evidence

A 3600-frame capture from the Old 3DS benchmark state (`o3auto`; sources of
`t3/o3ds60` 5e26a12) shows the sequence: the fruit lands at frame ~1256,
purple particles appear, Crash is magenta in frames 1260–1385, then he
spins back to orange.

1. **The renderer only draws what it is given.** The P.E.Op.S. reference
   replay of the same trace is magenta in exactly the same frames.
2. **CPU and GTE settings don't matter.** The magenta also appears with the
   interpreter instead of the dynarec, with GTE flags on, with
   "GTE regs unneeded" off, and with all three together (no idle skipping).
3. **The game switches Crash's colour table.** The GTE light and colour
   matrices are identical on normal and magenta frames. Crash's 732 polygons
   take their base colour (GTE RGBC) from a per-model colour table read by
   the game routine at 0x80034600. During the animation that routine reads
   a second table: the fur entry changes from orange `600031ff` to magenta
   `60ff00ba`, and the other entries (shorts, shoes) stay the same.
4. **That table comes from the disc.** A 116-byte span around the table head
   in RAM matches the disc byte for byte. The span has 39 copies in the
   game's data, one per level file. The first word after the span differs
   only because the game rewrites that reference field when it loads the
   level (disc `efaa9274`, RAM `00210680`).
   `tools/crash-idle/disc_table.py` reproduces this without storing game
   data. Expected for *Crash Bandicoot (USA)*, BIN SHA-256
   `35d4b837108adc4c5313a45c0f203aa76bc0911873204e908512187379eb98ec`:

   ```sh
   python3 -I tools/crash-idle/disc_table.py verify "Crash Bandicoot (USA).bin" \
       0x46a844 116 c0dd7ebb590e12f66cd1ff2d922035259ab9d6343a81ee7b51bede6ea2aa02eb 39
   # hash OK copies 39
   ```

   Offsets in Mode 2 Form 1 user data:
   0x46a844 0xc8862c 0x1032840 0x1860040 0x1b6dbe0 0x1ff7058 0x25bbf10
   0x29f1d74 0x31ef838 0x36ab83c 0x3a4094c 0x43c7038 0x49cd04c 0x4f6d83c
   0x53ac978 0x58ace2c 0x5d8ce30 0x6253dac 0x7262e2c 0x7544660 0x79fd840
   0x7e26844 0x839814c 0x8b5a134 0x916a838 0x931de2c 0x9805894 0x9f96038
   0xa121138 0xa596138 0xa85c208 0xac2caa4 0xb1f2840 0xb75cfc8 0xbd6dad0
   0xc2fe038 0xc677840 0xca2093c 0xd084040.
   `disc_table.py find BIN RAMDUMP` derives the span from a 2 MiB RAM dump
   taken at frame 1271 of the capture.

## Old and new GPU paths compared with the reference

The full trace was replayed with the old paths (default) and with
`TEXMASK=1 TEXMEMO=1 PICA_SUBMIT=7 PICA_PRIMS=7`. The two produce identical
full-VRAM hashes on all 3600 frames, the same draw hash `ada33e1893327694`,
and identical images for frames 1240–1400. The GT2 skip gate cannot be
switched in the host replayer and was not part of this comparison.

`tools/crash-idle/magenta_metrics.py` counts magenta pixels (R>150, B>130,
G<100) in frames 1240–1400:

| Replay | Magenta pixels | Not in reference | Missing vs reference | Max extra per frame |
|---|---|---|---|---|
| P.E.Op.S. reference | 82,780 | — | — | — |
| PICA, old paths | 81,552 | 7,750 | 8,978 | 94 |
| PICA, new paths | 81,552 | 7,750 | 8,978 | 94 |

Every extra pixel lies in frames 1260–1385, inside the animation. A scan of
all 3600 frames finds none elsewhere. 7,594 of them lie within 1 px and
7,734 within 2 px of the reference's own magenta: these are polygon edges
and colours near the cut-off, from the known differences between the host
rasterizer and P.E.Op.S. The remaining 16 are isolated single pixels, at
most 3 per frame, on juice particles and pink shading during the spin.
There is no magenta that the reference doesn't explain. The host rasterizer
is not a model of PICA precision; hardware screenshots, including the
released Software renderer on New 3DS, show the same animation.
