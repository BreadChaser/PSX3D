#!/usr/bin/env python3
"""A/B/A table from one hardware session: baseline A1/A2 brackets and the
candidate's same-binary configurations. Non-renderer Mcycles/vblank = run
mcycles minus the scoped renderer_mcycles (hardware PMU, not the model)."""
import argparse, csv, json, statistics
from pathlib import Path
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('directory', type=Path)
a = p.parse_args()

def rows(label):
    out = []
    for r in csv.DictReader((a.directory/label/'old/results.tsv').open(), delimiter='\t'):
        assert r['status'] == 'ok' and int(r['samples']) >= 14, r
        req = dict(w.split('=', 1) for w in r['request'].split() if '=' in w)
        r['game'] = req['game']
        on = [k[6:] for k, v in req.items() if k.startswith('dynr2_') and v == 'on']
        r['config'] = 'baseline' if not any(k.startswith('dynr2_') for k in req) else ('+'.join(on) or 'off')
        r['nonrenderer'] = float(r['mcycles']) - float(r['renderer_mcycles'])
        out.append(r)
    return out

base = rows('A1') + rows('A2')
trial = rows('B')
games = list(dict.fromkeys(r['game'] for r in base))
result = []
lines = ['Old 3DS, frameskip off, slot 2. FPS = emulated vblanks/s; non-renderer = mcycles - renderer_mcycles (M/vblank).', '',
         '| Game | Config | runs | FPS | vs baseline | vs cand. off | non-renderer M | vs cand. off | I-miss k (ev0) |',
         '|---|---|---:|---:|---:|---:|---:|---:|---:|']
for g in games:
    groups = {'baseline': [r for r in base if r['game'] == g]}
    for r in trial:
        if r['game'] == g:
            groups.setdefault(r['config'], []).append(r)
    mean = lambda rs, k: statistics.mean(float(r[k]) for r in rs)
    bfps = mean(groups['baseline'], 'fps')
    off = groups.get('off')
    for c, rs in groups.items():
        fps, nr, ev0 = mean(rs, 'fps'), mean(rs, 'nonrenderer'), mean(rs, 'ev0_k')
        d = dict(game=g, config=c, runs=len(rs), fps=fps, nonrenderer=nr, ev0_k=ev0,
                 fps_vs_baseline_pct=100*(fps/bfps-1),
                 fps_vs_off_pct=100*(fps/mean(off, 'fps')-1) if off else None,
                 nonrenderer_vs_off_pct=100*(nr/mean(off, 'nonrenderer')-1) if off else None,
                 spread_fps_pct=100*(max(float(r['fps']) for r in rs)/min(float(r['fps']) for r in rs)-1))
        result.append(d)
        f = lambda v: '' if v is None else '%+.2f%%' % v
        lines.append('| %s | %s | %d | %.2f | %s | %s | %.3f | %s | %.1f |' % (
            g, c, len(rs), fps, f(d['fps_vs_baseline_pct']), f(d['fps_vs_off_pct']), nr, f(d['nonrenderer_vs_off_pct']), ev0))
(a.directory/'summary.json').write_text(json.dumps(result, indent=2)+'\n')
(a.directory/'summary.md').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
