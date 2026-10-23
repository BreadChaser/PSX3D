#!/usr/bin/env python3
"""Exercise actual scene/theme bodies under ASan/UBSan, replacing SDK includes only."""
import os,pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='psx3d-scenes-') as tmp:
 tmp=pathlib.Path(tmp)
 for name in ('theme','scene'):
  source=(root/f'psx3d/source/{name}.c').read_text()
  for header in ('psx3d.h','scene.h','devtools.h'):
   source=source.replace(f'#include "{header}"','')
  (tmp/f'{name}-body.c').write_text(source)
 cmd=['cc','-std=gnu11','-g','-fsanitize=address,undefined','-Wno-pointer-to-int-cast','-I'+str(tmp),'-I'+str(root/'psx3d/tests/host'),'-I'+str(root/'psx3d/source'),str(root/'tests/themes/check.c'),'-lm','-o',str(tmp/'check')]
 subprocess.run(cmd,check=True);subprocess.run([str(tmp/'check')],check=True)
