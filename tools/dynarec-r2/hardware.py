#!/usr/bin/env python3
"""One exclusive Old A/B/A; no socket is opened before marker/lock checks."""
from pathlib import Path
from datetime import datetime, timezone
import argparse, fcntl, hashlib, json, os, subprocess, sys, time, signal

DEV=Path('/home/tony/Development/PS1_3ds/devtools')
ROOT=DEV/'dynarec-r2'

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--label',required=True)
p.add_argument('--candidate',default='artifacts-2',help='artifact directory under devtools/dynarec-r2')
p.add_argument('--identity',default='dynarec-r2-2')
p.add_argument('--console',choices=('old','new'),default='old',help='new: correctness smoke only (candidate batch, no baseline brackets)')
p.add_argument('--wait-lock',type=int,default=0,help='Seconds to wait without contacting console')
a=p.parse_args()
MARKER=DEV/('fastloop/OLD_UNAVAILABLE' if a.console=='old' else 'fastloop/NEW_UNAVAILABLE')
TIMEOUT=240 if a.console=='old' else 600
def interrupted(signum, frame):
    raise InterruptedError('Host driver received signal '+str(signum))
signal.signal(signal.SIGTERM, interrupted)
for gate in ('gate600/compare-c15.json','gate3600/compare-c15.json','boot-hle/compare-c15.json','boot-bios/compare-c15.json'):
    assert json.loads((ROOT/gate).read_text())['passed'], gate+' must pass before hardware.'
# Baselines the handoff rule accepts: restore exactly what was installed.
BASELINES={'release-v040-final2':(DEV/'v040-final/artifacts/PSX3D.cia','d6889d978703f7b3fac51b1aa2cbb2fb4ed8b9f2f9eb8585b0c304057fbf2d06'),
           'release-v040-final':(DEV/'frameskip-smart/baseline-final/PSX3D.cia','cba906a2c4cf8568ae71d80c3824dd2dd3030388b1d15a9a0d2e40606582ccdf'),
           'release-v040-rc1-split':(DEV/'v040/artifacts/PSX3D.cia','7c7464fbd055aa0d0d431668bf40b6b8475321d0cb98ccd0491ed1d04009071a')}
if MARKER.exists():sys.exit(MARKER.name+' exists; no console contact.')
with (DEV/('fastloop/lock.'+a.console)).open('a') as lock:
    deadline=time.monotonic()+a.wait_lock
    while True:
        if MARKER.exists():sys.exit('OLD_UNAVAILABLE appeared while waiting; no console contact.')
        try:
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            break
        except BlockingIOError:
            if time.monotonic()>=deadline:sys.exit(a.console+' lock is busy; no console contact.')
            time.sleep(min(5,max(0,deadline-time.monotonic())))
    if MARKER.exists():sys.exit('OLD_UNAVAILABLE appeared; no console contact.')
    candidate=ROOT/a.candidate/'PSX3D.cia'
    sums=dict(l.split()[::-1] for l in (ROOT/a.candidate/'SHA256SUMS').read_text().splitlines())
    assert hashlib.sha256(candidate.read_bytes()).hexdigest()==sums['PSX3D.cia']
    sys.path.insert(0,str(DEV))
    from psx3dctl import Client
    from fastbench import serve_cia,wait_relaunch
    token=(DEV/'fastloop/token').read_text().strip()
    client=Client(a.console,token,timeout=TIMEOUT)
    try:
        probe=client.request('status')
    except OSError as e:
        # PSX3D's remote server is not up (another app, asleep, off network)
        print('not ready: '+a.console+' remote server unreachable ('+str(e)+'); nothing sent')
        sys.exit(3)
    out=ROOT/'hardware'/a.label
    out.mkdir(parents=True,exist_ok=False)
    uncertain=False
    owned=False
    def save(name,data):
        (out/(name+'.json')).write_text(json.dumps(data,indent=2)+'\n')
    def install(cia,name):
        global uncertain
        before=client.request('status')['session']
        uncertain=True
        with serve_cia(cia,port=8783 if a.console=='old' else 8784) as request:
            save(name,client.request('update '+request))
            wait_relaunch(client,before,TIMEOUT)
        uncertain=False
    def batch(name,filename):
        subprocess.run([sys.executable,str(DEV/'fastbench.py'),str(ROOT/filename),a.console,
            '--keep-viewers','--out',str(out/name),'--timeout',str(TIMEOUT)],
            env=dict(os.environ,PSX3D_TOKEN=token),check=True)
    try:
        initial=client.request('status');save('initial',initial)
        assert initial['mode']=='library' and not initial['bench']['active'],'Console is busy; stop.'
        base_id=initial['settings']['build']
        assert base_id in BASELINES,'Not an idle baseline (handoff rule); release the lock and retry later.'
        baseline_cia,base_sha=BASELINES[base_id]
        assert hashlib.sha256(baseline_cia.read_bytes()).hexdigest()==base_sha
        observer=DEV/'o3ds60/udp-status.json'
        if observer.exists():(out/'ntr-condition.json').write_bytes(observer.read_bytes())
        owned=True
        if a.console=='old':
            batch('A1','batches/baseline.txt')
        install(candidate,'candidate-update')
        status=client.request('status');save('candidate',status)
        assert status['settings']['build']==a.identity
        batch('B','batches/candidate.txt' if a.console=='old' else 'batches/new-smoke.txt')
        install(baseline_cia,'baseline-update')
        if a.console=='old':
            batch('A2','batches/baseline.txt')
        client.request('quit') # unload game; remote server/application stay alive
        status=client.request('status')
        assert status['mode']=='library' and not status['bench']['active']
        assert status['settings']['build']==base_id
        save('restored-status',status)
        (out/'restored-library.png').write_bytes(client.request('shot both'))
    except BaseException:
        if not owned:raise
        # Never retry an uncertain install or attempt recovery. A failed batch
        # with a healthy known candidate can still be restored once to the baseline.
        if not uncertain:
            try:
                st=client.request('status')
                if st['settings']['build']==a.identity:
                    client.request('quit')
                    deadline=time.monotonic()+30
                    while st['mode']!='library' or st['bench']['active']:
                        assert time.monotonic()<deadline, 'Candidate did not unload'
                        time.sleep(1)
                        st=client.request('status')
                    install(baseline_cia,'error-baseline-restore')
                client.request('quit')
                save('error-status',client.request('status'))
                (out/'error-library.png').write_bytes(client.request('shot both'))
            except BaseException:uncertain=True
        if uncertain:MARKER.write_text(datetime.now(timezone.utc).isoformat()+'\n'+a.console+' dynarec-r2 update did not return; no retries or recovery. See '+str(out)+'\n')
        raise
    print('Baseline '+base_id+' restored, remote idle in library. Review restored-library.png.')
