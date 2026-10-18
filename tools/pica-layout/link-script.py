#!/usr/bin/env python3
"""Add only the renderer section to the installed devkitARM 3DS layout."""
from pathlib import Path
import sys
sdk, frontend = map(Path, sys.argv[1:])
base = sdk / 'arm-none-eabi/lib'
out = frontend / 'build'
out.mkdir(exist_ok=True)
snippet = (frontend / 'pica-hot.ld').read_text()
body = snippet[snippet.index(' .text.pica_hot'):snippet.index('\n}\nINSERT')]
body = body.rstrip() + ' : code\n\n'
s = (base / '3dsx.ld').read_text()
anchor = '\t/* =========== RODATA section =========== */'
assert s.count(anchor) == 1
ld = out / 'pica-hot-3dsx.ld'
ld.write_text(s.replace(anchor, body + anchor))
specs = (base / '3dsx.specs').read_text().replace('3dsx.ld%s', str(ld))
(out / 'pica-hot.specs').write_text(specs)
