#!/usr/bin/env python3
"""Write one new HLE benchmark state; never overwrites a slot. CLI holds console lock."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import sys
import time
DEV=Path(os.environ.get('PSX3D_DEVTOOLS','/home/tony/Development/PS1_3ds/devtools'))
sys.path.insert(0,str(DEV))
from psx3dctl import Client, ControlError
SIZE=4456448

def frame(reader):
    parts=reader.readline(100).split()
    if len(parts)!=2 or parts[0]!=b'JSON':raise ControlError('invalid import response frame')
    n=int(parts[1])
    if not 0<=n<=4096:raise ControlError('import response too large')
    payload=reader.read(n)
    if len(payload)!=n:raise ControlError('truncated import response')
    value=json.loads(payload)
    if value.get('ok') is not True:raise ControlError(value.get('error','import refused'))
    return value

def import_state(client,title,path,console,slot,rate=384*1024):
    path=Path(path)
    if any(c in title for c in '\r\n\x00'):raise ValueError('invalid title')
    if path.stat().st_size!=SIZE:raise ValueError('state must be exactly 4456448 bytes')
    data=path.read_bytes()
    if data[:9]!=b'STv4 PCSX' or struct.unpack_from('<II',data,32)!=(0x8b410006,1):
        raise ValueError('expected current native raw HLE state')
    digest=hashlib.sha256(data).hexdigest()
    if slot not in(5,6,7):raise ValueError('slot must be 5..7')
    command=f'state-import {title.replace(" ","_")} slot={slot} size={SIZE} sha256={digest}'
    line=(client.token+' '+command+'\n').encode()
    with socket.create_connection((client.host,client.port),timeout=client.timeout)as sock:
        sock.settimeout(client.timeout)
        sock.sendall(line)
        with sock.makefile('rb')as reader:
            ready=frame(reader)
            if ready.get('ready')is not True or ready.get('size')!=SIZE:raise ControlError('missing ready handshake')
            started=time.monotonic()
            for offset in range(0,SIZE,8192):
                sock.sendall(data[offset:offset+8192])
                due=min(offset+8192,SIZE)/rate-(time.monotonic()-started)
                if due>0:time.sleep(due)
            sock.shutdown(socket.SHUT_WR)
            result=frame(reader)
    if result.get('imported')is not True:raise ControlError('import not confirmed')
    return dict(result,title=title,slot=slot,sha256=digest)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('console',choices=('old','new'));parser.add_argument('title');parser.add_argument('state',type=Path);parser.add_argument('--slot',type=int,required=True,choices=(5,6,7))
    parser.add_argument('--expected-build',required=True,help='exact build identity supplied by coordinator')
    parser.add_argument('--out',type=Path,required=True,help='new evidence directory')
    args=parser.parse_args()
    root=DEV/'fastloop'
    if(root/(args.console.upper()+'_UNAVAILABLE')).exists():parser.exit(1,'STOP: console unavailable\n')
    with(root/('lock.'+args.console)).open('a')as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        if(root/(args.console.upper()+'_UNAVAILABLE')).exists():raise ControlError('console unavailable')
        client=Client(args.console,token=(root/'token').read_text().strip(),timeout=180)
        status=client.request('status')
        if status.get('mode')!='library' or status.get('bench',{}).get('active') or status.get('settings',{}).get('build')!=args.expected_build:
            raise ControlError('console handoff/build check failed; no file sent')
        args.out.mkdir(parents=True,exist_ok=False)
        (args.out/'before.json').write_text(json.dumps(status,indent=2))
        result=import_state(client,args.title,args.state,args.console,args.slot)
        (args.out/'import.json').write_text(json.dumps(result,indent=2))
        (args.out/'after.json').write_text(json.dumps(client.request('status'),indent=2))
        (args.out/'library.png').write_bytes(client.request('shot both'))
        print(json.dumps(result))
if __name__=='__main__':
    try:main()
    except (OSError,ValueError,ControlError)as e:raise SystemExit(f'STOP: {e}')
