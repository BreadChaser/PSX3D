#!/usr/bin/env python3
"""hwsum.py <session dir> <label...>: per-scene fps / speed / mcycles for fastbench runs"""
import csv,sys,glob
from pathlib import Path
d=Path(sys.argv[1]); labels=sys.argv[2:]
def rd(l):
    f=glob.glob(str(d/l/'*'/'results.tsv'))
    if not f: return None
    rows=list(csv.DictReader(open(f[0]),delimiter='\t'))
    return [(r.get('game') or r.get('request','')[:30], float(r['fps']) if r['fps'] not in ('','NA') else 0,
             float(r['speed_pct']) if r['speed_pct'] not in ('','NA') else 0, float(r['mcycles']) if r['mcycles'] not in ('','NA') else 0, r['status']) for r in rows]
data={l:rd(l) for l in labels}
names=['Crash 1','CTR','R4','SotN','SH garage','SH alley','SH cafe','GT2','Tekken 3','FFVII','Klonoa','Crash 2','NFS HS']
print('%-10s'%'scene'+''.join('%18s'%l for l in labels if data[l]))
for k,n in enumerate(names):
    row='%-10s'%n
    for l in labels:
        if not data[l]: continue
        if k<len(data[l]):
            g,f,s,m,st=data[l][k]; row+='%9.2f %4.1f%% %s'%(f,s,'' if st=='ok' else st[:2])
    print(row)
