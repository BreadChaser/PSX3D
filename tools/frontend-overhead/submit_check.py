#!/usr/bin/env python3
"""Compile the actual NDSP submit implementation against a strict buffer fixture."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
src=(root/'psx3d/source/audio.c').read_text()
submit=src[src.index('static void submit('):src.index('\nsize_t audio_write(')]
fixture=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
typedef uint32_t u32;
#define BUF_FRAMES 512
#define NBUF 16
#define CAP_FRAMES 3000
static struct { void *data_pcm16; } wb[NBUF];
static u32 memory[NBUF][BUF_FRAMES], capture[CAP_FRAMES+4];
static bool busy[NBUF], capturing, use_csnd;
static unsigned cur, cur_fill, cap_played_n, submitted, flushed;
static u32 *cap_played=capture;
static struct {unsigned dropped;} st;
static bool statsview_collecting(void) { return true; }
static bool buf_free(int i) { return !busy[i]; }
static void csnd_submit(const u32 *f, size_t n) { (void)f; (void)n; abort(); }
static void DSP_FlushDataCache(void *p, unsigned n) {
 assert(p==memory[cur]); assert(n==BUF_FRAMES*4); flushed++;
}
static void ndspChnWaveBufAdd(int ch, void *buf) {
 assert(ch==0); assert(buf==&wb[cur]); assert(!busy[cur]); busy[cur]=true; submitted++;
}
'''
main=r'''
int main(void) {
 u32 input[10000]; for(unsigned i=0;i<10000;i++) input[i]=0x12340000+i;
 for(unsigned i=0;i<NBUF;i++) wb[i].data_pcm16=memory[i];
 capturing=true; capture[CAP_FRAMES]=0x87654321;
 unsigned sizes[]={1,37,735,1024,13,4096,1730,900,1464}; unsigned at=0;
 for(unsigned j=0;j<sizeof(sizes)/sizeof(*sizes);j++) {submit(input+at,sizes[j]); at+=sizes[j];}
 assert(at==10000); assert(cur_fill==0); assert(submitted==16); assert(flushed==16);
 assert(st.dropped==10000-NBUF*BUF_FRAMES);
 for(unsigned i=0;i<NBUF*BUF_FRAMES;i++) assert(memory[i/BUF_FRAMES][i%BUF_FRAMES]==input[i]);
 assert(cap_played_n==CAP_FRAMES); assert(!memcmp(capture,input,CAP_FRAMES*4));
 assert(capture[CAP_FRAMES]==0x87654321);
 // A retired buffer can be reused, including a partial callback boundary.
 busy[0]=false; capturing=false; submit(input,7); submit(input+7,505);
 assert(cur==1 && cur_fill==0 && submitted==17); assert(!memcmp(memory[0],input,512*4));
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d); (p/'test.c').write_text(fixture+submit+main)
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-g','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('actual NDSP submit: partial buffers, wrap, queue-full drops and capture bound passed (ASan/UBSan)')
