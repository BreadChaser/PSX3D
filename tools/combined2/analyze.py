#!/usr/bin/env python3
"""Analyze a completed combined2 session from local evidence only."""
import argparse
import csv
import json
from pathlib import Path


def settings(row):
    return dict(word.split('=', 1) for word in row['request'].split() if '=' in word)


def delta(value, reference):
    return 100 * (float(value) / float(reference) - 1)


def analyze(root):
    batches = {path.name.removesuffix('-summary.json'): json.loads(path.read_text())
               for path in root.glob('*-summary.json')}
    assert all(row['status'] == 'ok' for rows in batches.values() for row in rows)
    assert sum(map(len, batches.values())) == 32, 'Expected 30 ordinary rows and two replays'
    report = dict(defaults=[], nearest=[], auto=[], sotn=[], passive_observer=[])
    for a1, removed, candidate, a2 in zip(*(batches[name] for name in
            ['A1', 'remove-sw-four', 'defaults', 'A2']), strict=True):
        game = settings(a1)['game']
        assert all(settings(row)['game'] == game for row in [removed, candidate, a2])
        mean = (float(a1['fps']) + float(a2['fps'])) / 2
        gain = delta(candidate['fps'], mean)
        drift = delta(a2['fps'], a1['fps'])
        row = dict(game=game, a1_fps=float(a1['fps']), a2_fps=float(a2['fps']),
                   rc2_mean=mean, drift_pct=drift, remove_sw_fps=float(removed['fps']),
                   remove_sw_delta_pct=delta(removed['fps'], mean),
                   candidate_fps=float(candidate['fps']), delta_pct=gain,
                   incremental_pct=delta(candidate['fps'], removed['fps']),
                   verdict='PASS' if gain >= -1 else 'FAIL', drift_flag=abs(drift) > 2)
        for name, sample in [('rc2_a1', a1), ('remove_sw', removed),
                             ('candidate', candidate), ('rc2_a2', a2)]:
            row[name + '_mcycles'] = float(sample['mcycles'])
            renderer = sample.get('renderer_mcycles', 'NA')
            row[name + '_nonrenderer_mcycles'] = (None if renderer == 'NA' else
                float(sample['mcycles']) - float(renderer))
        report['defaults'].append(row)
    report['performance_gate'] = all(row['verdict'] == 'PASS' for row in report['defaults'])
    for index in range(1, 4):
        before, on, after = batches[f'spu-{index}']
        game = settings(on)['game']
        assert settings(before)['game'] == settings(after)['game'] == game
        assert [settings(row)['hack_spunearest'] for row in [before, on, after]] == ['off', 'on', 'off']
        mean = (float(before['fps']) + float(after['fps'])) / 2
        report['nearest'].append(dict(game=game, before=float(before['fps']),
            on=float(on['fps']), after=float(after['fps']), mean=mean,
            gain_pct=delta(on['fps'], mean), drift_pct=delta(after['fps'], before['fps']),
            cycles_delta_pct=delta(on['mcycles'],
                (float(before['mcycles']) + float(after['mcycles'])) / 2),
            spu_ms=[float(row['spu_ms']) for row in [before, on, after]],
            spu_wait_ms=[float(row['spu_wait_ms']) for row in [before, on, after]],
            underruns=[float(row['audio_underruns']) for row in [before, on, after]]))
    for phase in ['auto', 'sotn']:
        report[phase] = [dict(game=settings(row)['game'], frameskip=settings(row)['frameskip'],
            fps=float(row['fps']), speed_pct=float(row['speed_pct']),
            black_samples=int(row['black_samples']),
            audio_underruns=float(row['audio_underruns'])) for row in batches[phase]]
    report['replay'] = json.loads((root / 'archived-replay-acceptance.json').read_text())
    report['restoration'] = json.loads((root / 'complete.json').read_text())
    report['restoration']['session'] = json.loads((root / 'restored-final.json').read_text())['session']
    report['ordinary_rows'] = sum(len(rows) for phase, rows in batches.items() if phase != 'archived-replays')
    report['black_samples'] = sum(int(row['black_samples']) for rows in batches.values() for row in rows)
    report['samples'] = sum(int(row['samples']) for rows in batches.values() for row in rows)
    for folder in sorted(root.iterdir()):
        before, after = folder / 'ntr-before.json', folder / 'ntr-after.json'
        if before.exists() and after.exists():
            b, a = json.loads(before.read_text()), json.loads(after.read_text())
            report['passive_observer'].append(dict(batch=folder.name, old_packets=
                a['packets'].get('192.168.1.147', 0) - b['packets'].get('192.168.1.147', 0),
                before_silence=b['silence_seconds']['192.168.1.147'],
                after_silence=a['silence_seconds']['192.168.1.147']))
    return report, batches


def markdown(report):
    text = ['# Combined2 archived Old hardware measurements', '']
    tables = [
        ('Frameskip off: arithmetic RC2 bracket mean', report['defaults'],
         ['game', 'a1_fps', 'a2_fps', 'drift_pct', 'rc2_mean', 'remove_sw_fps',
          'remove_sw_delta_pct', 'candidate_fps', 'delta_pct', 'incremental_pct', 'verdict']),
        ('Nearest SPU: on versus adjacent off mean', report['nearest'],
         ['game', 'before', 'on', 'after', 'gain_pct', 'drift_pct', 'cycles_delta_pct']),
        ('Frameskip auto (emulation FPS)', report['auto'], ['game', 'fps', 'speed_pct']),
        ('SotN GPU (emulation FPS)', report['sotn'], ['game', 'frameskip', 'fps', 'speed_pct'])]
    for title, rows, columns in tables:
        text += ['## ' + title, '', '| ' + ' | '.join(columns) + ' |',
                 '| ' + ' | '.join('---' for _ in columns) + ' |']
        for row in rows:
            text += ['| ' + ' | '.join(f'{row[key]:.3f}' if isinstance(row[key], float)
                                      else str(row[key]) for key in columns) + ' |']
        text += ['']
    text += ['FPS gate: ' + ('PASS' if report['performance_gate'] else 'FAIL'), '',
             'Recorder: ' + json.dumps(report['replay']), '',
             'Restoration: ' + json.dumps(report['restoration']), '']
    return '\n'.join(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    root = parser.parse_args().session
    report, batches = analyze(root)
    (root / 'analysis.json').write_text(json.dumps(report, indent=2) + '\n')
    (root / 'analysis.md').write_text(markdown(report))
    rows = [dict(phase=phase, **row) for phase, results in batches.items() for row in results]
    columns = list(dict.fromkeys(key for row in rows for key in row))
    with (root / 'all-results.tsv').open('w') as output:
        writer = csv.DictWriter(output, fieldnames=columns, delimiter='\t')
        writer.writeheader()
        writer.writerows(rows)
    print(markdown(report))


if __name__ == '__main__':
    main()
