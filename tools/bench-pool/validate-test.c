#define _GNU_SOURCE
#include "benchstate.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void) {
 const char *bad[]={"Crash Bandicoot (USA)","Silent Hill (USA)","R4 - Ridge Racer Type 4 (USA)","Gran Turismo 2 (USA) (Arcade Mode)","CTR - Crash Team Racing (USA)","Castlevania - Symphony of the Night (USA)","../Spyro","A/B","A\\B","A:B",""};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!bench_state_name(bad[i]));
 assert(bench_state_name("Spyro the Dragon (USA)"));
 assert(bench_state_name("Crash Bandicoot 2 - Cortex Strikes Back (USA)"));
 unsigned char h[40]={0};memcpy(h,"STv4 PCSXra",11);h[32]=6;h[34]=0x41;h[35]=0x8b;h[36]=1;
 assert(bench_state_header(h,BENCH_STATE_BYTES));
 assert(!bench_state_header(h,BENCH_STATE_BYTES-1));h[36]=0;assert(!bench_state_header(h,BENCH_STATE_BYTES));h[36]=1;
 h[32]=5;assert(!bench_state_header(h,BENCH_STATE_BYTES));h[32]=6;h[0]='X';assert(!bench_state_header(h,BENCH_STATE_BYTES));
 char dir[]="/tmp/benchstate-test-XXXXXX";assert(mkdtemp(dir));char stage[256],dest[256];
 snprintf(stage,sizeof(stage),"%s/stage",dir);snprintf(dest,sizeof(dest),"%s/state",dir);
 int fd=open(stage,O_CREAT|O_WRONLY,0600);assert(fd>=0);assert(!ftruncate(fd,BENCH_STATE_BYTES));close(fd);
 assert(bench_state_commit(stage,dest));assert(!bench_state_commit(stage,dest));
 FILE *f=fopen(dest,"r+b");assert(f);assert(fwrite("KEEP",1,4,f)==4);fclose(f);assert(!bench_state_commit(stage,dest));
 char b[4];f=fopen(dest,"rb");assert(fread(b,1,4,f)==4&&!memcmp(b,"KEEP",4));fclose(f);unlink(dest);
 assert(!truncate(stage,80));assert(!bench_state_commit(stage,dest));assert(access(dest,F_OK));
 unlink(stage);rmdir(dir);puts("benchmark state envelope/protection/exclusive-commit checks passed");
}
