#!/usr/bin/env python3
"""Verify paired traces and store small durable evidence (no ROMs/audio/binaries)."""
import hashlib,json,pathlib,re,sys
p=pathlib.Path(sys.argv[1]); dest=pathlib.Path(__file__).parent/'evidence';dest.mkdir(exist_ok=True)
def sha(f):return hashlib.sha256(f.read_bytes()).hexdigest()
def model(a,b):
 def load(f):
  return {(int(k,16),int(n)):[int(e),int(i),int(r),int(w)] for k,n,e,i,r,w,*rest in (line.split() for line in f.read_text().splitlines())}
 aa,bb=load(a),load(b); total=0
 for (pc,n),vals in bb.items():
  d=[v-old for v,old in zip(vals,aa.get((pc,n),[0]*4))]
  if d[0]>0: total+=n*d[0]+70*d[1]+70*d[2]+4*d[3]
 return total/300

def fields(path):
 s=path.read_text(); hash=re.search(r'CORE_AUDIO (\w+)',s); sim=re.search(r'SIM opens (\d+) presents (\d+) .* audio (\d+) work ([.\d]+)ms sleep ([.\d]+)ms level (\d+)',s)
 return dict(core_audio=hash[1],opens=int(sim[1]),presents=int(sim[2]),audio_frames=int(sim[3]),work_ms=float(sim[4]),sleep_ms=float(sim[5]),level=int(sim[6]))
hashes=[]
for a in sorted((p/'hashes').glob('before_*.log')):
 b=a.with_name(a.name.replace('before_','after_'));sa=a.read_text();sb=b.read_text()
 ha=re.findall(r'frame (\d+) hash (\w+)',sa);hb=re.findall(r'frame (\d+) hash (\w+)',sb)
 assert len(ha)==len(hb)==600 and ha==hb,a.name
 fa,fb=fields(a),fields(b); assert fa['core_audio']==fb['core_audio'] and fa['audio_frames']==fb['audio_frames']==441000,a.name
 hashes.append(dict(case=a.stem.removeprefix('before_'),frames=600,draw_hash=ha[-1][1],before=fa,after=fb,trace_before_sha=sha(a),trace_after_sha=sha(b)))
assert len(hashes)==20,len(hashes)
(dest/'hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
cycles=[]
for a in sorted((p/'cache').glob('before_*.cycles')):
 b=a.with_name(a.name.replace('before_','after_'));row=dict(case=a.stem.removeprefix('before_'))
 for v,f in [('before',a),('after',b)]:
  s=f.read_text();row[v]=dict(model_cycles_m=model(f.with_name(f.stem+'_300.cache'),f.with_name(f.stem+'_600.cache'))/1e6,summary=s.splitlines()[0],profile=s.splitlines()[1:])
 cycles.append(row)
assert len(cycles)==12,len(cycles)
(dest/'cycles.json').write_text(json.dumps(cycles,indent=2)+'\n')
paced=[]
for a in sorted((p/'paced').glob('before_*.log')):
 b=a.with_name(a.name.replace('before_','after_'));fa,fb=fields(a),fields(b)
 assert fa['core_audio']==fb['core_audio'],a.name
 paced.append(dict(case=a.stem.removeprefix('before_'),before=fa,after=fb))
assert len(paced)==12,len(paced)
(dest/'paced.json').write_text(json.dumps(paced,indent=2)+'\n')
audio_cost=[]
for f in sorted(p.glob('audio_*.cycles')):
 audio_cost.append(dict(case=f.stem,cycles_per_frame=model(f.with_name(f.stem+'_300.cache'),f.with_name(f.stem+'_600.cache')),profile=f.read_text().splitlines()))
(dest/'audio-cost.json').write_text(json.dumps(audio_cost,indent=2)+'\n')
(dest/'binary-sha256.json').write_text(json.dumps({f.name:sha(f) for f in [(p/'sim'/n) for n in ['runner_before','runner_after','runner_before_hash','runner_after_hash']]},indent=2)+'\n')
print('20 pairs x 600 draw hashes and all core audio hashes match; 12 cycle and 12 paced pairs summarized')
for c in cycles: print(c['case'],c['before']['model_cycles_m'],c['after']['model_cycles_m'])
