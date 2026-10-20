#!/usr/bin/env python3
"""Exclusive emission-context and ARM-encoding attribution, frames 301..600."""
import collections
import json
from pathlib import Path
import re
import struct
import sys

root=Path(sys.argv[1])
names=['guest/constant setup','state/coherence','cycle accounting','memory/SMC/address',
       'memory slow path','branch/dispatch','call save/restore','GTE setup']
def load(d):
    vals=collections.defaultdict(lambda:[0]*5)
    words={}
    for line in (d/'cache.txt.detail').read_text().splitlines():
        p,w,*v=line.split(); p=int(p,16); words[p]=int(w,16)
        for i,x in enumerate(v): vals[p][i]+=int(x)
    return vals,words

for d in root.glob('*-600'):
    key=d.name[:-4]; before,_=load(root/(key+'-300')); after,words=load(d)
    lo=int(re.search(r'code arena ([0-9a-f]+)',(d/'run.log').read_text())[1],16)
    tags=(d/'code.tags').read_bytes(); pcs=struct.unpack('<%dI'%len(tags),(d/'code.pcs').read_bytes())
    result=collections.defaultdict(lambda:[0]*6); hot=collections.Counter(); mix=collections.Counter()
    for pc,v in after.items():
        delta=[(v[i]-before[pc][i])/300 for i in range(5)]
        if not any(delta): continue
        idx=(pc-lo)//4; tag=tags[idx]; w=words[pc]
        # Recognize cycle register and explicit cycle-state accesses even in
        # callers outside do_cc, and separate guest GPR transfers from CP2.
        single=(w&0x0c000000)==0x04000000
        if single and (w>>16&15)==11:
            ofs=w&4095
            if ofs in (64,68,600,860,864): tag=2
            elif 80<=ofs<216: tag=1
            elif 344<=ofs<600: tag=7
        elif ((w>>25)&7) in (0,1) and (w>>12&15)==10 and ((w>>21)&15) not in (8,9,10,11):
            tag=2
        row=result[names[tag]]
        for i,x in enumerate(delta): row[i]+=x
        if delta[0]>0: row[5]+=4
        hot[pcs[idx]]+=delta[0]
        if single and (w>>16&15)==11:
            mix['fp-load' if w&(1<<20) else 'fp-store']+=delta[0]
        elif (w&0x0e000000)==0x08000000: mix['multiple-load/store']+=delta[0]
        elif (w&0x0e000000)==0x0a000000: mix['branch/call']+=delta[0]
        elif single or ((w&0x0e000090)==0x90 and w&0x60): mix['other-load/store']+=delta[0]
        else: mix['ALU/other']+=delta[0]
    out={}
    for name,(ins,im,rm,wm,hz,foot) in result.items():
        out[name]=dict(instructions=ins,imiss=im,rmiss=rm,wmiss=wm,interlocks=hz,
                       cycles=ins+70*im+70*rm+4*wm+hz,executed_bytes=foot)
    data=dict(categories=out,mix=mix,hot_guest_instructions=[(hex(k),v) for k,v in hot.most_common(40)],
              arena_bytes=len(tags)*4,generated_base=hex(lo))
    (root/(key+'-detail.json')).write_text(json.dumps(data,indent=2)+'\n')
    print(key, 'arena',len(tags)*4)
    for name,v in out.items(): print(' %-23s %9.0f ins %9.0f cycles %7.0f I-misses %7d bytes'%(name,v['instructions'],v['cycles'],v['imiss'],v['executed_bytes']))
