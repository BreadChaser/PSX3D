#!/usr/bin/env python3
"""Render real formatter output offline. Requires cc and ImageMagick.
Uses an approximate desktop font; this is not a hardware screenshot.
Run from the repository root: python3 tests/statsview/mockup.py
"""
import subprocess
import tempfile
from html import escape
from pathlib import Path

with tempfile.TemporaryDirectory() as tmp:
    exe = str(Path(tmp) / 'check')
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wno-unused-parameter',
                    'tests/statsview/check.c', '-o', exe], check=True)
    rows = subprocess.check_output([exe], text=True).splitlines()[:14]
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="320" height="240">',
           '<rect width="320" height="240" fill="black"/>',
           '<g font-family="DejaVu Sans" font-size="10" fill="#afb7c6">']
    for i, row in enumerate(rows):
        color = '#7dd7b9' if i == 1 else '#afb7c6'
        svg.append(f'<text x="6" y="{12+i*12}" fill="{color}">{escape(row)}</text>')
    svg.append('<rect x="6" y="176" width="308" height="3" fill="#dc825a"/>')
    for x, w, label in [(8,110,'L2'), (125,70,'MENU'), (202,110,'R2')]:
        svg.append(f'<rect x="{x}" y="184" width="{w}" height="30" fill="#1c1e2a"/>')
        svg.append(f'<text x="{x+w/2}" y="203" text-anchor="middle">{label}</text>')
    svg.append('<text x="6" y="231">Tap here: Off / FPS / Detailed</text></g></svg>')
    path = Path(tmp)/'mockup.svg'
    path.write_text(''.join(svg))
    subprocess.run(['magick', '-background', 'black', '-density', '288', str(path),
                    '-resize', '320x240', 'docs/statsview-mockup.png'], check=True)
print('host checks passed; wrote docs/statsview-mockup.png')
