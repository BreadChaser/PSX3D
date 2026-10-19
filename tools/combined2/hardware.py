#!/usr/bin/env python3
"""Old-only combined session, authorized by the updated console policy.

--dry-run validates local files without credentials, locks or console traffic.
Execution requires an explicit flag and a recorded NTR condition.
No install retries, automatic recovery, or finally-block console commands.
"""
import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import math
from pathlib import Path
import re
import signal
import sys
import time
from types import SimpleNamespace

DEV = Path('/home/tony/Development/PS1_3ds/devtools')
PACKAGE = DEV / 'combined2'
MARKER = DEV / 'fastloop/OLD_UNAVAILABLE'
RC2_SHA = '5fa3d650a11a8f2b0c9d247e3d0c1fe1c51af7818a4fef07c2befcec57b07660'
RC2_BUILD = 'release-v031-rc2-split'
CANDIDATE_BUILD = 'combined2-1-split'
BASELINE = dict(record=False, hack_spunearest=False)
REMOVE_BUILD = 'remove-sw-1-split'


def clearance():
    if MARKER.exists():
        raise RuntimeError('STOP: OLD_UNAVAILABLE exists; no console contact permitted')


def local_checks():
    manifest = json.loads((PACKAGE / 'artifacts/manifest.json').read_text())
    assert manifest['build'] == CANDIDATE_BUILD
    assert manifest['title_counter'] == 3076 and manifest['layout'] == 'split'
    for name, info in manifest['files'].items():
        data = (PACKAGE / 'artifacts' / name).read_bytes()
        assert len(data) == info['bytes'] and hashlib.sha256(data).hexdigest() == info['sha256']
    assert hashlib.sha256((DEV / 'release_v031/final/PSX3D.cia').read_bytes()).hexdigest() == RC2_SHA
    assert hashlib.sha256((DEV/'remove-sw/artifacts/PSX3D.cia').read_bytes()).hexdigest()=='e88a9a61fd45c80e4eab6ff303214a394c5d57a5a47ce151e3303280ff0284c3'
    sys.path.insert(0, str(DEV))
    import fastbench
    count = 0
    for name in ['rc2-four.txt','remove-four.txt','candidate-defaults.txt','auto.txt','sotn.txt']+[f'spu-{i}.txt' for i in range(1,4)]:
        rows=fastbench.bench_text(PACKAGE/'batches'/name,'old')
        for line in rows:
            words=dict(w.split('=',1) for w in line.split() if '=' in w)
            assert words['state']=='2' and words['renderer']=='gpu'
            assert words['warmup']=='10' and words['seconds']=='15' and words['pmu']=='0,2'
        count+=len(rows)
    # RC2 is used twice, A1 and A2.
    return manifest, count + 4


def check_status(status, build, library=False):
    assert status['console'] == 'old' and status['settings']['build'] == build
    assert not status.get('error'), status.get('error')
    if library:
        assert status['mode'] == 'library' and not status['bench']['active']


def check_settings(status, line):
    if status['mode'] != 'playing':
        return
    words = dict(w.split('=', 1) for w in line.split() if '=' in w)
    assert status['renderer'] == 'gpu'
    assert status['state'] == int(words.get('state', '-1'))
    settings = status['settings']
    for key in BASELINE:
        value = words[key]
        expected = value == 'on'
        assert settings[key] == expected, (key, settings[key], expected)
    assert settings['pmu0'] == 0 and settings['pmu1'] == 2
    assert settings['clock'] == '57'
    expected_fs = 'disabled' if words['frameskip'] == 'off' else 'auto'
    assert settings['frameskip'] == expected_fs


