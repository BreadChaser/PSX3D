#!/usr/bin/env python3
"""Small GPU streams with independently checkable pixels; no game data needed."""
import argparse
from pathlib import Path
import struct
import subprocess
from compare import images


def xy(x, y):
    return (x & 65535) | ((y & 65535) << 16)


def uv(u, v, page=0):
    return u | (v << 8) | (page << 16)


class Trace:
    def __init__(self, path):
        self.f = path.open("wb")
        self.f.write(b"PSXGPU01")

    def record(self, tag, data):
        self.f.write(struct.pack("<II", tag, len(data)))
        self.f.write(data)

    def words(self, tag, words):
        self.record(tag, struct.pack("<" + "I"*len(words), *words))

    def init(self, vram):
        self.words(3, [0, 0, 1024, 512, 1])
        self.record(4, struct.pack("<"+"H"*len(vram), *vram))
        self.commands([0xe100010a, 0xe3000000, 0xe4000000 | 319 | (239<<10), 0xe5000000, 0xe6000000])

    def commands(self, words):
        self.words(1, words)

    def finish(self):
        self.words(5, [1, 0, 0, 0, 320, 240, 320, 240, 0, 0, 0, 0])
        self.f.close()


def generate(directory):
    vram = [0]*(1024*512)
    for y in range(256):
        for x in range(256):
            vram[y*1024+640+x] = (31 if x&1 else 4) | ((31 if y&1 else 4)<<5) | (((x+y)%31+1)<<10)
    t = Trace(directory/"polygon-sampling.gputrace")
    t.init(vram)
    # One axis-aligned quad and an oblique quad with the same texture.
    for positions in [[(10,10),(110,10),(10,110),(110,110)], [(140,10),(240,35),(120,110),(230,120)]]:
        words = [0x2d808080]
        for i, ((x,y),(u,v)) in enumerate(zip(positions, [(0,0),(100,0),(0,100),(100,100)])):
            words += [xy(x,y), uv(u,v,0x10a if i==1 else 0)]
        t.commands(words)
    t.finish()
    # A masked red destination, an additive checker of transparent and STP blue
    # texels, then a mask-checking green rectangle. Transparent texels must leave
    # the original red and its mask intact. Green may never appear in that cell.
    vram = [0]*(1024*512)
    for y in range(64):
        for x in range(64):
            vram[y*1024+640+x] = 0xfc00 if ((x//8+y//8)&1) else 0
    t = Trace(directory/"transparent-add-mask.gputrace")
    t.init(vram)
    t.commands([0xe6000001, 0x600000ff, xy(16,16), xy(64,64), 0xe6000000,
                0xe100012a, 0x67808080, xy(16,16), uv(0,0), xy(64,64),
                0xe6000002, 0x6000ff00, xy(16,16), xy(64,64)])
    t.finish()
    for name, mask, page, command in [
            ("stp-half-mask", 0, 0x10a, 0x65808080),
            ("stp-mask-order", 2, 0x10a, 0x65808080),
            ("stp-sub-mask", 0, 0x14a, 0x67808080),
            ("stp-quarter-mask", 0, 0x16a, 0x67808080),
            ("forced-add-mask", 1, 0x12a, 0x67808080)]:
        vram = [0]*(1024*512)
        for y in range(64):
            for x in range(64):
                stp = (x//8+y//8)&1
                # Opaque blue and STP red; for force-mask, transparent and
                # opaque blue test both fragment rejection and forced alpha.
                vram[y*1024+640+x] = (0x7c00 if stp else 0) if mask==1 else (0x801f if stp else 0x7c00)
        t = Trace(directory/(name+".gputrace"))
        t.init(vram)
        t.commands([0xe6000000 | mask, 0xe1000000 | page,
                    command, xy(16,16), uv(0,0), xy(64,64),
                    0xe6000002, 0x6000ff00, xy(16,16), xy(64,64)])
        t.finish()
    # Two textured rectangles with identical draw state: the second must see
    # STP written by the first even when the renderer would batch them.
    vram=[0]*(1024*512)
    for y in range(64):
        for x in range(128):
            vram[y*1024+640+x]=0x801f if x<64 else 0x7c00
    t=Trace(directory/"stp-batch-order.gputrace")
    t.init(vram)
    t.commands([0xe6000002, 0x65808080, xy(16,16), uv(0,0), xy(64,64),
                0x65808080, xy(16,16), uv(64,0), xy(64,64)])
    t.finish()
    # Known remaining approximation: E6 mask-check currently turns half blend
    # into replacement on unmasked pixels. Keep a visible repro, not a passing
    # assertion claiming all mask combinations are solved.
    vram=[0]*(1024*512)
    for y in range(64):
        for x in range(64):
            vram[(y+16)*1024+x+16]=0x7c00
            vram[y*1024+640+x]=0x801f
    t=Trace(directory/"known-mask-half-blend.gputrace")
    t.init(vram)
    t.commands([0xe6000002,0x67808080,xy(16,16),uv(0,0),xy(64,64)])
    t.finish()
    return ["polygon-sampling", "transparent-add-mask", "stp-half-mask", "stp-mask-order", "forced-add-mask", "stp-sub-mask", "stp-quarter-mask", "stp-batch-order", "known-mask-half-blend"]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, default=Path("/tmp/correct-sim"))
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--pica", type=Path)
    p.add_argument("--check", action="store_true")
    args=p.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    for name in generate(args.output):
        for renderer in ["peops", "pica"]:
            dest=args.output/name/renderer
            dest.mkdir(parents=True,exist_ok=True)
            binary=args.pica if renderer=="pica" and args.pica else args.build/("replay-"+renderer)
            subprocess.run([binary,args.output/(name+".gputrace"),dest],check=True)
        reference=args.output/name/"peops/frame-0001.ppm"
        pica=args.output/name/"pica/frame-0001.ppm"
        print(name,images(reference,pica,args.output,name))
        if args.check and name=="transparent-add-mask":
            from PIL import Image
            expected=Image.open(reference).getpixel((20,20))
            actual=Image.open(pica).getpixel((20,20))
            assert expected==(255,0,0) and actual==expected, (expected,actual)
        if args.check and name in ["transparent-add-mask", "stp-half-mask", "stp-mask-order", "forced-add-mask", "stp-batch-order"]:
            from PIL import Image, ImageChops
            assert ImageChops.difference(Image.open(reference),Image.open(pica)).getbbox() is None, name
        if args.check and name in ["stp-sub-mask", "stp-quarter-mask"]:
            from PIL import Image
            im=Image.open(pica)
            assert im.getpixel((20,20))==(0,255,0) and im.getpixel((28,20))[1]==0, name


if __name__=="__main__":
    main()
