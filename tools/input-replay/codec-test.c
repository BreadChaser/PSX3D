#include "clip.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern unsigned char mock_ram[];
extern unsigned mock_ticks, mock_polls, mock_readonly;
extern bool mock_poison, mock_diverge;
void mock_poll(void);
int16_t mock_input(unsigned,unsigned,unsigned,unsigned);
static void run(void) { while (clip_active) clip_run(); }
int main(void)
{
    assert(clip_switch("record=on") && clip_enabled);
    assert(clip_switch("record_seconds=60") && clip_seconds==60);
    assert(!clip_switch("record_seconds=61") && !clip_switch("replay=../2"));
    assert(!clip_switch("replay=2oops") && clip_switch("replay=123"));
    clip_reset_request(); assert(clip_requested==-1);
    assert(clip_archive_name("recordings/Crash_Bandicoot/1.state"));
    assert(clip_archive_name("recordings/Crash_Bandicoot/1.inp"));
    const char *bad[]={"recordings/../1.inp","recordings/Crash/../1.inp","recordings/Crash/1.inp/extra", "recordings/Crash/1000000.inp", "recordings/Crash/1.inp.tmp", "recordings//1.inp","recordings/C:foo/1.inp","/recordings/Crash/1.inp"};
    for (unsigned k=0;k<sizeof(bad)/sizeof(*bad);k++) assert(!clip_archive_name(bad[k]));
    clip_set_live_input(mock_poll,mock_input);
    assert(clip_record("clip",121,60000,"SCUS12345","pcsx_rearmed_psxclock=57\npcsx_rearmed_frameskip_type=disabled\n"));
    run(); assert(!clip_status.failed && clip_status.frame==121 && clip_status.checks==3 && mock_polls==121);
    unsigned char expected[4096]; memcpy(expected,mock_ram,sizeof(expected));
    clip_close(); assert(!clip_record("clip",1,60000,"SCUS12345",""));
    assert(!clip_open("clip","SCUSwrong"));
    assert(clip_open("clip","SCUS12345"));
    assert(!strcmp(clip_option("pcsx_rearmed_psxclock"),"57"));
    assert(!clip_option("pcsx_rearmed_frameskip_type"));
    assert(clip_restore()); mock_poison=true; mock_polls=0;
    run(); assert(!clip_status.failed && clip_status.first_divergent==0 && mock_polls==0);
    assert(!memcmp(expected,mock_ram,sizeof(expected)) && mock_readonly==1);
    assert(clip_open("clip","SCUS12345") && clip_restore());
    mock_diverge=true; run(); assert(clip_status.failed && clip_status.first_divergent==120);
    clip_close();
    FILE *f=fopen("clip.inp","r+b"); assert(f); fseek(f,100,SEEK_SET); fputc(255,f); fclose(f);
    assert(!clip_open("clip","SCUS12345"));
    clip_close();
    puts("clip codec/input/analog/checkpoint/read-only/corruption tests passed");
}
