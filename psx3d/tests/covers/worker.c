// Exercise the actual cover lifecycle and curl driver with small SDK substitutes.
#define PSX3D_H
#define PSX3D_VERSION "test"
#define PSX3D_DIR "."
#define PATH_LEN 512
#define COVER_SIZE 128
#define CUR_THREAD_HANDLE 0
#define RESET_ONESHOT 0
#define R_SUCCEEDED(x) ((x)>=0)
#define GPU_RGBA8 0
#define GPU_LINEAR 0
#define GX_BUFFER_DIM(a,b) 0
#define GX_TRANSFER_FLIP_VERT(a) 0
#define GX_TRANSFER_OUT_TILED(a) 0
#define GX_TRANSFER_RAW_COPY(a) 0
#define GX_TRANSFER_IN_FORMAT(a) 0
#define GX_TRANSFER_OUT_FORMAT(a) 0
#define GX_TRANSFER_SCALING(a) 0
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef unsigned long long u64;
typedef int32_t s32;
typedef void *Thread;
typedef int LightLock;
typedef int LightEvent;
typedef int aptHookCookie;
typedef enum { APTHOOK_ONSUSPEND, APTHOOK_ONSLEEP, APTHOOK_ONRESTORE, APTHOOK_ONWAKEUP, APTHOOK_ONEXIT } APT_HookType;
typedef struct { void *data; } C3D_Tex;
typedef struct { char cover_key[256]; } game_t;
static game_t game = { "cover" };
static bool sw_nocovers,sw_nonet;
bool dev_remote = true;
static bool blocked, g_new3ds;
bool dev_covers_blocked(void) { return blocked; }
static void (*video_pre_draw)(void);
static int cancel_after, sleeps;
u64 osGetTime(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (u64)t.tv_sec*1000+t.tv_nsec/1000000; }
static void svcSleepThread(long long ns);
static void LightLock_Init(LightLock *l) {*l=0;}
static void LightLock_Lock(LightLock *l) {assert(!*l);*l=1;}
static void LightLock_Unlock(LightLock *l) {assert(*l);*l=0;}
static int LightLock_TryLock(LightLock *l) {if(*l)return 1;*l=1;return 0;}
static void LightEvent_Init(LightEvent *e,int mode) {(void)e;(void)mode;}
static void LightEvent_Signal(LightEvent *e) {(void)e;}
static void LightEvent_WaitTimeout(LightEvent *e,long long ns) {(void)e;(void)ns;}
static int socInit(void *p,int n) {(void)p;(void)n;return 0;}
static void socExit(void) {}
static void aptHook(aptHookCookie *c,void (*f)(APT_HookType,void*),void *p) {(void)c;(void)f;(void)p;}
static void aptUnhook(aptHookCookie *c) {(void)c;}
static void svcGetThreadPriority(s32 *p,int h) {(void)h;*p=0x30;}
static Thread threadCreate(void (*f)(void*),void *p,int n,int prio,int core,bool detached) {(void)f;(void)p;(void)n;(void)detached;assert(prio==(core==2?0x34:0x30));return NULL;}
static int threadJoin(Thread t,long long ns) {(void)t;(void)ns;return 0;}
static void threadFree(Thread t) {(void)t;}
static const game_t *library_get(int i) {assert(i==0);return &game;}
static int library_count(void) {return 1;}
static void log_printf(const char *fmt,...) {(void)fmt;}
static void C3D_TexDelete(C3D_Tex *t) {free(t->data);}
static bool C3D_TexInit(C3D_Tex *t,int w,int h,int fmt) {(void)fmt;t->data=malloc(w*h*4);return !!t->data;}
static void C3D_TexSetFilter(C3D_Tex *t,int a,int b) {(void)t;(void)a;(void)b;}
#define linearAlloc malloc
#define linearFree free
static void GSPGPU_FlushDataCache(void *p,int n) {(void)p;(void)n;}
static void GX_DisplayTransfer(void *a,int b,void *c,int d,int e) {(void)a;(void)b;(void)c;(void)d;(void)e;}
CURLMcode __real_curl_multi_poll(CURLM*,struct curl_waitfd*,unsigned,int,int*);
CURLMcode __wrap_curl_multi_poll(CURLM *m,struct curl_waitfd *f,unsigned n,int ms,int *num)
{assert(ms==0);return __real_curl_multi_poll(m,f,n,ms,num);}
#include "../../source/covers.c"
static void svcSleepThread(long long ns) { if(cancel_after && ++sleeps>=cancel_after) paused=true;usleep(ns/1000); }
int main(void)
{
	covers_init();assert(paused);covers_resume();assert(!paused);covers_get(0);assert(slots[0].state==1);
	// Transient network backoff must return immediately instead of sleeping 15s.
	net_retry_at=osGetTime()+15000;bool retry=true;
	u64 start=osGetTime();assert(!load_cover(&game,&retry,false));assert(osGetTime()-start<100 && retry);
	net_retry_at=0;
	LightLock_Lock(&net_lock);
	u32 *px=download("cover",&retry);assert(px);free(px);
	px=download("missing",&retry);assert(!px && !retry && last_http==404);
	px=download("error",&retry);assert(!px && retry && last_http==503);
	cancel_after=1;sleeps=0;
	start=osGetTime();px=download("slow",&retry);assert(!px && retry && aborted_count==1);assert(osGetTime()-start<100);
	cancel_after=0;
	LightLock_Unlock(&net_lock);
	covers_stop_wait();assert(paused && !net_open && !busy);
	blocked=true;covers_resume();assert(paused);blocked=false;
	on_apt(APTHOOK_ONSUSPEND,NULL);on_apt(APTHOOK_ONSLEEP,NULL);
	on_apt(APTHOOK_ONRESTORE,NULL);on_apt(APTHOOK_ONWAKEUP,NULL);assert(paused); // gameplay stays stopped
	covers_resume();on_apt(APTHOOK_ONSUSPEND,NULL);on_apt(APTHOOK_ONSLEEP,NULL);
	on_apt(APTHOOK_ONRESTORE,NULL);assert(paused);on_apt(APTHOOK_ONWAKEUP,NULL);assert(!paused);
	assert(covers_refresh(0));assert(slots[0].refresh && slots[0].state==1);
	slots[0].pixels=malloc(COVER_SIZE*COVER_SIZE*4);slots[0].state=3;
	covers_release_gpu();assert(!slots[0].pixels && slots[0].state==1);
	assert(!covers_refresh(-1) && !covers_refresh(1));
	covers_exit();
	puts("PASS remote covers, core0 priority, backoff, HTTPS driver cancellation, HTTP retries, nested APT gameplay pause");
}
