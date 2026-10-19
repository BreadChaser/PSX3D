#!/usr/bin/env python3
"""List or archive developer clips using authenticated, read-only commands."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
sys.path.insert(0, os.environ.get('PSX3D_HOST_TOOLS', '/home/tony/Development/PS1_3ds/devtools'))
import psx3dctl
psx3dctl.MAX_RESPONSE = 8 * 1024 * 1024

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('console')
    p.add_argument('title', help='recording folder, e.g. Crash_Bandicoot')
    p.add_argument('--id', type=int)
    p.add_argument('--out', type=Path)
    args=p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,100}',args.title) or (args.id is not None and not 0<=args.id<=999999):
        p.error('invalid folder or id')
    client=psx3dctl.Client(args.console)
    listing=client.request('recordings '+args.title)
    if args.id is None:
        print(json.dumps(listing,indent=2)); return
    if args.id not in listing['recordings']: p.error('recording not listed')
    if args.out is None: p.error('--out is required when fetching')
    args.out.mkdir(parents=True,exist_ok=True)
    manifest={}
    for ext in ('state','inp'):
        target=args.out/f'{args.id}.{ext}'
        if target.exists(): p.error('refusing to replace '+str(target))
        data=client.request(f'get recordings/{args.title}/{args.id}.{ext}')
        if not isinstance(data,bytes): raise psx3dctl.ControlError('expected recording bytes')
        with target.open('xb') as f: f.write(data)
        manifest[target.name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    (args.out/f'{args.id}.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))
if __name__=='__main__': main()
