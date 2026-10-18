#!/usr/bin/env python3
"""Refresh only this experiment's core files; preserve new-file patch entries."""
import subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[2]
p=root/'patches/0002-psx3d-widescreen-and-stereo-depth.patch'
files={'libpcsxcore/new_dynarec/new_dynarec.c','libpcsxcore/new_dynarec/new_dynarec.h'}
chunks=p.read_text().split('diff --git ');out=[chunks[0]]
for chunk in chunks[1:]:
 f=chunk.splitlines()[0].split()[0][2:]
 if f in files:out.append(subprocess.check_output(['git','-C',str(root/'core'),'-c','diff.mnemonicPrefix=false','-c','diff.noprefix=false','diff','--',f],text=True))
 else:out.append('diff --git '+chunk)
p.write_text(''.join(out))
