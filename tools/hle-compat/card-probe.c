typedef unsigned u32;
extern u32 bios_a0(u32,u32,u32,u32);
extern u32 bios_b0(u32,u32,u32,u32);
static volatile u32 *out=(volatile u32 *)0x800f0000;
static unsigned direntry[12];
static unsigned char writebuf[24576],readbuf[24576];
#ifndef BLOCKS
#define BLOCKS 2
#endif
void main(void) {
 out[0]=1;
 out[1]=bios_b0(0x4a,1,0,0);
 out[2]=bios_b0(0x4b,0,0,0);
 out[3]=bios_a0(0x70,0,0,0);
 out[4]=bios_b0(0x32,(u32)"bu00:TESTFILE",(BLOCKS<<16)|0x200,0);
 if(out[4]!=0xffffffff) {
  for(int i=0;i<BLOCKS*8192;i++)writebuf[i]=(unsigned char)(i*13+(i>>8));
  out[5]=bios_b0(0x35,out[4],(u32)writebuf,BLOCKS*8192);
  out[6]=bios_b0(0x36,out[4],0,0);
  out[7]=bios_b0(0x32,(u32)"bu00:TESTFILE",1,0);
  out[8]=bios_b0(0x34,out[7],(u32)readbuf,BLOCKS*8192);
  out[9]=0;
  for(int i=0;i<BLOCKS*8192;i++)if(readbuf[i]!=writebuf[i])out[9]++;
  out[10]=bios_b0(0x36,out[7],0,0);
 }
 if(BLOCKS>1 && out[4]!=0xffffffff) {
  out[14]=bios_b0(0x32,(u32)"bu00:TESTFILE",3,0);
  out[15]=bios_b0(0x33,out[14],8064,0);
  out[16]=bios_b0(0x34,out[14],(u32)readbuf,256);
  out[17]=0;
  for(int i=0;i<256;i++)if(readbuf[i]!=writebuf[8064+i])out[17]++;
  bios_b0(0x33,out[14],8064,0);
  for(int i=0;i<256;i++)readbuf[i]=0x3c;
  out[18]=bios_b0(0x35,out[14],(u32)readbuf,256);
  bios_b0(0x33,out[14],8064,0);
  bios_b0(0x34,out[14],(u32)readbuf,256);
  out[19]=0;
  for(int i=0;i<256;i++)if(readbuf[i]!=0x3c)out[19]++;
  bios_b0(0x36,out[14],0,0);
 }
 out[11]=bios_b0(0x42,(u32)"bu00:TESTFILE",(u32)direntry,0);
 out[12]=direntry[6];
#ifdef DELETE_TEST
 out[13]=bios_b0(0x45,(u32)"bu00:TESTFILE",0,0);
#endif
 out[0]=0xdeadbeef;
 for(;;)__asm__ volatile("");
}
