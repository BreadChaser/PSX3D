#!/usr/bin/env python3
"""Render-to-texture ordering and native VRAM-copy probes (no game data)."""
import argparse
import os
from pathlib import Path
import re
import subprocess
from fixtures import Trace, xy, uv


def direct(path):
    vram = [0] * (1024 * 512)
    # All 65536 combinations of RGB5 and mask, not just bright primaries.
    for y in range(256):
        for x in range(256):
            vram[(256+y)*1024+x] = y*256+x
    t = Trace(path)
    t.init(vram)
    t.commands([0xe4000000 | 1023 | (511 << 10), 0xe1000110])
    t.commands([0x65808080, xy(640,0), uv(0,0), xy(256,256)])
    for page, x, y in [(0x10a,0,0), (0x10a,256,0), (0x100,640,256)]:
        t.commands([0xe1000000 | page, 0x65808080, xy(x,y), uv(0,0), xy(256,256)])
    # A second write while CPU VRAM is still stale must invalidate a GPU
    # snapshot. Also exercise atlas/vertex rollover with many snapshots.
    for i in range(96):
        t.commands([0x60000000 | ((i*8)&255), xy(640,0), xy(32,32),
                    0xe100010a, 0x65808080, xy(0,0), uv(0,0), xy(32,32)])
    # Windowed and wrapping direct reads retain the precise CPU fallback.
    t.commands([0xe2000001, 0x65808080, xy(32,32), uv(0,0), xy(32,32), 0xe2000000])
    t.finish()


def copy(path):
    t = Trace(path)
    t.init([0]*(1024*512))
    t.commands([0xe4000000 | 1023 | (511 << 10), 0x600000ff, xy(640,0), xy(64,32)])
    # A copy command and its captured CPU upload are paired. New PICA
    # executes the former; the baseline and reference execute the latter.
    t.words(8, [0x80000000, xy(640,0), xy(0,256), xy(64,32)])
    t.words(3, [0,256,64,32,0])
    t.record(4, b'\x1f\x00'*(64*32))
    t.commands([0xe1000110,0x65808080,xy(0,0),uv(0,0),xy(64,32)])
    t.finish()


def palette(path):
    vram=[0]*(1024*512)
    for y in range(32):
        for x in range(8): vram[y*1024+640+x]=0x1111
        for x in range(16): vram[y*1024+704+x]=0x0101
    vram[480*1024+1]=31
    t=Trace(path); t.init(vram)
    t.commands([0xe4000000 | 1023 | (511<<10)])
    for depth,page in [(0,10),(1,0x8b)]:
        t.commands([0xe1000000|page,0x65808080,xy(0,depth*64),uv(0,0,480<<6),xy(32,32)])
        # Adjacent CPU row shares native GPU tiles, but no palette words.
        t.words(3,[0,481,256,1,0]); t.record(4,b'\x00\x7c'*256)
        t.commands([0x65808080,xy(40,depth*64),uv(0,0,480<<6),xy(32,32)])
        # Same-address CPU palette replacement must still invalidate.
        t.words(3,[1,480,1,1,0]); t.record(4,b'\x00\x7c')
        t.commands([0x65808080,xy(80,depth*64),uv(0,0,480<<6),xy(32,32)])
        # A GPU-written palette requires a precise CPU readback.
        t.commands([0x6000ff00,xy(1,480),xy(1,1),0x65808080,xy(120,depth*64),uv(0,0,480<<6),xy(32,32)])
    t.finish()


def stage(path):
    t = Trace(path)
    t.init([0]*(1024*512))
    # Retire the state-load stage, then exhaust vertices while stage rows
    # remain available. Upload reservation must survive the rollover.
    t.words(3, [800,400,1,1,0]); t.record(4,b'\x1f\x00')
    for i in range(2729):
        t.commands([0x600000ff,xy(0,0),xy(1,1)])
    for i in range(4):
        t.words(3, [800+i,400,1,1,0]); t.record(4,bytes([i+1,0]))
    t.finish()


def regions(path):
    """Scattered sprites sharing a CLUT must retain independent atlas squares."""
    vram = [0]*(1024*512)
    for y in range(256):
        for x in range(64):
            vram[y*1024+640+x] = 0x1234 ^ ((x+y)&7)*0x1111
    for clut in range(24):
        for i in range(16):
            vram[480*1024+clut*16+i] = (i+1) | ((clut+1)<<5) | ((31-i)<<10)
    t = Trace(path); t.init(vram)
    t.commands([0xe100000a])
    for frame in range(1, 5):
        for clut in range(24):
            for j, (u, v) in enumerate([(0,0), (224,224), (32,160), (192,32)]):
                x, y = (clut%8)*38+j*2, (clut//8)*64+j*4
                t.commands([0x65808080, xy(x,y), uv(u,v,(480<<6)|clut), xy(16,16)])
            # Oblique polygons land exactly on integer texel boundaries.
            # The oracle must be invariant to the square's atlas placement.
            t.commands([0x25808080, xy(x+2,y+18), uv(192,32,(480<<6)|clut),
                        xy(x+17,y+23), uv(205,32,10), xy(x+4,y+38), uv(192,47)])
        # Source and palette replacements must invalidate every overlapping
        # retained square, while queued readers retain their old contents.
        t.words(3, [640,0,8,32,0]); t.record(4, b'\x21\x43'*(8*32))
        t.words(3, [1,480,1,1,0]); t.record(4, bytes([frame+1,0]))
        t.words(5, [frame,0,0,0,320,240,320,240,0,0,0,0])
    t.f.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--replay', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    for name, generate in [('direct', direct), ('copy', copy), ('stage', stage), ('palette', palette), ('regions', regions)]:
        trace = a.output / (name+'.gputrace')
        generate(trace)
        hashes = []
        for label, debug in [('old', '262144'), ('cache-off', '524288'), ('new', '0')]:
            dest = a.output/name/label
            dest.mkdir(parents=True, exist_ok=True)
            result = subprocess.run([a.replay,trace,dest], check=True, capture_output=True, text=True,
                env=dict(os.environ,PICA_DEBUG=debug,GPU_HASH_VRAM='1',PICA_STATS='1',SIM_CHECK_STAGE_LIFETIME='1',SIM_CHECK_ATLAS_LIFETIME='1'))
            (dest/'replay.log').write_text(result.stderr)
            hashes.append(re.findall(r'VRAM hash (\w+)',result.stderr))
        assert hashes[0] and hashes[0] == hashes[1] == hashes[2], (name, hashes)
        for picture in (a.output/name/'old').glob('*.ppm'):
            assert picture.read_bytes() == (a.output/name/'new'/picture.name).read_bytes()
            assert picture.read_bytes() == (a.output/name/'cache-off'/picture.name).read_bytes()
        print(name, 'scanout and complete VRAM match')


if __name__ == '__main__':
    main()
