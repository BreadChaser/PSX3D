#!/usr/bin/env python3
"""Use scoped renderer PMU subtraction; never call model cycles hardware data."""
import argparse,csv,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);a=p.parse_args()
def rows(label):
    result=list(csv.DictReader((a.directory/label/'old/results.tsv').open(),delimiter='\t'))
    for r in result:
        assert r['status']=='ok' and int(r['samples'])>=15
        assert float(r['renderer_samples'])>0 and float(r['renderer_mcycles'])>=0
        r['game']=dict(w.split('=',1) for w in r['request'].split() if '=' in w)['game']
        r['nonrenderer']=float(r['mcycles'])-float(r['renderer_mcycles'])
        assert r['nonrenderer']>0
    return result
base1,base2,trial=rows('A1'),rows('A2'),rows('B')
results=[]
for before,after in zip(base1,base2):
    assert before['game']==after['game']
    group=[r for r in trial if r['game']==before['game']]
    assert len(group)==3 and ['dyn3_mem='+v in r['request'] for v,r in zip(['off','on','off'],group)]==[True]*3
    off1,on,off2=group
    off=(off1['nonrenderer']+off2['nonrenderer'])/2
    bracket=(before['nonrenderer']+after['nonrenderer'])/2
    fps_ref=(float(before['fps'])+float(after['fps']))/2
    r=dict(game=before['game'],A1=before['nonrenderer'],off1=off1['nonrenderer'],on=on['nonrenderer'],off2=off2['nonrenderer'],A2=after['nonrenderer'],
        switch_reduction_pct=100*(1-on['nonrenderer']/off),rc2_reduction_pct=100*(1-on['nonrenderer']/bracket),
        off_drift_pct=100*(off2['nonrenderer']/off1['nonrenderer']-1),rc2_drift_pct=100*(after['nonrenderer']/before['nonrenderer']-1),
        fps_rc2=fps_ref,fps_off=(float(off1['fps'])+float(off2['fps']))/2,fps_on=float(on['fps']),
        off_fps_change_pct=100*((float(off1['fps'])+float(off2['fps']))/2/fps_ref-1),on_fps_change_pct=100*(float(on['fps'])/fps_ref-1))
    results.append(r)
lines=['Non-renderer Mcycles/vblank = run mcycles − scoped renderer_mcycles.','',
    '| Game | RC2 A1 | Candidate off1 | on | off2 | RC2 A2 | Switch reduction | vs RC2 |',
    '|---|---:|---:|---:|---:|---:|---:|---:|']
for r in results:lines.append('| {game} | {A1:.3f} | {off1:.3f} | {on:.3f} | {off2:.3f} | {A2:.3f} | {switch_reduction_pct:.2f}% | {rc2_reduction_pct:.2f}% |'.format(**r))
(a.directory/'summary.json').write_text(json.dumps(results,indent=2)+'\n');(a.directory/'summary.md').write_text('\n'.join(lines)+'\n');print('\n'.join(lines))
