#!/usr/bin/env python3
"""One exclusive Old A/B/A; no socket is opened before marker/lock checks."""
from pathlib import Path
from datetime import datetime, timezone
import argparse, fcntl, hashlib, json, os, subprocess, sys, time, signal

DEV=Path('/home/tony/Development/PS1_3ds/devtools')
ROOT=DEV/'dynarec-rewrite'
MARKER=DEV/'fastloop/OLD_UNAVAILABLE'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--label',required=True)
p.add_argument('--wait-lock',type=int,default=0,help='Seconds to wait without contacting console')
a=p.parse_args()
def interrupted(signum, frame):
    raise InterruptedError('Host driver received signal '+str(signum))
signal.signal(signal.SIGTERM, interrupted)
assert json.loads((ROOT/'compare-scratch-long.json').read_text())['passed'], 'Long-run gate must pass before hardware.'
if MARKER.exists():sys.exit('OLD_UNAVAILABLE exists; no console contact.')
with (DEV/'fastloop/lock.old').open('a') as lock:
    deadline=time.monotonic()+a.wait_lock
    while True:
        if MARKER.exists():sys.exit('OLD_UNAVAILABLE appeared while waiting; no console contact.')
        try:
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            break
        except BlockingIOError:
            if time.monotonic()>=deadline:sys.exit('Old lock is busy; no console contact.')
            time.sleep(min(5,max(0,deadline-time.monotonic())))
    if MARKER.exists():sys.exit('OLD_UNAVAILABLE appeared; no console contact.')
    candidate=ROOT/'artifacts-scratch-1/PSX3D.cia'
    rc2=DEV/'release_v031/final/PSX3D.cia'
    assert hashlib.sha256(rc2.read_bytes()).hexdigest()=='5fa3d650a11a8f2b0c9d247e3d0c1fe1c51af7818a4fef07c2befcec57b07660'
    assert hashlib.sha256(candidate.read_bytes()).hexdigest()=='96f653289714aa572119066adbc4f4a2b41b13198ced7f2036f2724f149e2065'
    out=ROOT/'hardware'/a.label
    out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(DEV))
    from psx3dctl import Client
    from fastbench import serve_cia,wait_relaunch
    token=(DEV/'fastloop/token').read_text().strip()
    client=Client('old',token,timeout=240)
    uncertain=False
    owned=False
    def save(name,data):
        (out/(name+'.json')).write_text(json.dumps(data,indent=2)+'\n')
    def install(cia,name):
        global uncertain
        before=client.request('status')['session']
        uncertain=True
        with serve_cia(cia,port=8783) as request:
            save(name,client.request('update '+request))
            wait_relaunch(client,before,240)
        uncertain=False
    def batch(name,filename):
        subprocess.run([sys.executable,str(DEV/'fastbench.py'),str(ROOT/filename),'old',
            '--keep-viewers','--out',str(out/name),'--timeout','240'],
            env=dict(os.environ,PSX3D_TOKEN=token),check=True)
    try:
        initial=client.request('status');save('initial',initial)
        assert initial['mode']=='library' and not initial['bench']['active'],'Console is busy; stop.'
        assert initial['settings']['build']=='release-v031-rc2-split','Exact RC2 must be restored before A1.'
        observer=DEV/'o3ds60/udp-status.json'
        if observer.exists():(out/'ntr-condition.json').write_bytes(observer.read_bytes())
        owned=True
        batch('A1','rc2-four.txt')
        install(candidate,'candidate-update')
        status=client.request('status');save('candidate',status)
        assert status['settings']['build']=='dynarec-rewrite-1-split'
        batch('B','scratch-four.txt')
        install(rc2,'rc2-update')
        batch('A2','rc2-four.txt')
        client.request('quit') # unload game; remote server/application stay alive
        status=client.request('status')
        assert status['mode']=='library' and not status['bench']['active']
        assert status['settings']['build']=='release-v031-rc2-split'
        save('restored-status',status)
        (out/'restored-library.png').write_bytes(client.request('shot both'))
    except BaseException:
        if not owned:raise
        # Never retry an uncertain install or attempt recovery. A failed batch
        # with a healthy known candidate can still be restored once to RC2.
        if not uncertain:
            try:
                st=client.request('status')
                if st['settings']['build']=='dynarec-rewrite-1-split':
                    client.request('quit')
                    deadline=time.monotonic()+30
                    while st['mode']!='library' or st['bench']['active']:
                        assert time.monotonic()<deadline, 'Candidate did not unload'
                        time.sleep(1)
                        st=client.request('status')
                    install(rc2,'error-rc2-restore')
                client.request('quit')
                save('error-status',client.request('status'))
                (out/'error-library.png').write_bytes(client.request('shot both'))
            except BaseException:uncertain=True
        if uncertain:MARKER.write_text(datetime.now(timezone.utc).isoformat()+'\nDynarec-rewrite update did not return; no retries or recovery. See '+str(out)+'\n')
        raise
    print('Exact RC2 restored, remote idle in library. Review restored-library.png.')
