#!/usr/bin/env python3
"""Locate triangles whose size test changes with per-vertex offset wrapping.

This decodes command/state records; it is diagnostic evidence, not a rasterizer.
Packet words are relative to their trace record (whose byte offset is included).
"""
import argparse
import json
from pathlib import Path
import re
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("trace", type=Path)
p.add_argument("--frames", nargs="+", type=int, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
s = (repo / "core/plugins/gpulib/gpu.c").read_text().split("const unsigned char cmd_lengths[256]", 1)[1]
s = s[s.index("{")+1:s.index("}")]
s = re.sub(r"/\*.*?\*/|//[^\n]*", "", s, flags=re.S)
lengths = list(map(int, re.findall(r"\b\d+\b", s)))
assert len(lengths) == 256

def sx(v):
    return ((v + 1024) & 2047) - 1024

area = [0, 0, 1024, 512]
offset = [0, 0]
def ex(word):
    cmd = word >> 24
    if cmd == 0xe3:
        area[:2] = [word & 1023, (word >> 10) & 511]
    if cmd == 0xe4:
        area[2:] = [(word & 1023)+1, ((word >> 10) & 511)+1]
    if cmd == 0xe5:
        offset[:] = [sx(word), sx(word >> 11)]

frame, pending, result = 1, [], []
with a.trace.open("rb") as f:
    assert f.read(8) == b"PSXGPU01"
    while True:
        record = f.tell()
        h = f.read(8)
        if not h:
            break
        tag, size = struct.unpack("<II", h)
        data = f.read(size)
        if tag == 2:
            for w in struct.unpack("<8I", data):
                ex(w)
        if tag == 5:
            info = struct.unpack("<12i", data)
            if info[0] in a.frames:
                result.append({"frame": info[0], "scanout": info[2:6], "triangles": pending})
            pending = []
            frame = info[0]+1
        if tag != 1:
            continue
        ws = struct.unpack("<"+"I"*(size//4), data)
        i = 0
        while i < len(ws):
            cmd = ws[i] >> 24
            n = lengths[cmd]+1
            if 0x20 <= cmd < 0x40 and frame in a.frames:
                raw, j = [], i+1
                for k in range(4 if cmd & 8 else 3):
                    if cmd & 16 and k:
                        j += 1
                    raw.append([sx(ws[j]), sx(ws[j] >> 16)])
                    j += 1+bool(cmd & 4)
                for tri in ([0, 1, 2], [1, 3, 2]) if cmd & 8 else ([0, 1, 2],):
                    plain = [[raw[k][d]+offset[d] for d in range(2)] for k in tri]
                    old = [[sx(c) for c in v] for v in plain]
                    lo = [min(v[d] for v in plain) for d in range(2)]
                    hi = [max(v[d] for v in plain) for d in range(2)]
                    oldspan = [max(v[d] for v in old)-min(v[d] for v in old) for d in range(2)]
                    valid = hi[0]-lo[0] <= 1023 and hi[1]-lo[1] <= 511
                    oldvalid = oldspan[0] <= 1023 and oldspan[1] <= 511
                    if valid == oldvalid:
                        continue
                    delta = [sx(c)-c for c in lo]
                    correct = [[v[d]+delta[d] for d in range(2)] for v in plain]
                    visible = correct if valid else old
                    clo = [min(v[d] for v in visible) for d in range(2)]
                    chi = [max(v[d] for v in visible) for d in range(2)]
                    if chi[0] <= area[0] or clo[0] >= area[2] or chi[1] <= area[1] or clo[1] >= area[3]:
                        continue
                    pending.append({"kind": "recovered" if valid else "oversize_removed",
                        "record_byte": record, "packet_word": i, "command": hex(cmd),
                        "triangle": tri, "raw": [raw[k] for k in tri], "offset": offset[:],
                        "old": old, "old_span": oldspan, "correct": correct,
                        "raw_span": [hi[d]-lo[d] for d in range(2)], "area": area[:]})
            if cmd >= 0xe0:
                ex(ws[i])
            if cmd & 0xf8 in (0x48, 0x58):
                step = 2 if cmd & 16 else 1
                n = 4 if step == 2 else 3
                while i+n < len(ws) and ws[i+n] & 0xf000f000 != 0x50005000:
                    n += step
                n += 1
            i += n
a.output.write_text(json.dumps(result, indent=2)+"\n")
for item in result:
    print(item["frame"], "scanout", item["scanout"],
          {kind: sum(t["kind"] == kind for t in item["triangles"])
           for kind in ("recovered", "oversize_removed")})
