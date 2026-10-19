#!/usr/bin/env python3
"""Executed generated instructions per vblank by tag and normalized mnemonic/operand shape."""
import collections, re, struct, subprocess, sys, tempfile
from pathlib import Path
root = Path(sys.argv[1]); key = sys.argv[2]; want = sys.argv[3] if len(sys.argv) > 3 else None
names = ['guest','state','cycle','mem','slow','branch','save','gte']
def load(d):
    v = {}
    for line in (d/'cache.txt.detail').read_text().splitlines():
        p, w, *x = line.split(); v[int(p,16)] = list(map(int, x))
    return v
b = load(root/f'{key}-300'); a = load(root/f'{key}-600'); d = root/f'{key}-600'
lo = int(re.search(r'code arena ([0-9a-f]+)', (d/'run.log').read_text())[1], 16)
code = (d/'code.bin').read_bytes(); tags = (d/'code.tags').read_bytes()
with tempfile.NamedTemporaryFile(suffix='.bin') as f:
    f.write(code); f.flush()
    dis = subprocess.check_output(['arm-linux-gnueabihf-objdump','-D','-b','binary','-marm','--adjust-vma=%#x'%lo, f.name], text=True)
lines = {}
for l in dis.splitlines():
    m = re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]+\s+(.*)', l)
    if m: lines[int(m[1],16)] = m[2]
def shape(t):
    t = t.split('@')[0].strip()
    t = re.sub(r'\b(r\d+|sl|fp|ip|lr|sb)\b', 'R', t)
    t = re.sub(r'#-?\d+(, \d+)?', '#n', t)
    t = re.sub(r'0x[0-9a-f]+', 'ADDR', t)
    return t
c = collections.Counter(); tot = collections.Counter()
for p, v in a.items():
    i = (p-lo)//4
    if not 0 <= i < len(tags): continue
    n = (v[0]-b.get(p,[0]*5)[0])/300
    tg = names[tags[i]]; tot[tg] += n
    if want is None or tg == want: c[(tg, shape(lines.get(p,'?')))] += n
for k, v in tot.most_common(): print('%-7s %9.0f' % (k, v))
print()
for k, v in c.most_common(45): print('%-7s %-40s %9.0f' % (k[0], k[1], v))
