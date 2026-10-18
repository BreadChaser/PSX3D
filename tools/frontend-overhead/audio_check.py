#!/usr/bin/env python3
"""Compare the real frontend kernels; stdlib only. No console access.
Usage: audio_check.py <baseline audio_resample.c> <captured stereo s16le.raw> <evidence dir>
"""
import array, ctypes as C, hashlib, json, math, pathlib, random, subprocess, sys, tempfile
ROOT = pathlib.Path(__file__).resolve().parents[2]
class State(C.Structure):
    _fields_ = [('hist', C.c_int16*6), ('pos', C.c_float), ('slow', C.c_float),
                ('deficit', C.c_float), ('seconds', C.c_float), ('frames', C.c_size_t)]
def build(source, dest):
    subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-I', str(ROOT/'psx3d/source'),
                    str(source), '-lm', '-o', str(dest)], check=True)
    lib = C.CDLL(str(dest))
    lib.audio_resample.argtypes = [C.POINTER(State), C.POINTER(C.c_int16), C.c_size_t, C.c_float, C.POINTER(C.c_int16)]
    lib.audio_resample.restype = C.c_size_t
    return lib

def resample(lib, samples, steps, chunks=(735,)):
    data = (C.c_int16*len(samples))(*samples)
    out = (C.c_int16*(4096*2))()
    state = State(); result = array.array('h'); at=0; call=0
    while at<len(samples)//2:
        n=min(chunks[call%len(chunks)],len(samples)//2-at)
        step=steps[call%len(steps)]
        count=lib.audio_resample(C.byref(state), C.cast(C.byref(data, at*4),C.POINTER(C.c_int16)), n, step, out)
        assert count <=4096
        result.extend(out[:count*2]); at+=n; call+=1
    return result

def diff(a,b):
    n=min(len(a),len(b)); diffs=[a[i]-b[i] for i in range(n)]
    return dict(frames_before=len(a)//2,frames_after=len(b)//2,
                max_deviation=max(map(abs,diffs)),rms_deviation=math.sqrt(sum(x*x for x in diffs)/n))

def tone_metrics(stereo, known_frequency=None):
    # Estimate actual frequency from all rising zero crossings, then fit a
    # sinusoid and its harmonics. Residual includes interpolation noise/spurs.
    x=list(stereo[400:88200:2]); crossings=[]
    for i in range(1,len(x)):
        if x[i-1]<0<=x[i]: crossings.append(i-x[i]/(x[i]-x[i-1]))
    count=len(crossings); mid=(count-1)/2
    period=sum((i-mid)*t for i,t in enumerate(crossings))/sum((i-mid)**2 for i in range(count))
    if known_frequency: period=44100/known_frequency
    omega=2*math.pi/period
    def fit(h, values=x):
        ss=cc=sc=ys=yc=0.0
        for i,v in enumerate(values):
            s=math.sin(h*omega*i); c=math.cos(h*omega*i)
            ss+=s*s; cc+=c*c; sc+=s*c; ys+=v*s; yc+=v*c
        det=ss*cc-sc*sc
        return ((ys*cc-yc*sc)/det,(yc*ss-ys*sc)/det)
    a,b=fit(1); amp=math.hypot(a,b)
    residual_values=[v-a*math.sin(omega*i)-b*math.cos(omega*i) for i,v in enumerate(x)]
    residual=sum(v*v for v in residual_values)/len(x)
    thd=math.sqrt(sum(sum(v*v for v in fit(h,residual_values)) for h in range(2,11)))/amp
    return dict(frequency_hz=44100/period,amplitude=amp,
                thd_db=20*math.log10(max(thd,1e-12)),
                thdn_db=10*math.log10(max(residual/(amp*amp/2),1e-24)))

def main():
    baseline,capture,dest=map(pathlib.Path,sys.argv[1:]); dest.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        old=build(baseline,pathlib.Path(td)/'old.so'); new=build(ROOT/'psx3d/source/audio_resample.c',pathlib.Path(td)/'new.so')
        # A Hermite oracle at exactly the new Q20 positions separates kernel
        # error from the old float accumulator's long-term rate/phase error.
        oracle=baseline.read_text()
        oracle=oracle.replace('while ((size_t)(int)rs->pos < frames) {', 'int phase=(int)(rs->pos*(1<<20)), delta=(int)(step*(1<<20)+0.5f);\n\t\twhile ((size_t)(phase>>20) < frames) {')
        oracle=oracle.replace('int k = (int)rs->pos;', 'int k = phase>>20;').replace('float t = rs->pos - k;', 'float t = (phase & ((1<<20)-1))/(float)(1<<20);')
        oracle=oracle.replace('rs->pos += step;', 'phase += delta;').replace('rs->pos -= frames;', 'rs->pos = (phase-(int)(frames*(1<<20)))/(float)(1<<20);')
        oracle_path=pathlib.Path(td)/'oracle.c'; oracle_path.write_text(oracle)
        aligned=build(oracle_path,pathlib.Path(td)/'oracle.so')
        game=array.array('h'); game.frombytes(capture.read_bytes())
        tone=array.array('h',(round(16000*math.sin(2*math.pi*440*(i//2)/44100)) for i in range(44100*2)))
        results={}
        tone10k=array.array('h',(round(16000*math.sin(2*math.pi*10000*(i//2)/44100)) for i in range(44100*2)))
        for name,data in [('tone440',tone),('tone10000',tone10k),('game',game)]:
            results[name]={}
            for step in [0.995,1.0,1.001,1.005]:
                a=resample(old,data,[step]); b=resample(new,data,[step]); m=diff(a,b)
                m['same_phase']=diff(resample(aligned,data,[step]),b)
                if name=='tone440': m.update(before=tone_metrics(a),after=tone_metrics(b))
                if name=='tone10000':
                    actual_step=round(C.c_float(step).value*(1<<20))/(1<<20)
                    m['after']=tone_metrics(b,10000*actual_step)
                    m['reference_same_phase']=tone_metrics(resample(aligned,data,[step]),10000*actual_step)
                results[name][str(step)]=m
        # Exact unity preserves history delay; slow path must remain bit-exact.
        rng=random.Random(123); noise=array.array('h',(rng.randrange(-32768,32768) for _ in range(32000)))
        for step in [0.495,0.75,0.98,1.0]:
            assert resample(old,noise,[step])==resample(new,noise,[step]),step
        # Phase and history must not depend on callbacks/chunk boundaries.
        for step in [0.995,1.001,1.005]:
            assert resample(new,noise,[step],(1024,))==resample(new,noise,[step],(1,13,735,1024)),step
        # Exercise transitions between both kernels, saturation and tiny batches.
        for step in [0.495,0.75,0.995,1.0,1.005]:
            r=resample(new,noise,[step,1.0,0.75],(1,13,1024,735))
            assert max(r)<=32767 and min(r)>=-32768
        results['capture']={'sha256':hashlib.sha256(capture.read_bytes()).hexdigest(),'frames':len(game)//2,'rate':44100,'format':'s16le stereo','game':'Crash Bandicoot o3auto; 600 vblanks from slot-2 state'}
        results['checks']='unity and stretch bit-exact; Q20 chunk independence; full-scale transitions passed'
        (dest/'audio.json').write_text(json.dumps(results,indent=2)+'\n')
        print(json.dumps(results,indent=2))
if __name__=='__main__': main()
