#!/usr/bin/env python3
"""Exclusive console acceptance session. JSON actions arrive on stdin.
No automatic mutations on exceptions. Only an approved release build in the idle library starts a session.
"""
import argparse, csv, fcntl, hashlib, io, json, sys, time
from pathlib import Path
from types import SimpleNamespace
D=Path('/home/tony/Development/PS1_3ds/devtools'); E=D/'v040-final'
sys.path.insert(0,str(D))
import psx3dctl,fastbench
from psx3dctl import Client,ControlError
psx3dctl.MAX_RESPONSE=8*1024*1024
BASELINE='release-v040-rc1-split'; FINAL='release-v040-final'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('console',choices=['old','new']);p.add_argument('--label',required=True);p.add_argument('--wait-lock',action='store_true');a=p.parse_args()
marker=D/'fastloop'/f'{a.console.upper()}_UNAVAILABLE'
def clearance():
 if marker.exists():raise RuntimeError('UNAVAILABLE marker: no contact')
def save(label,value):
 path=out/(label+('.png' if isinstance(value,bytes) else '.json'))
 if isinstance(value,bytes):path.write_bytes(value)
 else:path.write_text(json.dumps(value,indent=2)+'\n')
 return path
with (D/'fastloop'/('lock.'+a.console)).open('a') as lock:
 try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 except BlockingIOError:
  if not a.wait_lock:print('BUSY lock',flush=True);sys.exit(2)
  clearance();print('WAITING lock (no console contact)',flush=True);fcntl.flock(lock,fcntl.LOCK_EX)
 clearance();out=E/'hardware'/a.console/a.label;out.mkdir(parents=True,exist_ok=False)
 secret=(D/'fastloop/token').read_text().strip()
 class GuardedClient(Client):
  def request(self,cmd):
   clearance()
   value=super().request(cmd)
   with (out/'requests.jsonl').open('a') as f:f.write(json.dumps(dict(time=time.time(),command=cmd,result='PNG' if isinstance(value,bytes) else value))+'\n')
   return value
 c=GuardedClient(a.console,secret,timeout=600);fastbench.Client=GuardedClient
 s=c.request('status');save('initial-status',s)
 if s['settings']['build'] not in (BASELINE,FINAL) or s['mode']!='library' or s['bench']['active']:
  print('BUSY handoff '+json.dumps(s),flush=True);sys.exit(2)
 save('initial-library',c.request('shot both'));print('READY '+str(out),flush=True)
 expected=s['settings']['build']
 def library(label):
  c.request('ui library');deadline=time.monotonic()+60
  while True:
   s=c.request('status')
   if s['mode']=='library' and not s['bench']['active']:break
   if time.monotonic()>deadline:raise RuntimeError('library timeout')
   time.sleep(.5)
  assert s['settings']['build']==expected,s
  save(label,s);save(label+'-screens',c.request('shot both'));return s
 for line in sys.stdin:
  try:
   x=json.loads(line);op=x['op'];label=x.get('label',op)
   if op=='install':
    library(label+'-before');file=E/'artifacts/PSX3D.cia' if x['build']=='final' else D/'v040/artifacts/PSX3D.cia'
    build=FINAL if x['build']=='final' else BASELINE
    before=c.request('status');uncertain=True
    try:
     with fastbench.serve_cia(file,port=8785 if a.console=='old' else 8786) as request:
      save(label+'-reply',c.request('update '+request))
      fastbench.wait_relaunch(c,before['session'],600)
     s=c.request('status');assert s['session']!=before['session'] and s['settings']['build']==build and s['mode']=='library'
     expected=build;uncertain=False;save(label+'-status',s);save(label+'-library',c.request('shot both'))
    except BaseException:
     if uncertain:marker.write_text('v040 install uncertain; no retry. Evidence: '+str(out)+'\n')
     raise
    save(label+'-installed',dict(build=build,sha256=hashlib.sha256(file.read_bytes()).hexdigest(),session=s['session']))
   elif op=='batch':
    dest=out/label;dest.mkdir();file=E/'batches'/x['file'];rows=fastbench.bench_text(file,a.console);(dest/'batch.txt').write_text('\n'.join(rows)+'\n')
    observer=D/'o3ds60/udp-status.json'
    if observer.exists():(dest/'ntr-before.json').write_bytes(observer.read_bytes())
    _,results=fastbench.run_console(a.console,SimpleNamespace(token=secret,port=5555,timeout=600,bench=file,batch_id=label,out=dest),None)
    save(label+'-summary',results)
    if observer.exists():(dest/'ntr-after.json').write_bytes(observer.read_bytes())
    library(label+'-library')
   elif op=='request':
    cmd=x['command'];assert cmd!='exit' and not cmd.startswith(('update ','bench '))
    assert not cmd.startswith('savestate ') or cmd=='savestate 9'
    save(label,c.request(cmd))
   elif op=='shot':save(label,c.request('shot both'))
   elif op=='library':library(label)
   elif op=='release':
    s=library('final-state');assert s['settings']['build']==x['build'];save('complete',dict(build=expected,session=s['session'],reviewed=x.get('reviewed',False)));print('RELEASED '+expected,flush=True);break
   else:raise ValueError('unknown action')
   print('DONE '+label,flush=True)
  except BaseException as error:
   save('STOP-'+str(int(time.time())),dict(error=str(error),action=x));print('STOP '+repr(error),flush=True)
   if marker.exists():sys.exit(1)