def main():
    def stop_requested(signum, frame):
        raise RuntimeError(f'Host signal {signum}; stop without console recovery')

    # Default SIGTERM bypasses Python exception cleanup. Ensure an interrupted
    # update leaves the unavailable marker before the flock is released.
    signal.signal(signal.SIGTERM, stop_requested)
    signal.signal(signal.SIGHUP, stop_requested)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--label')
    parser.add_argument('--ntr-condition')
    args = parser.parse_args()
    manifest, count = local_checks()
    if args.dry_run:
        print(f'Local package PASS: {count} ordinary rows, {count * 25 / 60:.2f} timed minutes; no console contact')
        return
    if not args.execute or not args.label or not args.ntr_condition:
        parser.error('--execute, --label and --ntr-condition are required')
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,80}', args.label):
        parser.error('label must be a safe, unique directory name')
    clearance()
    with (DEV / 'fastloop/lock.old').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        clearance()
        out = PACKAGE / 'hardware' / args.label
        out.mkdir(parents=True, exist_ok=False)
        from psx3dctl import Client
        import psx3dctl
        import fastbench
        psx3dctl.MAX_RESPONSE = 8 * 1024 * 1024
        secret = (DEV / 'fastloop/token').read_text().strip()
        current_batch = None
        verified_rows = set()
        baseline_settings = {}
        batch_in_flight = False
        expected_build = RC2_BUILD
        started = time.monotonic()

        def save(name, value):
            (out / (name + '.json')).write_text(json.dumps(value, indent=2) + '\n')

        class GuardedClient(Client):
            def __init__(self, console='old', *a, **kw):
                assert console == 'old', 'New console contact is forbidden'
                super().__init__(console, *a, **kw)

            def request(self, command):
                nonlocal batch_in_flight
                clearance()
                if command == 'quit' and batch_in_flight:
                    raise RuntimeError('Uncertain batch completion: refusing automatic quit/recovery')
                value = super().request(command)
                if command.startswith('bench '):
                    batch_in_flight = True
                if command == 'status' and current_batch is not None:
                    folder, rows = current_batch
                    with (folder / 'status-history.jsonl').open('a') as log:
                        log.write(json.dumps(value) + '\n')
                    check_status(value, expected_build)
                    if value['bench']['active'] and value['bench']['samples'] > 0:
                        clearance()
                        with (folder / 'stats-history.jsonl').open('a') as log:
                            log.write(json.dumps(super().request('stats')) + '\n')
                    run = value['bench']['run']
                    if not value['bench']['active']:
                        batch_in_flight = False
                    if value['bench']['active'] and value['bench']['samples'] > 0 and 0 <= run < len(rows):
                        if expected_build == CANDIDATE_BUILD and 'replay=' not in rows[run]:
                            check_settings(value, rows[run])
                        words=dict(w.split('=',1) for w in rows[run].split() if '=' in w)
                        if expected_build == RC2_BUILD:
                            current={k:v for k,v in value['settings'].items() if k not in ('free_heap','free_linear')}
                            if folder.name=='A1': baseline_settings[words['game']]=current
                            elif folder.name=='A2': assert current==baseline_settings[words['game']], 'RC2 settings drift'
                        verified_rows.add(run)
                return value

        client = GuardedClient('old', secret, timeout=240)
        fastbench.Client = GuardedClient
        save('session-plan', dict(console_policy='common.md 2026-10-19 02:50 UTC', ntr_condition=args.ntr_condition,
                                  candidate=manifest, started_utc=datetime.now(timezone.utc).isoformat()))

        def library(name):
            # Remote quit only unloads the game; exit (app shutdown) is never sent.
            client.request('quit')
            deadline = time.monotonic() + 60
            while True:
                status = client.request('status')
                if status['mode'] == 'library':
                    break
                if time.monotonic() > deadline:
                    raise RuntimeError('quit did not reach library')
                time.sleep(1)
            check_status(status, expected_build, library=True)
            save(name, status)
            (out / (name + '.png')).write_bytes(client.request('shot both'))
            return status

        def batch(name, file):
            nonlocal current_batch
            clearance()
            destination = out / name
            destination.mkdir()
            rows = fastbench.bench_text(file, 'old')
            (destination / 'batch.txt').write_text('\n'.join(rows) + '\n')
            current_batch = destination, rows
            verified_rows.clear()
            observer = DEV / 'o3ds60/udp-status.json'
            if observer.exists():
                (destination / 'ntr-before.json').write_bytes(observer.read_bytes())
            try:
                _, results = fastbench.run_console('old', SimpleNamespace(
                    token=secret, port=5555, timeout=240, bench=file,
                    batch_id=name.lower(), out=destination), None)
                save(name + '-summary', results)
                if not any('replay=' in line for line in rows):
                    assert verified_rows == set(range(len(rows))), 'Missing live switch verification for a row'
            finally:
                # Host bookkeeping only; no automatic console cleanup.
                current_batch = None
            library(name + '-library')
            if observer.exists():
                (destination / 'ntr-after.json').write_bytes(observer.read_bytes())

        def install(file, build, name):
            nonlocal expected_build
            clearance()
            status = library(name + '-before')
            previous_session = status['session']
            print(f'Install: {file.name} -> {build}. Do not HOME/power off/remove SD.', flush=True)
            with fastbench.serve_cia(file, port=8778) as request:
                save(name + '-reply', client.request('update ' + request))
                fastbench.wait_relaunch(client, status['session'], 240)
            expected_build = build
            status = client.request('status')
            assert status['session'] != previous_session
            check_status(status, build, library=True)
            save(name + '-after', status)
            (out / (name + '-library.png')).write_bytes(client.request('shot both'))
            print('Verified remote library; review ' + str(out / (name + '-library.png')), flush=True)

        def archived_replay():
            archive=DEV/'combined1/hardware/old-session-2/clip'
            saved=out/'clip';saved.mkdir()
            for ext in ('state','inp'):
                data=client.request('get recordings/Crash_Bandicoot/1.'+ext)
                assert data==(archive/('1.'+ext)).read_bytes(), 'Archived clip on SD differs'
                (saved/('1.'+ext)).write_bytes(data)
            file=out/'replay.txt'
            file.write_text('\n'.join(['game=Crash_Bandicoot replay=1 renderer=gpu clock=57 frameskip=off pmu=0,2 pmuclass=on record=off hack_spunearest=off warmup=0 seconds=900 shot=1']*2)+'\n')
            batch('archived-replays',file)
            result=json.loads((out/'archived-replays-summary.json').read_text())
            for row in result:
                assert row['status']=='ok' and int(row['replay_frames'])==900 and int(row['replay_checks'])==15 and int(row['replay_divergent'])==0
            save('archived-replay-acceptance',dict(frames=900,checks=15,replays=2,divergent=0))

        def pictures():
            game='Castlevania_-_Symphony_of_the_Night'
            # Menu state 8 is protected/load-only. Title boot uses no gameplay input.
            for name,extra in [('menu','state=8'),('title','')]:
                save('sotn-'+name+'-load',client.request('load '+game+' renderer=gpu clock=57 frameskip=off record=off hack_spunearest=off '+extra))
                for i in range(1,41 if name=='title' else 7):
                    time.sleep(3)
                    save(f'sotn-{name}-{i}',client.request('status'))
                    (out/f'sotn-{name}-{i}.png').write_bytes(client.request('shot top'))
                library('sotn-'+name+'-library')

        try:
            status = client.request('status')
            save('initial-status', status)
            if status.get('settings',{}).get('build')!=RC2_BUILD or status['bench']['active'] or status['mode']!='library':
                save('preflight-busy',dict(reason='Handoff requires RC2 idle library; no mutation issued'))
                print('Preflight busy: no mutation issued; release lock and retry later',flush=True)
                return
            check_status(status, RC2_BUILD, library=True)
            (out / 'initial-library.png').write_bytes(client.request('shot both'))
            batch('A1', PACKAGE / 'batches/rc2-four.txt')
            install(DEV/'remove-sw/artifacts/PSX3D.cia',REMOVE_BUILD,'remove-sw')
            batch('remove-sw-four',PACKAGE/'batches/remove-four.txt')
            install(PACKAGE/'artifacts/PSX3D.cia',CANDIDATE_BUILD,'candidate')
            batch('defaults',PACKAGE/'batches/candidate-defaults.txt')
            batch('auto',PACKAGE/'batches/auto.txt')
            batch('sotn',PACKAGE/'batches/sotn.txt')
            for i in range(1,4): batch(f'spu-{i}',PACKAGE/f'batches/spu-{i}.txt')
            archived_replay()
            pictures()
            install(DEV / 'release_v031/final/PSX3D.cia', RC2_BUILD, 'restore')
            batch('A2', PACKAGE / 'batches/rc2-four.txt')
            library('restored-final')
            print('WAITING FOR LOCAL VISUAL REVIEW: '+str(out/'restored-final.png'),flush=True)
            deadline=time.monotonic()+300
            while not (out/'FINAL_REVIEWED').exists():
                if time.monotonic()>deadline: raise RuntimeError('Final screenshot local review timeout; lock held until stop')
                time.sleep(1)
            save('complete', dict(rc2_sha256=RC2_SHA, final_build=RC2_BUILD,
                                  elapsed_seconds=time.monotonic() - started))
        except BaseException:
            # Do not contact the console here, including quit/status/restore.
            MARKER.write_text(datetime.now(timezone.utc).isoformat() + '\n'
                'combined2 session stopped; installed state unverified.\n'
                'No retries/recovery/contact until the user checks the console.\n'
                'Evidence: ' + str(out) + '\n')
            raise
        print('Exact RC2 restored; final screenshot reviewed. Lock releases on driver exit.')


if __name__ == '__main__':
    main()
