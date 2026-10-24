#!/usr/bin/env python3
"""Export deterministic seeds and layer parameters from the approved local mock.
Usage: import-scenes.py /path/to/ui-mock (requires node; no browser).
Palette/icon roles live separately in theme.c and check-mock.py.
"""
import json, pathlib, subprocess, sys
W=pathlib.Path(__file__).resolve().parents[2]
root=pathlib.Path(sys.argv[1]).resolve()
js="""const fs=require('fs'),vm=require('vm');
const c=vm.createContext({});
vm.runInContext(fs.readFileSync(process.argv[1]+'/js/backdrops.js','utf8').replace('return { opt, draw, drawBottom,','return { tables:{STARS,SF,GX,CN,FL,SIG,EM,CR,SUN}, opt, draw, drawBottom,')+';this.scenes=Backdrops.SCENES;this.tables=Backdrops.tables;',c);
console.log(JSON.stringify({scenes:c.scenes,tables:c.tables}));"""
m=json.loads(subprocess.check_output(['node','-e',js,str(root)],text=True));tables=m['tables']
order=['Original (v0.5.0)','Midnight teal','PS1 grey','Sunset magenta','High contrast','Phosphor','Eclipse','Ember']
# Pure, seeded effect data from approved mock. No runtime RNG or image assets.
f=['// Approved round-3 mock parameters; seeded data exported from devtools/ui-mock/js/backdrops.js.\n']
for key in ['STARS','SF','GX','CN','FL','EM','CR']:
    arr=tables[key]; keys=list(arr[0]); typ=key.lower()+'_seed_t'
    f+=['typedef struct { float '+','.join(keys)+'; } '+typ+';\nstatic const '+typ+' '+key+'[] = {\n']
    for row in arr: f+=['{'+','.join(str(int(row[k])) if isinstance(row[k],bool) else str(row[k]) for k in keys)+'},\n']
    f+=['};\n']
f+=['static const struct { float ph,v,len; float pts[27][2]; } SIG[] = {\n']
for row in tables['SIG']['P']: f+=['{'+','.join(str(row[k]) for k in ['ph','v','len'])+',{'+','.join('{'+','.join(map(str,p))+'}' for p in row['pts'])+'}},\n']
f+=['};\n']
# Layer params with fixed arrays, zero defaults interpreted by renderer.
fx=['sky','light','stars','galaxy','constellations','shade','vignette','silk','flow','signals','glyphs','starfield','corona','embers','sunset','sunsetFloor']
f+=['enum { '+','.join('FX_'+x.upper() for x in fx)+' };\n']
f+=['typedef struct { int fx; float i,z,v,t0,n,drift,y0,y1,a,steps,grid,cx,cy,r,width,yShift,strands; int nf,ns; float fans[3][6], stops[5]; u32 colours[5]; } layer_t;\n']
f+=['typedef struct { const char *name; layer_t top[8],bottom[8]; } scene_t;\nstatic const scene_t scenes[] = {\n']
def layer(x):
    name=x[0]; d=x[1] if len(x)>1 else {}; q=['.fx=FX_'+name.upper(),'.i='+str(d.get('i',1)),'.z='+str(d.get('z',1)),'.v='+str(d.get('v',1))]
    for k,v in d.items():
        if k in ['i','z','v','ribbons','glint']:continue
        if k=='stops':
            q+=['.ns='+str(len(v)),'.stops={'+','.join(str(a[0]) for a in v)+'}', '.colours={'+','.join('TH_RGB('+','.join(map(str,a[1]))+')' for a in v)+'}']
        elif k=='fans':
            q+=['.nf='+str(len(v)),'.fans={'+','.join('{'+','.join(map(str,[a[0],a[1],a[2],a[4],a[5],j]))+'}' for j,a in enumerate(v))+'}', '.colours={'+','.join('TH_RGB('+','.join(map(str,a[3]))+')' if a[3] else '0' for a in v)+'}']
        else:q+=['.'+k+'='+str(v)]
    return '{'+','.join(q)+'}'
for name in order:
    sc=m['scenes'][name]
    # Owner correction: Original restores rc9, with no darkening vignette.
    if name=='Original (v0.5.0)':sc=dict(sc,top=[['sunset',{}]])
    f+=['{ "'+sc['name']+'", {'+','.join(layer(x) for x in sc['top'])+'}, {'+','.join(layer(x) for x in sc['bottom'])+'} },\n']
f+=['};\n'];(W/'psx3d/source/scene_data.h').write_text(''.join(f))

sun=tables['SUN']['stars']
f=['\nstatic const struct { float x,y,a,f,p; } SUN_STARS[] = {\n']
f += ['{'+','.join(str(q[k]) for k in ['x','y','a','f','p'])+'},\n' for q in sun]
f += ['};\n']
with (W/'psx3d/source/scene_data.h').open('a') as out: out.write(''.join(f))
