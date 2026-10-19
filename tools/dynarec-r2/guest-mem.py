#!/usr/bin/env python3
"""Classify executed guest loads/stores: does an earlier access in the same straight-line run share an unmodified base register?"""
import collections, re, struct, sys
from pathlib import Path
root = Path(sys.argv[1]); key = sys.argv[2]
d = root/f'{key}-600'
def load(dd):
    v = {}
    for line in (dd/'cache.txt.detail').read_text().splitlines():
        p, w, *x = line.split(); v[int(p,16)] = int(x[0])
    return v
b = load(root/f'{key}-300'); a = load(d)
lo = int(re.search(r'code arena ([0-9a-f]+)', (d/'run.log').read_text())[1], 16)
tags = (d/'code.tags').read_bytes(); pcs = struct.unpack('<%dI' % len(tags), (d/'code.pcs').read_bytes())
ram = (d/'code.ram').read_bytes()
cnt = collections.Counter()
for p, v in a.items():
    i = (p-lo)//4
    if 0 <= i < len(pcs):
        n = (v-b.get(p,0))/300
        if n > cnt[pcs[i]]: cnt[pcs[i]] = n
def word(pc): o = pc & 0x1ffffc; return struct.unpack_from('<I', ram, o)[0]
def dec(w):
    op = w >> 26; rs = (w>>21)&31; rt = (w>>16)&31; rd = (w>>11)&31
    ld = op in (0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x32); st = op in (0x28,0x29,0x2a,0x2b,0x2e,0x3a)
    br = op in (2,3,4,5,6,7) or (op == 0 and (w & 0x3f) in (8,9)) or op == 1 or (op == 0x10 and rs == 8)
    # destination register written
    wr = None
    if op == 0: wr = rd if (w & 0x3f) not in (8, 0x18,0x19,0x1a,0x1b,0x11,0x13,0xc,0xd) else None
    elif op == 3: wr = 31
    elif op in (8,9,10,11,12,13,14,15) or ld and op != 0x32: wr = rt
    elif op == 0x10 and rs in (0,2): wr = rt
    elif op == 0x12 and rs in (0,2): wr = rt
    if op == 0 and (w & 0x3f) == 9: wr = rd
    return ld, st, br, rs, wr, (w & 0xffff) - ((w & 0x8000) << 1), op
import subprocess, tempfile
code = (d/'code.bin').read_bytes()
with tempfile.NamedTemporaryFile(suffix='.bin') as f:
    f.write(code); f.flush()
    dis = subprocess.check_output(['arm-linux-gnueabihf-objdump','-D','-b','binary','-marm','--adjust-vma=%#x'%lo, f.name], text=True)
htype = {}
for l in dis.splitlines():
    m = re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]+\s+(.*)', l)
    if not m: continue
    i = (int(m[1],16)-lo)//4
    if not 0 <= i < len(pcs) or tags[i] != 3: continue
    t = m[2]
    if '528482304' in t and t.startswith('eor'): htype[pcs[i]] = 'spad'
    elif 'cmp' in t and '#128, 18' in t: htype.setdefault(pcs[i], 'ram')
res = collections.Counter(); samepage = collections.Counter()
for pc, n in cnt.items():
    if n < 0.5 or not (0x80000000 <= pc < 0x80200000): continue
    ld, st, br, base, wr, imm, op = dec(word(pc))
    if not (ld or st): continue
    kind = 'load' if ld else 'store'
    if base == 29: kindb = 'sp'
    elif base == 0: kindb = 'zero'
    else: kindb = 'other'
    found = None; allmem = True
    for k in range(1, 40):
        q = pc - 4*k
        if q not in cnt and k > 1: pass
        l2, s2, b2, r2, w2, i2, o2 = dec(word(q))
        # stop if previous instruction is a branch (q is a delay slot boundary start)
        lb, sb, bb, *_ = dec(word(q - 4))
        if (l2 or s2) and r2 == base: found = (k, abs(i2 - imm), allmem); break
        if not (l2 or s2): allmem = False
        if w2 == base: break
        if b2 or bb: break
    tag = 'first'
    if found: tag = ('shared<=32' if found[1] <= 32 else ('shared<4k' if found[1] < 4096 else 'shared-far')) + ('/adj' if found[2] else '/gap')
    res[(kind, htype.get(pc,'none'), tag.split('/')[0])] += n
tot = sum(res.values())
print(key, 'executed guest memory ops/vblank %.0f' % tot)
for k, v in sorted(res.items(), key=lambda x: -x[1]): print('  %-6s %-6s %-11s %8.0f %5.1f%%' % (*k, v, 100*v/tot))
