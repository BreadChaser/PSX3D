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

# The disc stand-in (discart.c): its JPEG decoder against ImageMagick's
# (libjpeg) on synthetic pictures in every layout it takes, refusal of the ones
# it does not, damaged files, and a disc put together from a synthetic photo
# (dark disc on white, white hole) and wordmark (black text on transparent).
cc -std=gnu11 -DDISCART_TEST -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  "$ROOT/psx3d/tests/covers/disc.c" "$ROOT/psx3d/source/discart.c" "$ROOT/psx3d/source/covers_png.c" \
  "$ROOT/psx3d/source/covers_pack.c" $(pkg-config --cflags --libs libpng) -lm -o "$TMP/disc"
magick -size 333x217 -seed 7 plasma:fractal "$TMP/src.ppm"
pairs=()
for v in "420:2x2" "444:1x1" "422:2x1" "440:1x2"; do
  n=${v%%:*}
  magick "$TMP/src.ppm" -sampling-factor "${v#*:}" -quality 92 "$TMP/t$n.jpg"
  pairs+=("$TMP/t$n.jpg" "$TMP/t$n.ppm")
done
magick "$TMP/src.ppm" -colorspace gray -quality 90 "$TMP/tgray.jpg"
pairs+=("$TMP/tgray.jpg" "$TMP/tgray.ppm")
magick "$TMP/src.ppm" -resize 1001x763\! -quality 85 "$TMP/tbig.jpg"   # sizes not a multiple of the block
pairs+=("$TMP/tbig.jpg" "$TMP/tbig.ppm")
for ((i = 0; i < ${#pairs[@]}; i += 2)); do
  # the reference: libjpeg through ImageMagick, accurate DCT, chroma repeated (no smoothing)
  magick -define jpeg:fancy-upsampling=off -define jpeg:dct-method=float "${pairs[i]}" -colorspace sRGB -type TrueColor "${pairs[i+1]}"
done
"$TMP/disc" decode "${pairs[@]}"
magick "$TMP/src.ppm" -interlace JPEG "$TMP/tprog.jpg"
printf 'not a jpeg' > "$TMP/tjunk.jpg"
"$TMP/disc" fail "$TMP/tprog.jpg" "$TMP/tjunk.jpg"
"$TMP/disc" broken "$TMP/t420.jpg"
magick -size 600x560 xc:white -fill '#15111c' -draw 'ellipse 300,280 280,262 0,360' \
  -fill '#2a2238' -draw 'ellipse 300,270 70,64 0,360' -fill white -draw 'ellipse 300,270 26,24 0,360' \
  -quality 90 "$TMP/photo.jpg"
magick -size 400x80 xc:none -fill black -draw 'rectangle 10,10 390,70' "$TMP/word.png"
"$TMP/disc" make "$TMP/photo.jpg" "$TMP/word.png" "$TMP/disc.pam"
