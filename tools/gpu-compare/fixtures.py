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
    # All texture depths, distinct palette addresses, same-address palette
    # replacement, raw mode, and half-strength modulation. PS1 colour 31 *
    # vertex 64 / 128 is 15, not 16 (replicating the low bits produces 16).
    vram=[0]*(1024*512)
    for depth in range(3):
        px=640+depth*64
        for y in range(32):
            for x in range(32 if depth==2 else 16 if depth==1 else 8):
                vram[y*1024+px+x]=0x7fff if depth==2 else 0x0101 if depth==1 else 0x1111
    vram[480*1024+1]=0x7fff
    vram[481*1024+1]=0x001f
    t=Trace(directory/"texture-colours.gputrace")
    t.init(vram)
    for depth in range(3):
        x=16+depth*80
        t.commands([0xe1000000|(10+depth)|(depth<<7),
                    0x65404040,xy(x,16),uv(0,0,480<<6),xy(32,32),
                    0x64404040,xy(x,56),uv(0,0,480<<6),xy(32,32)])
    # Switching CLUT must find a different entry.
    t.commands([0xe100000a,0x65808080,xy(16,96),uv(0,0,481<<6),xy(32,32)])
    # Updating the original CLUT must invalidate its existing decoded entry.
    t.words(3,[1,480,1,1,0]);t.record(4,struct.pack("<H",0x7c00))
    t.commands([0x65808080,xy(56,96),uv(0,0,480<<6),xy(32,32)])
    t.finish()
    vram=[0]*(1024*512)
    for y in range(32):
        for x in range(32):vram[y*1024+640+x]=16|(16<<5)|(16<<10)
    t=Trace(directory/"ordered-dither.gputrace")
    t.init(vram)
    t.commands([0xe100030a])
    # Eligible gouraud and modulated polygons, plus flat/raw/sprite exclusions.
    for x,cmd in [(17,0x38),(65,0x3c),(113,0x20),(161,0x2d)]:
        positions=[(x,17),(x+32,17),(x,49),(x+32,49)] if cmd&8 else [(x,17),(x+32,17),(x,49)]
        words=[(cmd<<24)|0x6c6c6c]
        for k,(xx,yy) in enumerate(positions):
            if k and cmd&16: words.append(0x6c6c6c)
            words.append(xy(xx,yy))
            if cmd&4:words.append(uv(0 if k%2==0 else 31,0 if k<2 else 31,0x10a if k==1 else 0))
        t.commands(words)
    t.commands([0x646c6c6c,xy(209,17),uv(0,0),xy(32,32)])
    # Same gouraud under E1 off: force option should still dither it.
    t.commands([0xe100010a,0x386c6c6c,xy(17,65),0x6c6c6c,xy(49,65),0x6c6c6c,xy(17,97),0x6c6c6c,xy(49,97)])
    # Upload with a pending dithered batch: stage 1 must be reset.
    t.commands([0xe100030a,0x386c6c6c,xy(65,65),0x6c6c6c,xy(97,65),0x6c6c6c,xy(65,97),0x6c6c6c,xy(97,97)])
    t.words(3,[113,65,8,8,0]);t.record(4,struct.pack("<64H",*([0x4210]*64)))
    # Capture the option per primitive even when toggled with queued vertices.
    for x,mode in [(17,1),(65,0),(113,1)]:
        t.words(7,[mode])
        t.commands([0xe100030a,0x386c6c6c,xy(x,113),0x6c6c6c,xy(x+32,113),0x6c6c6c,xy(x,145),0x6c6c6c,xy(x+32,145)])
    t.finish()
    vram=[0]*(1024*512)
    for v in range(256):
        for u in range(256):
            vram[v*1024+640+u]=(u&31)|((v&31)<<5)|((8+(u>>5))<<10)
    t=Trace(directory/"sprite-wrap.gputrace")
    t.init(vram)
    for i,(fx,fy,u,v) in enumerate([(False,False,250,250),(True,False,5,250),(False,True,250,5),(True,True,5,5)]):
        t.commands([0xe100010a|(fx<<12)|(fy<<13),0x65808080,xy(16+i*64,16),uv(u,v),xy(40,40)])
    # More than a page, both flipped: repeated wraps then clipping.
    t.commands([0xe100310a,0x65808080,xy(16,80),uv(0,0),xy(600,300)])
    t.finish()
    return ["sprite-wrap", "ordered-dither", "texture-colours", "polygon-sampling", "transparent-add-mask", "stp-half-mask", "stp-mask-order", "forced-add-mask", "stp-sub-mask", "stp-quarter-mask", "stp-batch-order", "known-mask-half-blend"]


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
            if name=="sprite-wrap" and renderer=="peops":
                # P.E.Op.S. reads outside VRAM for the negative flipped span.
                # Use direct modulo addressing as the independent oracle.
                from PIL import Image
                im=Image.new("RGB",(320,240))
                cases=[(16+i*64,16,40,40,fx,fy,u,v) for i,(fx,fy,u,v) in enumerate([(False,False,250,250),(True,False,5,250),(False,True,250,5),(True,True,5,5)])]
                cases.append((16,80,304,160,True,True,0,0))
                for x,y,w,h,fx,fy,u,v in cases:
                    for dy in range(h):
                        for dx in range(w):
                            uu=(u+(-dx if fx else dx))&255
                            vv=(v+(-dy if fy else dy))&255
                            im.putpixel((x+dx,y+dy),tuple(c*255//31 for c in [uu&31,vv&31,8+(uu>>5)]))
                im.save(dest/"frame-0001.ppm")
                continue
            binary=args.pica if renderer=="pica" and args.pica else args.build/("replay-"+renderer)
            subprocess.run([binary,args.output/(name+".gputrace"),dest],check=True)
        reference=args.output/name/"peops/frame-0001.ppm"
        pica=args.output/name/"pica/frame-0001.ppm"
        print(name,images(reference,pica,args.output,name,"PS1 modulo addressing" if name=="sprite-wrap" else "P.E.Op.S."))
        if args.check and name=="sprite-wrap":
            from PIL import Image,ImageChops
            assert ImageChops.difference(Image.open(reference),Image.open(pica)).getbbox() is None, "sprite wrap"
        if args.check and name=="ordered-dither":
            from PIL import Image
            import os
            im=Image.open(pica)
            mode=int(os.environ.get("GPU_DITHER","1"))
            matrix=[[-4,0,-3,1],[2,-2,3,-1],[-3,1,-4,0],[3,-1,2,-2]]
            for base,enabled in [((20,20),True),((68,20),True),((20,68),mode==2),((68,68),True)]:
                for y in range(base[1],base[1]+8):
                    for x in range(base[0],base[0]+8):
                        level=(108+(matrix[y&3][x&3] if enabled and mode else 0))>>3
                        expected=(level*255//31,)*3
                        assert im.getpixel((x,y))==expected,(x,y,im.getpixel((x,y)),expected)
            for base,enabled in [((20,116),True),((68,116),False),((116,116),True)]:
                for y in range(base[1],base[1]+8):
                    for x in range(base[0],base[0]+8):
                        level=(108+(matrix[y&3][x&3] if enabled else 0))>>3
                        assert im.getpixel((x,y))==(level*255//31,)*3,("option change",x,y)
            for point,level in [((116,20),13),((164,20),16),((212,20),13),((116,68),16)]:
                assert im.getpixel(point)==(level*255//31,)*3,("exclusion/upload",point,im.getpixel(point))
        if args.check and name=="texture-colours":
            from PIL import Image
            for path in [reference,pica]:
                im=Image.open(path)
                for x in [20,100,180]:
                    assert im.getpixel((x,20))==(255,255,255), (path,x,"raw")
                    assert im.getpixel((x,60))==(123,123,123), (path,x,"modulate")
                assert im.getpixel((20,100))==(255,0,0), (path,"CLUT selection")
                assert im.getpixel((60,100))==(0,0,255), (path,"CLUT invalidation")
        if args.check and name=="transparent-add-mask":
            from PIL import Image
            expected=Image.open(reference).getpixel((20,20))
            actual=Image.open(pica).getpixel((20,20))
            assert expected==(255,0,0) and actual==expected, (expected,actual)
        if args.check and name in ["transparent-add-mask", "stp-half-mask", "stp-mask-order", "forced-add-mask", "stp-batch-order", "stp-sub-mask", "stp-quarter-mask"]:
            from PIL import Image, ImageChops
            assert ImageChops.difference(Image.open(reference),Image.open(pica)).getbbox() is None, name



if __name__=="__main__":
    main()
