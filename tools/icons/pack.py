#!/usr/bin/env python3
"""Pack the Dreamstale icons of icons.tsv into psx3d/romfs/icons/dreamstale.bin.

  tools/icons/pack.py ORIGINALS_DIR [out.bin] [--preview DIR]

ORIGINALS_DIR holds the 512 px PNGs (tools/icons/fetch.sh). ImageMagick
(magick) makes the masks; this script adds the stroke order and writes one
file that psx3d/source/icons.c reads. Only the masks ship, never the originals.

Layout (little endian, all images 8-bit alpha, rows top to bottom):
  "PSXI", u16 version 1, u16 pictures, u16 glyphs, u16 0
  per picture, in icons.tsv order: name[12],
    mask128, order128, mask64, order64, mask32, order32, glow64
  per bar glyph, in icons.tsv (= UI_ICON_*) order: name[12], mask16

order = where the stroke is in the draw-on, 0..254 from the first pixel drawn
to the last (the UI mock's strokeOrder(), devtools/ui-mock/js/icons.js).
glow = the 128 mask with 40 px of room on each side, blurred (sigma 7: the
mock's 0.055 x size) and shrunk to 64: drawn 1.625 x the icon's size."""
import os, pathlib, struct, subprocess, sys
from collections import deque

here = pathlib.Path(__file__).resolve().parent
root = here.parents[1]


def magick(src, n, dilate=0, glow=False):
	cmd = ['magick', str(src), '-alpha', 'extract']
	if dilate:
		cmd += ['-morphology', 'Dilate', f'Disk:{dilate}']
	cmd += ['-filter', 'Lanczos', '-resize', f'{128 if glow else n}x{128 if glow else n}!']
	if glow:
		cmd += ['-bordercolor', 'black', '-border', '40', '-blur', '0x7', '-filter', 'Lanczos', '-resize', f'{n}x{n}!']
	cmd += ['-depth', '8', 'gray:-']
	data = subprocess.run(cmd, capture_output=True, check=True).stdout
	assert len(data) == n * n, (src, n, len(data))
	return data


def stroke_order(a, n):
	"""Walking distance along the strokes from the end nearest the top-left,
	0..1 per connected part, parts staggered a little (the mock's strokeOrder)."""
	on = [v > 70 for v in a]
	dist = [-1] * (n * n)
	starts = sorted((i for i in range(n * n) if on[i]), key=lambda i: i % n + i // n)
	comps = []
	for s in starts:
		if dist[s] >= 0:
			continue
		q = deque([s]); mem = [s]; dist[s] = 0; top = 0
		while q:
			i = q.popleft(); x, y = i % n, i // n
			for dy in (-1, 0, 1):
				for dx in (-1, 0, 1):
					xx, yy = x + dx, y + dy
					if (dx or dy) and 0 <= xx < n and 0 <= yy < n:
						j = yy * n + xx
						if on[j] and dist[j] < 0:
							dist[j] = dist[i] + 1; top = max(top, dist[j]); q.append(j); mem.append(j)
		if len(mem) > 12:
			comps.append((mem, top))
	order = [-1.0] * (n * n)
	nc = len(comps)
	for k, (mem, top) in enumerate(comps):
		for i in mem:
			order[i] = (0.22 * k / (nc - 1) if nc > 1 else 0) + (0.78 if nc > 1 else 1) * dist[i] / max(1, top)
	# the antialiased fringe takes its nearest stroke pixel's order
	for _ in range(3):
		new = order[:]
		for i in range(n * n):
			if order[i] >= 0 or not a[i]:
				continue
			x, y = i % n, i // n; best = 2
			for dy in (-1, 0, 1):
				for dx in (-1, 0, 1):
					xx, yy = x + dx, y + dy
					if 0 <= xx < n and 0 <= yy < n and order[yy * n + xx] >= 0:
						best = min(best, order[yy * n + xx])
			if best < 2:
				new[i] = best
		order = new
	return bytes(min(254, round(max(0.0, o) * 254)) for o in order)


def main():
	args = [a for a in sys.argv[1:] if not a.startswith('--')]
	preview = None
	if '--preview' in sys.argv:
		preview = pathlib.Path(sys.argv[sys.argv.index('--preview') + 1]); args.remove(str(preview))
	src = pathlib.Path(args[0])
	out = pathlib.Path(args[1]) if len(args) > 1 else root / 'psx3d/romfs/icons/dreamstale.bin'
	rows = [l.split('\t') for l in (here / 'icons.tsv').read_text().splitlines() if l and not l.startswith('#')]
	pics = [r for r in rows if r[1] == 'pic']
	bars = [r for r in rows if r[1] == 'bar']
	blob = bytearray(b'PSXI' + struct.pack('<HHHH', 1, len(pics), len(bars), 0))
	for name, _, fid, _ in pics:
		png = src / f'{fid}.png'
		blob += name.encode().ljust(12, b'\0')
		for n, dil in ((128, 0), (64, 4), (32, 6)):
			m = magick(png, n, dil)
			blob += m + stroke_order(m, n)
		blob += magick(png, 64, glow=True)
		print(f'{name:10s} {fid}')
	for name, _, fid, _ in bars:
		blob += name.encode().ljust(12, b'\0') + magick(src / f'{fid}.png', 16, 14)
	out.parent.mkdir(parents=True, exist_ok=True)
	out.write_bytes(blob)
	print(f'{out}: {len(pics)} pictures, {len(bars)} bar glyphs, {len(blob)} bytes')
	if preview:   # contact sheet of every image in the file, for checking by eye
		preview.mkdir(parents=True, exist_ok=True)
		parts, off = [], 12
		for _ in pics:
			off += 12
			for n in (128, 128, 64, 64, 32, 32, 64):
				parts.append((n, blob[off:off + n * n])); off += n * n
		for _ in bars:
			off += 12; parts.append((16, blob[off:off + 256])); off += 256
		for k, (n, data) in enumerate(parts):
			subprocess.run(['magick', '-size', f'{n}x{n}', '-depth', '8', 'gray:-', '-filter', 'point', '-resize', '128x128',
				str(preview / f'{k:03d}.png')], input=bytes(data), check=True)
		subprocess.run(['magick', 'montage', *sorted(str(p) for p in preview.glob('[0-9]*.png')), '-tile', '7x', '-geometry', '+2+2',
			'-background', '#303040', str(preview / 'sheet.png')], check=True)


if __name__ == '__main__':
	main()
