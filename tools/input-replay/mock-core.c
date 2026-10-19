/* Host-only core for testing the actual clip codec, callbacks and failure paths. */
#include "clip.h"
#include <string.h>
#include <zlib.h>
unsigned long mz_crc32(unsigned long c,const unsigned char *p,size_t n) { return crc32(c,p,n); }
char Mcd1Data[128*1024], Mcd2Data[128*1024];
unsigned char mock_ram[4096];
unsigned mock_ticks, mock_polls, mock_readonly;
bool mock_poison, mock_diverge;
static retro_input_poll_t poll_cb;
static retro_input_state_t state_cb;
void psx3d_clip_prepare(int replay) { if (replay) mock_readonly++; }
size_t retro_serialize_size(void) { return sizeof(mock_ram)+sizeof(mock_ticks); }
bool retro_serialize(void *p,size_t n)
{
    if (n!=retro_serialize_size()) return false;
    memcpy(p,mock_ram,sizeof(mock_ram)); memcpy((char*)p+sizeof(mock_ram),&mock_ticks,sizeof(mock_ticks)); return true;
}
bool retro_unserialize(const void *p,size_t n)
{
    if (n!=retro_serialize_size()) return false;
    memcpy(mock_ram,p,sizeof(mock_ram)); memcpy(&mock_ticks,(const char*)p+sizeof(mock_ram),sizeof(mock_ticks)); return true;
}
void *retro_get_memory_data(unsigned id) { (void)id; return mock_ram; }
size_t retro_get_memory_size(unsigned id) { (void)id; return sizeof(mock_ram); }
void retro_set_input_poll(retro_input_poll_t p) { poll_cb=p; }
void retro_set_input_state(retro_input_state_t p) { state_cb=p; }
void mock_poll(void) { mock_polls++; }
int16_t mock_input(unsigned port,unsigned device,unsigned index,unsigned id)
{
    if (mock_poison) return -12345;
    if (device==RETRO_DEVICE_JOYPAD) return id<16 && id==(mock_ticks+port)%16;
    return (int16_t)(1000*port+100*index+10*id+mock_ticks);
}
void retro_run(void)
{
    poll_cb();
    for (unsigned p=0;p<2;p++) {
        unsigned mask=(uint16_t)state_cb(p,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK);
        unsigned a=(uint16_t)state_cb(p,RETRO_DEVICE_ANALOG,1,1);
        mock_ram[mock_ticks%4096]^=mask^a;
        for (unsigned b=0;b<16;b++)
            if (!!state_cb(p,RETRO_DEVICE_JOYPAD,0,b)!=!!((mask>>b)&1)) mock_ram[0]++;
    }
    if (mock_diverge && mock_ticks==75) mock_ram[42]++;
    mock_ticks++;
}
