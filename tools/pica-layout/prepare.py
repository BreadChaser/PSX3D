#!/usr/bin/env python3
"""Invalidate only objects whose flags change between renderer layout builds."""
from pathlib import Path
import sys
root = Path(__file__).resolve().parents[2]
stamp = root / 'psx3d/build/pica-layout.txt'
layout = sys.argv[1]
if not stamp.exists() or stamp.read_text() != layout:
    for name in ['core/plugins/gpu_pica/gpu_pica.o', 'core/plugins/gpulib/gpu.o',
                 'psx3d/build/devcontrol.o', 'psx3d/PSX3D.elf']:
        (root / name).unlink(missing_ok=True)
    stamp.parent.mkdir(exist_ok=True)
    stamp.write_text(layout)
