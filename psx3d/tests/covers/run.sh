#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
python3 - "$TMP" <<'PY'
import sys
from pathlib import Path
import struct,zlib,subprocess
out=Path(sys.argv[1])
def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b))
for w,h in [(1,1),(37,91),(128,128),(512,512),(535,512),(500,500)]:
 for color,nc in [(0,1),(2,3),(3,1),(4,2),(6,4)]:
  data=b''.join(b'\0'+bytes((x*29+y*13+c*41)%256 for x in range(w) for c in range(nc)) for y in range(h))
  png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,color,0,0,0))
  if color==3:png+=chunk(b'PLTE',bytes(v for x in range(256) for v in (x,255-x,x//2)))+chunk(b'tRNS',bytes(range(256)))
  if w==128 and color==2:
   for gamma in [45455,100000,55555]:
    variant=png+chunk(b'gAMA',struct.pack('>I',gamma))+chunk(b'IDAT',zlib.compress(data))+chunk(b'IEND',b'')
    (out/f'gamma-{gamma}.png').write_bytes(variant)
  png+=chunk(b'IDAT',zlib.compress(data))+chunk(b'IEND',b'')
  path=out/f'{w}x{h}-{color}.png';path.write_bytes(png)
  if w==37:subprocess.run(['magick',str(path),'-interlace','PNG',str(out/f'{w}x{h}-{color}-adam7.png')],check=True)

for color,nc in [(0,1),(2,3),(4,2),(6,4)]:
 data=b''.join(b'\0'+b''.join(struct.pack('>H',(x*601+y*307+c*1009)%65536) for x in range(37) for c in range(nc)) for y in range(91))
 for gamma in [None,45455,100000]:
  png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',37,91,16,color,0,0,0))
  if gamma:png+=chunk(b'gAMA',struct.pack('>I',gamma))
  (out/f'16bit-{color}-{gamma}.png').write_bytes(png+chunk(b'IDAT',zlib.compress(data))+chunk(b'IEND',b''))

PY
cc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  "$ROOT/psx3d/tests/covers/png.c" "$ROOT/psx3d/source/covers_png.c" "$ROOT/psx3d/source/covers_pack.c" $(pkg-config --cflags --libs libpng) -lm -Wl,--wrap=malloc -o "$TMP/check"
"$TMP/check" "$TMP/"*.png "$@"
cc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  "$ROOT/psx3d/tests/covers/pack.c" "$ROOT/psx3d/source/covers_pack.c" "$ROOT/psx3d/source/covers_png.c" \
  $(pkg-config --cflags --libs libpng) -lm -o "$TMP/pack"
"$TMP/pack" ""
