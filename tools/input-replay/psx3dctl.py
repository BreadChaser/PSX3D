#!/usr/bin/env python3
"""Use the existing developer client with the bounded 8 MiB recording transfer limit."""
import os
from pathlib import Path
import sys
sys.path.insert(0, os.environ.get('PSX3D_HOST_TOOLS', '/home/tony/Development/PS1_3ds/devtools'))
import psx3dctl
psx3dctl.MAX_RESPONSE = 8 * 1024 * 1024
if __name__ == '__main__':
    psx3dctl.main()
