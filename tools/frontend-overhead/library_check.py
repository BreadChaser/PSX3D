#!/usr/bin/env python3
"""Exercise actual video teardown with held/open PICA frames and strict lifetimes."""
import argparse, subprocess, tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
s=(a.source or root/'psx3d/source/video.c').read_text()
def function(start,end):return s[s.index(start):s.index(end,s.index(start))]
body=function('void video_game_end(void)','\nvoid video_exit(void)')
idle=function('static void idle_on_gpu_thread(void *data)','\nstruct present_request')
hold=function('static void hold_on_gpu_thread(void *unused)','\n// UI screens')
fixture=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint64_t u64;
static bool hardware_active, gpu_frame, have_frame, pica_bound, alive, submitted, pending_vertices;
static bool ui_layout, ui_depth, ui_texenv, ui_uniforms, ui_scissor, ui_alpha, ui_blend;
static void *pending;
static u64 frame_end_tick;
static int (*pica_thread_call)(void (*)(void *), void *);
static void video_gpu_idle(void);
static void video_hold(void);
static bool C3D_FrameBegin(int flags) { (void)flags; assert(!gpu_frame); submitted=false; return true; }
static void C3D_FrameEnd(int flags) { (void)flags; assert(alive); if(gpu_frame) submitted=true; }
static u64 svcGetSystemTick(void) { return 0; }
static void pica_frame_ended(void) {}
static void pica_prepare_present(void) { assert(gpu_frame && alive); pending_vertices=false; }
static void C2D_Prepare(void) {
 // citro3d consults the previously bound shader, which must still exist.
 assert(!pica_bound || alive);
 pica_bound=false; ui_layout=ui_depth=ui_texenv=ui_uniforms=true;
}
#define GPU_SCISSOR_DISABLE 0
#define GPU_ALWAYS 0
#define GPU_BLEND_ADD 0
#define GPU_SRC_ALPHA 0
#define GPU_ONE_MINUS_SRC_ALPHA 0
static void C3D_SetScissor(int a,int b,int c,int d,int e) {(void)a;(void)b;(void)c;(void)d;(void)e;ui_scissor=true;}
static void C3D_AlphaTest(bool a,int b,int c) {(void)a;(void)b;(void)c;ui_alpha=true;}
static void C3D_AlphaBlend(int a,int b,int c,int d,int e,int f) {(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;ui_blend=true;}
static void pica_exit(void) {
 assert(!gpu_frame && !submitted && !pending_vertices);
 assert(!pica_bound); // no dangling citro3d shader/vertex-buffer binding
 alive=false;
}
static void music_reload(void) {}
'''
main=r'''
int main(void) {
 for (int state=0;state<3;state++) {
  hardware_active=alive=have_frame=true;
  // 0: last presentation restored C2D; 1: held PICA-only frame;
  // 2: pause entered after retro_run, before presentation/FrameEnd.
  pica_bound=state!=0;gpu_frame=pending_vertices=state==2;submitted=state!=2;
  ui_layout=ui_depth=ui_texenv=ui_uniforms=ui_scissor=ui_alpha=ui_blend=state==0;
  video_game_end();
  assert(!hardware_active && !have_frame && !pending && !alive);
  assert(ui_layout && ui_depth && ui_texenv && ui_uniforms && ui_scissor && ui_alpha && ui_blend);
 }
 puts("actual video teardown: presented/held/open frames, resource retirement and UI restore PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 q=Path(d);(q/'test.c').write_text(fixture+body+idle+hold+main)
 subprocess.run(['cc','-O1','-g','-Wall','-fsanitize=address,undefined',str(q/'test.c'),'-o',str(q/'test')],check=True)
 subprocess.run([str(q/'test')],check=True)
