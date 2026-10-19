#!/usr/bin/env python3
"""Exercise the actual shared CPU scanout with dirty, alternating host buffers."""
import argparse
from pathlib import Path
import subprocess
import tempfile
p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[3] / 'core/frontend/libretro.c')
p.add_argument('--expect-bug', action='store_true')
args = p.parse_args()
s = args.source.read_text()
a = s.index('static void vout_flip(')
b = s.index('\n#ifdef _3DS', a)
function = s[a:b].replace('memset(', 'counted_memset(')
setup = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef uint32_t u32;
typedef void bgr_to_fb_func(void *, const void *, int);
#define RETRO_PIXEL_FORMAT_XRGB8888 1
#define RETRO_PIXEL_FORMAT_RGB565 2
#define PSE_PAD_TYPE_GUNCON 3
#define PSE_PAD_TYPE_GUN 4
#define min(a,b) ((a)<(b)?(a):(b))
static unsigned clears;
static void *counted_memset(void *p, int c, size_t n) { clears++; return memset(p,c,n); }
static void copy_pixels(void *dst, const void *src, int w) {
 (void)src;
 for (int i=0; i<w; i++) ((uint16_t *)dst)[i]=0x1234;
}
static struct { bgr_to_fb_func *cspace_blit; unsigned flip_cnt; } pl_rearmed_cbs = {copy_pixels,0};
static int current_fmt = RETRO_PIXEL_FORMAT_RGB565;
static void *vout_buf_ptr, *vout_cleared;
static bool vout_clear_other;
static int vout_width=320, vout_height=240, vout_pitch_b=2048;
static int psx_w=320, vout_fb_dirty;
static int in_enable_crosshair[2], in_type[2];
struct CrosshairInfo {int pos_x,pos_y,thickness,size_x,size_y;};
static void CrosshairDimensions(int p,struct CrosshairInfo *i) {(void)p; memset(i,0,sizeof(*i));}
static void addCrosshair(int a,int b,unsigned short *c,int d,int e,int f,int g,int h,int i)
 {(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;}
'''
checks = r'''
static uint16_t buffers[2][1024*240];
static uint8_t vram[1024*512*2];
static int dirty_rows(int buffer,int top,int height) {
 int dirty=0;
 for(int y=0;y<240;y++)for(int x=0;x<320;x++) {
  uint16_t expected=(y>=top && y<top+height)?0x1234:0;
  if(buffers[buffer][y*1024+x]!=expected) {dirty++;break;}
 }
 return dirty;
}
int main(int argc,char **argv) {
 bool expect_bug=argc>1;
 int total_dirty=0;
 for(int rgb24=0;rgb24<=1;rgb24++) {
  int top=rgb24?16:8, height=240-top*2;
  memset(buffers,0x5a,sizeof(buffers));
  vout_cleared=NULL;vout_clear_other=true;
  vout_buf_ptr=buffers[0];vout_flip(vram,0,rgb24,0,top,320,height,1);
  // Reusing the same buffer (held/skipped frames) must not consume its peer's invalidation.
  vout_flip(vram,0,rgb24,0,top,320,height,0);
  vout_buf_ptr=buffers[1];vout_flip(vram,0,rgb24,0,top,320,height,0);
  int dirty=dirty_rows(0,top,height)+dirty_rows(1,top,height);
  printf("%s dirty border rows: %d\n",rgb24?"RGB24 movie":"RGB555 gameplay",dirty);
  total_dirty+=dirty;
  if(!expect_bug) {
   assert(dirty==0);
   unsigned before=clears;
   for(int i=0;i<8;i++) {
    vout_buf_ptr=buffers[i&1];vout_flip(vram,0,rgb24,0,top,320,height,0);
   }
   assert(clears==before); // No steady-state memory clears.
   // A full-height picture followed by a shorter range at unchanged 320x240.
   for(int i=0;i<2;i++) {
    vout_buf_ptr=buffers[i];vout_flip(vram,0,rgb24,0,0,320,240,i==0);
   }
   for(int i=0;i<2;i++) {
    vout_buf_ptr=buffers[i];vout_flip(vram,0,rgb24,0,top,320,height,i==0);
   }
   assert(!dirty_rows(0,top,height) && !dirty_rows(1,top,height));
   // Display blanking invalidates the peer too.
   vout_buf_ptr=buffers[0];vout_flip(NULL,0,rgb24,0,0,320,240,0);
   assert(!dirty_rows(0,240,0));
   vout_buf_ptr=buffers[1];vout_flip(vram,0,rgb24,0,top,320,height,0);
   assert(!dirty_rows(1,top,height));
   for(int i=0;i<2;i++)for(int y=0;y<240;y++)
    for(int x=320;x<1024;x++)assert(buffers[i][y*1024+x]==0x5a5a);
  }
 }
 if(expect_bug)assert(total_dirty>0);
 puts(expect_bug?"Baseline defect reproduced":"Alternating scanout border regression: PASS");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='psx3d-vout-') as tmp:
    src = Path(tmp) / 'check.c'
    src.write_text(setup + function + checks)
    exe = Path(tmp) / 'check'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-unused-variable','-Wno-unused-parameter',
                    '-fsanitize=address,undefined','-g',str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)] + (['bug'] if args.expect_bug else []),check=True)
