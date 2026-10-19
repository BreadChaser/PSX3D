#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "widescreen.h"
static unsigned char ram[0x200000];
static int invalidations;
void *ws_memory(void) { return ram; }
void ws_invalidate(uint32_t a, unsigned n) { assert(!(a&3) && n && !(n&3)); invalidations++; }
static void file(const char *text) { FILE *f=fopen("ws/SLUS12345-01020304.ws","w"); assert(f);fputs(text,f);fclose(f); }
#define HEAD "PSX3D-WS-1\naspect=16:9\nmodels=1,1\n"
int main(int argc, char **argv) {
 if (argc == 4) {
  for (int newer=0; newer<2; newer++) {
   assert(ws_load(argv[1],argv[2],(uint32_t)strtoul(argv[3],NULL,16),newer));
   bool allowed=ws_available();
   assert(ws_enable(true)==allowed);ws_frame();ws_enable(false);
  }
  return 0;
 }
 mkdir("ws",0700);
 file(HEAD "A7000010 12345678\n80000020 0000ABCD\n90000024 12345678\nA4000028 00000002\n30000030 00000009\n00000000 0000FFFF\n");
 ram[16]=0x34;ram[17]=0x12;ram[32]=7;ram[48]=4;
 assert(ws_load("ws","slus12345",0x01020304,1)); assert(ws_enable(1));ws_frame();
 assert(ram[16]==0x78 && ram[17]==0x56 && ram[32]==0xCD && ram[36]==0x78 && ram[48]==4);
 ram[40]=2;ws_frame();assert(ram[48]==9);ram[32]=0x66; // game replaces a patched location
 ws_enable(0);assert(ram[16]==0x34 && ram[17]==0x12 && ram[32]==0x66 && ram[36]==0 && ram[48]==4);
 assert(invalidations==7);
 ws_unload();assert(!ws_load("ws","SLUS12345",0x01020305,1));assert(!ws_enable(1));
 file(HEAD "80000020 00000099\nA6000010 00000002\n");assert(!ws_load("ws","SLUS12345",0x01020304,1)); ws_frame();assert(ram[32]==0x66);
 file(HEAD "80000020 00000099\n90000022 00000001\n");assert(!ws_load("ws","SLUS12345",0x01020304,1));
 file(HEAD "80200000 00000099\n");assert(!ws_load("ws","SLUS12345",0x01020304,1));
 file(HEAD "A4000010 00000099\n80000020 00000099\n");assert(!ws_load("ws","SLUS12345",0x01020304,1));
 file(HEAD "D0000020 00000099\n");assert(!ws_load("ws","SLUS12345",0x01020304,1));
 file("PSX3D-WS-1\naspect=16:9\nmodels=0,1\n80000020 00000099\n");
 assert(ws_load("ws","SLUS12345",0x01020304,0));assert(!ws_available());assert(!ws_enable(1));ws_frame();assert(ram[32]==0x66);
 assert(ws_load("ws","SLUS12345",0x01020304,1));assert(ws_available());assert(ws_enable(1));ws_frame();assert(ram[32]==0x99);ws_unload();assert(ram[32]==0x66);
 // Costly: offered with a warning. Too slow: held back and explained. Unknown values reject.
 file("PSX3D-WS-1\naspect=16:9\nmodels=2,3\n80000020 00000099\n");
 assert(ws_load("ws","SLUS12345",0x01020304,0));assert(ws_available());assert(ws_console_policy()==WS_COSTLY);
 assert(ws_load("ws","SLUS12345",0x01020304,1));assert(!ws_available());assert(ws_console_policy()==WS_TOO_SLOW);assert(!ws_enable(1));
 file("PSX3D-WS-1\naspect=16:9\nmodels=4,1\n80000020 00000099\n");
 assert(!ws_load("ws","SLUS12345",0x01020304,0));assert(ws_console_policy()==-1);assert(ws_available());
 file(HEAD "A7000010 12345678\n");ram[16]=0x78;ram[17]=0x56;
 assert(ws_load("ws","SLUS12345",0x01020304,1));ws_enable(1);ws_frame();ws_enable(0);assert(ram[16]==0x34 && ram[17]==0x12);
 // A patched state loaded with Widescreen off must not retain its patches.
 ram[16]=0x78;ram[17]=0x56;ws_state_loaded();assert(ram[16]==0x34 && ram[17]==0x12);
 file(HEAD "A4000040 12345678\nA7000010 12345678\n00000000 FFFF\n");
 assert(ws_load("ws","SLUS12345",0x01020304,1));ram[16]=0x78;ram[17]=0x56;
 ws_state_loaded();assert(ram[16]==0x78); // another overlay: guard no longer matches
 file(HEAD "D0000040 00000001\nA7000010 12345678\n");
 assert(ws_load("ws","SLUS12345",0x01020304,1));ram[64]=1;ram[16]=0x34;ram[17]=0x12;
 ws_enable(1);ws_frame();assert(ram[16]==0x78);
 ram[64]=2;ws_enable(0);assert(ram[16]==0x78); // replacement overlay owns this word now
 ram[64]=1;ws_state_loaded();assert(ram[16]==0x34); // matching patched state still rolls back
 assert(!ws_load("ws","../S12345",0,1));puts("bundle selection, atomic validation, guards, rollback and console limits PASS");
}
