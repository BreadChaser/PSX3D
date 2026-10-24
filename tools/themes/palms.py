#!/usr/bin/env python3
"""Rebuild code-drawn Original palm spans from the shipped CC0 silhouettes.
Requires Pillow (build time only). Dimensions/placement mirror make_art.py.
"""
from pathlib import Path
from PIL import Image,ImageChops
W=Path(__file__).resolve().parents[2]
canvas=Image.new('L',(800,480))
for name,x,h,flip in [('a',.13,310,False),('b',.86,320,True)]:
 im=Image.open(W/f'tools/art/palms_pair_{name}.png').convert('RGBA');a=im.getchannel('A')
 if a.getextrema()[0]==255:a=ImageChops.invert(im.convert('L'))
 a=a.crop(a.getbbox());a=a.crop((0,0,a.width,int(a.height*.94)))
 a=a.resize((int(a.width*h/a.height),h),Image.Resampling.LANCZOS)
 if flip:a=a.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
 canvas.paste(a,(int(x*800-a.width/2),int(280+30-h)))
spans=[]
for y in range(310):
 run=None
 for x in range(801):
  filled=x<800 and canvas.getpixel((x,y))>=110
  if filled and run is None:run=x
  if not filled and run is not None:spans.append((run,y,x-run));run=None
p=W/'psx3d/source/scene_palms.h'
p.write_text('// Code-drawn spans traced from the shipped CC0 palm silhouettes.\n// Source: tools/art/palms_pair_a.png + palms_pair_b.png (CREDITS.md).\n// Half-pixel rectangles rasterized once on menu entry; no bitmap asset.\nstatic const unsigned short original_palm_spans[][3] = {\n'+''.join('{%d,%d,%d},'%v+ ('\n' if i%6==5 else '') for i,v in enumerate(spans))+'\n};\n')
print(len(spans),'spans')
