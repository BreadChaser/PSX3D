#!/usr/bin/env python3
"""Summarize which GPU features a captured trace actually exercises."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import struct


def inspect(trace, lengths):
    counters={name: Counter() for name in ["primitives", "blend", "texture_depth", "mask", "polygon_dither", "texture_window", "scanout", "dimensions"]}
    page=window=mask=0
    with trace.open("rb") as f:
        assert f.read(8)==b"PSXGPU01"
        while header:=f.read(8):
            tag,size=struct.unpack("<II",header)
            if tag not in [1,2,5]:
                f.seek(size,1)
                continue
            data=f.read(size)
            if tag==2:
                regs=struct.unpack("<8I",data)
                page,window,mask=regs[1]&0x3fff,regs[2]&0xfffff,regs[6]&3
            elif tag==5:
                info=struct.unpack("<12i",data)
                counters["scanout"]["24-bit" if info[1]&(1<<21) else "15-bit"]+=1
                counters["dimensions"][f"{info[4]}x{info[5]}"]+=1
            else:
                words=struct.unpack("<"+"I"*(size//4),data)
                i=0
                while i<len(words):
                    cmd=words[i]>>24
                    count=lengths[cmd]+1
                    if cmd==0xe1:page=words[i]&0x3fff
                    elif cmd==0xe2:window=words[i]&0xfffff
                    elif cmd==0xe6:mask=words[i]&3
                    if 0x20<=cmd<0x80:
                        kind="polygon" if cmd<0x40 else "line" if cmd<0x60 else "rectangle"
                        counters["primitives"][kind]+=1
                        if kind=="polygon" and cmd&4:
                            tp=words[i+(5 if cmd&16 else 4)]>>16
                            page=(page&~0x9ff)|(tp&0x9ff)
                        textured=kind!="line" and cmd&4
                        if textured:
                            counters["texture_depth"][str(min(2,(page>>7)&3))]+=1
                            counters["texture_window"]["on" if window&0x3ff else "off"]+=1
                        counters["mask"][str(mask)]+=1
                        counters["blend"][str((page>>5)&3) if cmd&2 else "opaque"]+=1
                        if kind=="polygon":counters["polygon_dither"]["on" if page&0x200 else "off"]+=1
                    if cmd&0xf8 in [0x48,0x58]:
                        stride=2 if cmd&0x10 else 1
                        count=4 if stride==2 else 3
                        while i+count<len(words) and words[i+count]&0xf000f000!=0x50005000:count+=stride
                        count+=1
                    i+=count
    return counters


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("traces",nargs="+",type=Path)
    args=p.parse_args()
    source=(Path(__file__).resolve().parents[2]/"core/plugins/gpulib/gpu.c").read_text()
    source=source[source.index("const unsigned char cmd_lengths"):]
    source=source[source.index("{")+1:source.index("}")]
    source=re.sub(r"/\*.*?\*/|//[^\n]*","",source,flags=re.S)
    lengths=[int(x) for x in re.findall(r"\b\d+\b",source)]
    assert len(lengths)==256
    print(json.dumps({trace.stem:inspect(trace,lengths) for trace in args.traces},indent=2))
