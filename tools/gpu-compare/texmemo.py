#!/usr/bin/env python3
"""Exercise texture memo collisions, eviction, growth, uploads and state reset."""
import argparse
import os
from pathlib import Path
import re
import struct
import subprocess
from fixtures import Trace, xy, uv


def generate(path):
    vram = [0] * (1024 * 512)
    for y in range(256):
        for x in range(128):
            vram[y * 1024 + 640 + x] = 0x1234 ^ ((x + y) & 7) * 0x1111
    for clut in range(128):
        for i in range(16):
            vram[480 * 1024 + clut * 16 + i] = ((i + 1) | ((clut & 31) << 5) | ((31 - i) << 10))
    t = Trace(path)
    t.init(vram)
    t.commands([0xe4000000 | 319 | (239 << 10)])
    # Full-page squares exhaust even the large atlas; CLUTs collide in memo.
    for turn in range(2):
        for clut in range(128):
            for size in [32, 256, 32]:
                t.commands([0xe100000a, 0x65808080, xy(0, 0),
                            uv(0, 0, (480 + clut // 64) * 64 + clut % 64), xy(size, size)])
        # Same-address palette update and masked texture window.
        t.words(3, [1, 480, 1, 1, 0])
        t.record(4, struct.pack('<H', 0x7c00 if turn else 0x001f))
        for win in [1, 0, 0x421, 0]:
            t.commands([0xe2000000 | win, 0x65808080, xy(16, 16), uv(0, 0, 480 * 64), xy(64, 64)])
        # A state replacement retires memo pointers even at the same addresses.
        t.record(9, b'')
        t.words(3, [0, 0, 1024, 512, 1])
        t.record(4, struct.pack('<' + 'H' * len(vram), *vram))
    t.finish()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--replay', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    trace = a.output / 'texmemo.gputrace'
    generate(trace)
    expected = None
    for mask, memo in [(0, 0), (1, 0), (0, 1), (1, 1)]:
        dest = a.output / f'mask{mask}-memo{memo}'
        dest.mkdir(exist_ok=True)
        result = subprocess.run([a.replay, trace, dest], check=True, capture_output=True, text=True,
            env=dict(os.environ, TEXMASK=str(mask), TEXMEMO=str(memo), GPU_HASH_VRAM='1',
                     PICA_STATS='1', SIM_CHECK_STAGE_LIFETIME='1', SIM_CHECK_ATLAS_LIFETIME='1'))
        (dest / 'replay.log').write_text(result.stderr)
        actual = (re.findall(r'VRAM hash (\w+)', result.stderr),
                  [(f.name, f.read_bytes()) for f in sorted(dest.glob('*.ppm'))])
        assert actual[0] and actual[1]
        if expected is None:
            expected = actual
        assert actual == expected, (mask, memo)
    print('texture memo eviction/growth/collision/reset: complete VRAM and scanout match')


if __name__ == '__main__':
    main()
