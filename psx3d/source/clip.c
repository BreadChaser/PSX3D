#include "clip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef __3DS__
#include <zlib.h>
#else
/* The ARM Linux simulator already links the core's miniz; no host zlib SDK. */
extern unsigned long mz_crc32(unsigned long crc, const unsigned char *ptr, size_t len);
#define crc32 mz_crc32
#endif

/* The core does not include cards in a save state. Store both, including the
 * second shared card, without depending on the frontend's SAVE_RAM mapping. */
extern void psx3d_clip_prepare(int replay);
extern char Mcd1Data[128 * 1024], Mcd2Data[128 * 1024];
#define CARD_BYTES (256u * 1024)
#define HEADER_BYTES (64u + CLIP_META)
#define FRAME_BYTES 24u
static unsigned char *frames;
static unsigned char *cards;
static char metadata[CLIP_META], base_path[1024];
static unsigned state_crc, fps_milli;
static bool playing;
static retro_input_poll_t live_poll;
static retro_input_state_t live_input;
static uint16_t pads[2];
static int16_t axes[2][4];
int clip_active;
bool clip_enabled, clip_replay_session;
int clip_seconds = 15, clip_requested = -1;
clip_result clip_status;

static unsigned get32(const unsigned char *p)
{ return p[0] | (unsigned)p[1]<<8 | (unsigned)p[2]<<16 | (unsigned)p[3]<<24; }
static void put32(unsigned char *p, unsigned n)
{ for (int i=0;i<4;i++) p[i] = n >> (8*i); }
static unsigned get16(const unsigned char *p) { return p[0] | (unsigned)p[1]<<8; }
static void put16(unsigned char *p, unsigned n) { p[0]=n; p[1]=n>>8; }
static unsigned crc(const void *p, size_t n) { return crc32(0, p, n); }
static bool fail(const char *s)
{
    snprintf(clip_status.message, sizeof(clip_status.message), "%s", s);
    clip_status.failed = true; clip_status.done = true; clip_active = 0;
    return false;
}
static FILE *open_ext(const char *ext, const char *mode)
{
    char path[1100]; snprintf(path,sizeof(path),"%s%s",base_path,ext);
    return fopen(path,mode);
}
static bool file_write(const char *ext, const void *p, size_t n)
{
    FILE *f=open_ext(ext,"wb"); if (!f) return false;
    bool ok=fwrite(p,1,n,f)==n; if (fclose(f)) ok=false; return ok;
}
static void *state_read(void)
{
    size_t n=retro_serialize_size();
    FILE *f=open_ext(".state","rb"); if (!f) return NULL;
    unsigned char *p=malloc(n);
    bool ok=p && fread(p,1,n,f)==n && fgetc(f)==EOF && crc(p,n)==state_crc;
    fclose(f); if (!ok) { free(p); return NULL; } return p;
}
void clip_set_live_input(retro_input_poll_t poll, retro_input_state_t input)
{ live_poll=poll; live_input=input; }
static int16_t input(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port>=2) return 0;
    if (device==RETRO_DEVICE_JOYPAD && index==0) {
        if (id==RETRO_DEVICE_ID_JOYPAD_MASK) return pads[port];
        return id<16 ? (pads[port]>>id)&1 : 0;
    }
    if (device==RETRO_DEVICE_ANALOG && index<2 && id<2) return axes[port][index*2+id];
    return 0;
}
static void poll(void)
{
    /* One snapshot, at the core's actual poll boundary; input query order and
     * JOYPAD_MASK vs individual-button queries cannot advance the recording. */
    if (!playing && live_poll) live_poll();
    unsigned char *p=frames + clip_status.frame*FRAME_BYTES;
    for (unsigned port=0;port<2;port++) {
        if (!playing) {
            pads[port]=0;
            for (unsigned id=0;id<16;id++)
                if (live_input && live_input(port,RETRO_DEVICE_JOYPAD,0,id)) pads[port]|=1u<<id;
            put16(p+port*10,pads[port]);
            for (unsigned a=0;a<4;a++) {
                axes[port][a]=live_input ? live_input(port,RETRO_DEVICE_ANALOG,a/2,a%2) : 0;
                put16(p+port*10+2+a*2,axes[port][a]);
            }
        } else {
            pads[port]=get16(p+port*10);
            for (unsigned a=0;a<4;a++) axes[port][a]=(int16_t)get16(p+port*10+2+a*2);
        }
    }
}
static bool allocate(unsigned count)
{
    free(frames); frames=NULL;
    memset(&clip_status,0,sizeof(clip_status));
    if (!count || count>CLIP_MAX_FRAMES) return fail("Clip length must be 1..3600 emulated frames");
    frames=calloc(count,FRAME_BYTES);
    free(cards); cards=malloc(CARD_BYTES);
    if (!frames || !cards) return fail("Not enough memory for clip inputs");
    clip_status.frames=count; return true;
}
bool clip_record(const char *base, unsigned count, unsigned rate, const char *serial, const char *settings)
{
    clip_close();
    if (strlen(base)>=sizeof(base_path) || strlen(settings)+strlen(serial)+64>=CLIP_META)
        return fail("Clip metadata/path too long");
    snprintf(base_path,sizeof(base_path),"%s",base);
    /* Caller reserves an unused basename. Never replace an existing clip. */
    FILE *old=open_ext(".inp","rb"); if (old) { fclose(old); return fail("Clip already exists"); }
    old=open_ext(".state","rb"); if (old) { fclose(old); return fail("Clip state already exists"); }
    if (!allocate(count)) return false;
    snprintf(metadata,sizeof(metadata),"serial=%s\nbuild=%s\n%s",serial,CLIP_BUILD " " __DATE__ " " __TIME__,settings);
    fps_milli=rate;
    size_t n=retro_serialize_size();
    void *state=calloc(1,n);
    if (!state) return fail("Not enough memory for clip state");
    psx3d_clip_prepare(0);
    bool ok=retro_serialize(state,n);
    state_crc=crc(state,n);
    if (ok) ok=file_write(".state",state,n);
    /* Record from the same restore boundary as replay: core LoadState resets
     * pipeline/SIO details and dynarec caches. A save alone is not equivalent. */
    if (ok) ok=retro_unserialize(state,n);
    free(state);
    if (!ok) return fail("Could not save/restore clip state");
    memcpy(cards,Mcd1Data,CARD_BYTES/2); memcpy(cards+CARD_BYTES/2,Mcd2Data,CARD_BYTES/2);
    playing=false; clip_active=1;
    snprintf(clip_status.message,sizeof(clip_status.message),"Recording test clip");
    return true;
}
static bool save_inputs(void)
{
    unsigned char h[HEADER_BYTES]; memset(h,0,sizeof(h));
    memcpy(h,"PSX3DINP",8); put32(h+8,1); put32(h+12,HEADER_BYTES);
    put32(h+16,FRAME_BYTES); put32(h+20,clip_status.frames); put32(h+24,fps_milli);
    put32(h+28,CLIP_CHECK_INTERVAL); put32(h+32,retro_serialize_size()); put32(h+36,state_crc);
    put32(h+40,CARD_BYTES); put32(h+44,crc(cards,CARD_BYTES));
    put32(h+48,crc(frames,clip_status.frames*FRAME_BYTES));
    memcpy(h+64,metadata,strlen(metadata)+1); put32(h+52,crc(h+64,CLIP_META));
    FILE *f=open_ext(".inp.tmp","wb"); if (!f) return false;
    bool ok=fwrite(h,1,sizeof(h),f)==sizeof(h) && fwrite(cards,1,CARD_BYTES,f)==CARD_BYTES &&
        fwrite(frames,FRAME_BYTES,clip_status.frames,f)==clip_status.frames;
    if (fclose(f)) ok=false;
    if (ok) {
        char a[1100],b[1100]; snprintf(a,sizeof(a),"%s.inp.tmp",base_path); snprintf(b,sizeof(b),"%s.inp",base_path);
        ok=rename(a,b)==0;
    }
    return ok;
}
bool clip_open(const char *base, const char *serial)
{
    clip_close();
    if (strlen(base)>=sizeof(base_path)) return fail("Clip path too long");
    snprintf(base_path,sizeof(base_path),"%s",base);
    FILE *f=open_ext(".inp","rb"); if (!f) return fail("Recording not found");
    unsigned char h[HEADER_BYTES];
    bool ok=fread(h,1,sizeof(h),f)==sizeof(h) && !memcmp(h,"PSX3DINP",8) && get32(h+8)==1 &&
        get32(h+12)==HEADER_BYTES && get32(h+16)==FRAME_BYTES && get32(h+28)==CLIP_CHECK_INTERVAL &&
        get32(h+32)==retro_serialize_size() && get32(h+40)==CARD_BYTES &&
        h[HEADER_BYTES-1]==0 && get32(h+52)==crc(h+64,CLIP_META) && get32(h+24)>=1000 && get32(h+24)<=100000;
    if (!ok || !allocate(get32(h+20))) { fclose(f); return fail("Invalid recording header"); }
    memcpy(metadata,h+64,CLIP_META);
    fps_milli=get32(h+24); state_crc=get32(h+36);
    ok=fread(cards,1,CARD_BYTES,f)==CARD_BYTES && crc(cards,CARD_BYTES)==get32(h+44) &&
        fread(frames,FRAME_BYTES,clip_status.frames,f)==clip_status.frames && fgetc(f)==EOF &&
        crc(frames,clip_status.frames*FRAME_BYTES)==get32(h+48);
    fclose(f);
    char expected[64]; snprintf(expected,sizeof(expected),"serial=%s\n",serial ? serial : "");
    if (!ok || (serial && strncmp(metadata,expected,strlen(expected)))) return fail("Corrupt clip or wrong game serial");
    playing=true; clip_status.replay=true; clip_replay_session=true;
    return true;
}
bool clip_restore(void)
{
    psx3d_clip_prepare(1);
    void *state=state_read(); if (!state) return fail("Clip state missing/corrupt");
    memcpy(Mcd1Data,cards,CARD_BYTES/2); memcpy(Mcd2Data,cards+CARD_BYTES/2,CARD_BYTES/2);
    bool ok=retro_unserialize(state,retro_serialize_size()); free(state);
    if (!ok) return fail("Clip state incompatible");
    clip_active=1; snprintf(clip_status.message,sizeof(clip_status.message),"Replaying test clip"); return true;
}
void clip_run(void)
{
    if (!clip_active || !frames || clip_status.frame>=clip_status.frames) return;
    retro_set_input_poll(poll); retro_set_input_state(input);
    retro_run();
    retro_set_input_poll(live_poll); retro_set_input_state(live_input);
    unsigned frame=++clip_status.frame;
    if (frame%CLIP_CHECK_INTERVAL==0 || frame==clip_status.frames) {
        unsigned actual=crc(retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM),retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM));
        unsigned char *p=frames+(frame-1)*FRAME_BYTES+20;
        clip_status.checks++;
        if (playing && get32(p)!=actual && !clip_status.first_divergent) clip_status.first_divergent=frame;
        if (!playing) put32(p,actual);
    }
    if (frame==clip_status.frames) {
        clip_active=0; clip_status.done=true;
        if (!playing && !save_inputs()) { fail("Clip input write failed (state retained)"); return; }
        clip_status.failed=clip_status.first_divergent!=0;
        snprintf(clip_status.message,sizeof(clip_status.message),playing ?
            (clip_status.failed ? "Replay diverged at checkpoint %u" : "Replay matched (%u checks)") : "Test clip saved (%u frames)",
            playing ? (clip_status.failed ? clip_status.first_divergent : clip_status.checks) : frame);
    }
}
void clip_close(void)
{ clip_active=0; free(frames); frames=NULL; free(cards); cards=NULL; playing=false; metadata[0]=0; clip_replay_session=false; }
void clip_reset_request(void) { clip_requested=-1; }
bool clip_switch(const char *word)
{
    if (!strcmp(word,"record") || !strcmp(word,"record=on")) { clip_enabled=true; return true; }
    if (!strcmp(word,"record=off")) { clip_enabled=false; return true; }
    const char *s=NULL; long lo=0,hi=999999;
    if (!strncmp(word,"replay=",7)) s=word+7;
    else if (!strncmp(word,"record_seconds=",15)) { s=word+15; lo=1; hi=60; }
    if (!s) return false;
    char *end; long n=strtol(s,&end,10);
    if (!*s || *end || n<lo || n>hi) return false;
    if (lo) clip_seconds=n; else clip_requested=n;
    return true;
}
bool clip_presentation_option(const char *key)
{
    return strstr(key,"frameskip") || (strstr(key,"gpu_") && !strstr(key,"gpu_slow_llists")) || strstr(key,"screen_") || strstr(key,"dithering") ||
        strstr(key,"display_") || strstr(key,"show_") || strstr(key,"crosshair") || strstr(key,"psx3d_stereo") ||
        strstr(key,"rgb32") || strstr(key,"scale_hires") || strstr(key,"3ds_appcputime");
}
const char *clip_option(const char *key)
{
    static char value[128];
    if (!playing || clip_presentation_option(key)) return NULL;
    size_t n=strlen(key);
    for (const char *p=metadata; *p;) {
        const char *end=strchr(p,'\n'); if (!end) break;
        if ((size_t)(end-p)>n && !strncmp(p,key,n) && p[n]=='=') {
            size_t count=end-p-n-1; if (count>=sizeof(value)) return NULL;
            memcpy(value,p+n+1,count); value[count]=0; return value;
        }
        p=end+1;
    }
    return NULL;
}
const char *clip_metadata(void) { return metadata; }
bool clip_archive_name(const char *name)
{
    if (strncmp(name,"recordings/",11)) return false;
    const char *p=name+11,*start=p;
    while ((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-') p++;
    if (p==start || p-start>100 || *p++!='/') return false;
    start=p; while (*p>='0'&&*p<='9') p++;
    return p>start && p-start<=6 && (!strcmp(p,".state")||!strcmp(p,".inp"));
}
