#!/usr/bin/env python3
"""Ordering probes for additive batching; compare scanout and all VRAM mask bits.

Use --baseline and --candidate built from the same renderer correctness level.
The existing mask fixtures can additionally check the STP-corrected variant.
"""
import argparse
from pathlib import Path
import os
import re
import subprocess
from fixtures import Trace, xy


def quad(t, x, y, color, semi=False):
    t.commands([(0x2a000000 if semi else 0x28000000) | color,
                xy(x,y),xy(x+8,y),xy(x,y+8),xy(x+8,y+8)])


def generate(directory):
    names = ['disjoint', 'overlap', 'capacity', 'area-mask-upload', 'vertex-wrap']
    for name in names:
        t = Trace(directory/(name+'.gputrace'))
        t.init([0]*(1024*512))
        t.commands([0xe100002a])
        count = 3000 if name=='vertex-wrap' else 96 if name=='capacity' else 24
        for i in range(count):
            x,y = 10+(i%24)*12,10+((i//24)%16)*12
            if name=='overlap':
                x,y = 20,20
            quad(t,x,y,0x080808)
            quad(t,x,y,0x100008,True)
            if name=='area-mask-upload' and i%3==0:
                # A different area, E6 state, and CPU VRAM upload are barriers.
                t.commands([0xe3000000|12|(12<<10),0xe6000001])
                quad(t,x,y,0x001000)
                t.commands([0xe6000002])
                quad(t,x,y,0x100000)
                t.commands([0xe6000000,0xe3000000])
                t.words(3,[300,230,1,1,0]);t.record(4,b'\x1f\x00')
        t.finish()
    return names


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    for name in generate(a.output):
        logs=[];pictures=[]
        for label,binary in [('before',a.baseline),('after',a.candidate)]:
            d=a.output/name/label;d.mkdir(parents=True,exist_ok=True)
            run=subprocess.run([binary,a.output/(name+'.gputrace'),d],env=dict(os.environ,GPU_HASH_VRAM='1'),capture_output=True,text=True,check=True)
            (d/'replay.log').write_text(run.stderr)
            logs.append(re.findall(r'VRAM hash (\w+)',run.stderr));pictures.append((d/'frame-0001.ppm').read_bytes())
        assert logs[0] and logs[0]==logs[1],(name,logs)
        assert pictures[0]==pictures[1],name
        print(name,'scanout + full VRAM match')


if __name__=='__main__':
    main()
