#!/usr/bin/env python3
"""Disassemble the hottest generated code (frames 301..600) with per-word counts and tags."""
import collections, re, struct, subprocess, sys, tempfile
from pathlib import Path
root = Path(sys.argv[1]); key = sys.argv[2]; nguest = int(sys.argv[3]) if len(sys.argv) > 3 else 12
names = ['guest','state','cycle','mem','slow','branch','save','gte']
def load(d):
    v = {}
    for line in (d/'cache.txt.detail').read_text().splitlines():
        p, w, *x = line.split(); v[int(p,16)] = list(map(int, x))
    return v
b = load(root/f'{key}-300'); a = load(root/f'{key}-600')
d = root/f'{key}-600'
lo = int(re.search(r'code arena ([0-9a-f]+)', (d/'run.log').read_text())[1], 16)
code = (d/'code.bin').read_bytes(); tags = (d/'code.tags').read_bytes()
pcs = struct.unpack('<%dI' % len(tags), (d/'code.pcs').read_bytes())
cnt = {p: (v[0]-b.get(p,[0]*5)[0])/300 for p, v in a.items()}
im = {p: (v[1]-b.get(p,[0]*5)[1])/300 for p, v in a.items()}
byguest = collections.Counter()
for p, c in cnt.items():
    i = (p-lo)//4
    if 0 <= i < len(pcs): byguest[pcs[i]] += c
with tempfile.NamedTemporaryFile(suffix='.bin') as f:
    f.write(code); f.flush()
    dis = subprocess.check_output(['arm-linux-gnueabihf-objdump','-D','-b','binary','-marm','--adjust-vma=%#x'%lo, f.name], text=True)
lines = {}
for l in dis.splitlines():
    m = re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]+\s+(.*)', l)
    if m: lines[int(m[1],16)] = m[2]
tot = sum(byguest.values())
print('total generated instructions/vblank %.0f' % tot)
for g, c in byguest.most_common(nguest):
    print('\n=== guest %08x  %.0f ins/vblank (%.1f%%)' % (g, c, 100*c/tot))
    hs = sorted(p for p in cnt if 0 <= (p-lo)//4 < len(pcs) and pcs[(p-lo)//4] == g and cnt[p] >= 1)
    for p in hs:
        i = (p-lo)//4
        print('%08x %8.0f %5.0f %-6s %s' % (p, cnt[p], im[p], names[tags[i]], lines.get(p,'?')))
