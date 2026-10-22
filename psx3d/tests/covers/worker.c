// Exercise the actual cover lifecycle and curl driver with small SDK substitutes.
#define PSX3D_H
#define PSX3D_VERSION "test"
#define PSX3D_DIR "."
#define PATH_LEN 512
#define CUR_THREAD_HANDLE 0
#define RESET_ONESHOT 0
#define R_SUCCEEDED(x) ((x)>=0)
#define GPU_RGB565 0
#define GPU_LINEAR 0
#define GPU_CLAMP_TO_EDGE 0
#define SYSCLOCK_ARM11 268111856ULL
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <curl/curl.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef unsigned long long u64;
typedef int32_t s32;
typedef void *Thread;
typedef int LightLock;
typedef int LightEvent;
typedef int aptHookCookie;
typedef enum { APTHOOK_ONSUSPEND, APTHOOK_ONSLEEP, APTHOOK_ONRESTORE, APTHOOK_ONWAKEUP, APTHOOK_ONEXIT } APT_HookType;
typedef struct { void *data; } C3D_Tex;
typedef struct { unsigned short width, height; float left, top, right, bottom; } Tex3DS_SubTexture;
typedef struct { char cover_key[256]; } game_t;
static game_t game = { "cover" };
static bool sw_nocovers,sw_nonet;
bool dev_remote = true;
// The daily preset check (presetupdate.c) has its own host test.
bool presets_update_due(void) { return false; }
void presets_update_work(int (*fetch)(const char *, const char *, size_t, long *), bool (*stop)(void)) { (void)fetch; (void)stop; }
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
static bool C3D_TexInitMipmap(C3D_Tex *t,int w,int h,int fmt) {(void)fmt;t->data=malloc(w*h*2*4/3+16);return !!t->data;}
static void C3D_TexSetFilter(C3D_Tex *t,int a,int b) {(void)t;(void)a;(void)b;}
static void C3D_TexSetFilterMipmap(C3D_Tex *t,int a) {(void)t;(void)a;}
static void C3D_TexSetWrap(C3D_Tex *t,int a,int b) {(void)t;(void)a;(void)b;}
static unsigned long long svcGetSystemTick(void) {return 0;}
static void GSPGPU_FlushDataCache(void *p,int n) {(void)p;(void)n;}
CURLMcode __real_curl_multi_poll(CURLM*,struct curl_waitfd*,unsigned,int,int*);
CURLMcode __wrap_curl_multi_poll(CURLM *m,struct curl_waitfd *f,unsigned n,int ms,int *num)
{assert(ms==0);return __real_curl_multi_poll(m,f,n,ms,num);}
#include "../../source/covers.c"
static void svcSleepThread(long long ns) { if(cancel_after && ++sleeps>=cancel_after) paused=true;usleep(ns/1000); }
int main(void)
{
	covers_init();assert(paused);covers_resume();assert(!paused);covers_get(0,NULL);assert(slots[0].state==1);
	// Transient network backoff must return immediately instead of sleeping 15s.
	net_retry_at=osGetTime()+15000;bool retry=true;
	cover_t c={0};
	u64 start=osGetTime();assert(!load_cover(&game,&retry,false,&c));assert(osGetTime()-start<100 && retry);
	net_retry_at=0;
	LightLock_Lock(&net_lock);
	int w,h;
	u32 *px=download("cover",&retry,&w,&h);assert(px && w==1 && h==1);free(px);
	px=download("missing",&retry,&w,&h);assert(!px && !retry && last_http==404);
	px=download("error",&retry,&w,&h);assert(!px && retry && last_http==503);
	cancel_after=1;sleeps=0;
	start=osGetTime();px=download("slow",&retry,&w,&h);assert(!px && retry && aborted_count==1);assert(osGetTime()-start<100);
	cancel_after=0;
	LightLock_Unlock(&net_lock);
	covers_stop_wait();assert(paused && !net_open && !busy);
	// A cover cached by an older version shows at once; the full size one replaces it.
	{
		net_retry_at=0;
		u32 *old=calloc(COVER_LEGACY_SIZE*COVER_LEGACY_SIZE,4);assert(old);
		for(int i=0;i<COVER_LEGACY_SIZE*COVER_LEGACY_SIZE;i++) old[i]=0x336699ffu;
		old[5]=0x10203000u; // transparent: flattened onto the card colour
		FILE *f=fopen("covers/cover.bin","wb");assert(f&&fwrite(old,4,COVER_LEGACY_SIZE*COVER_LEGACY_SIZE,f)==(size_t)(COVER_LEGACY_SIZE*COVER_LEGACY_SIZE));fclose(f);
		f=fopen("covers/missing.bin","wb");assert(f&&fwrite(old,4,COVER_LEGACY_SIZE*COVER_LEGACY_SIZE,f)==(size_t)(COVER_LEGACY_SIZE*COVER_LEGACY_SIZE));fclose(f);
		free(old);
		paused=false;
		retry=true;cover_t lc={0};
		assert(load_cover(&game,&retry,false,&lc) && lc.lowres && lc.w==128 && lc.h==128);
		free(lc.data);
		struct stat st;
		assert(stat("covers/cover.cv2",&st) && !stat("covers/cover.bin",&st)); // not cached yet: still the old file only
		cover_t up={0};
		assert(upgrade_cover(&game,&retry,&up) && !up.lowres && up.w==1 && up.h==1);
		free(up.data);
		assert(!stat("covers/cover.cv2",&st) && st.st_size==CV2_BYTES && stat("covers/cover.bin",&st));
		cover_t hit={0};
		assert(load_cover(&game,&retry,false,&hit) && !hit.lowres && hit.w==1 && hit.h==1);free(hit.data);
		// the database has no such picture: the old one becomes final
		strcpy(game.cover_key,"missing");
		cover_t fin={0};
		assert(upgrade_cover(&game,&retry,&fin) && !fin.lowres && fin.w==128 && fin.h==128 && !retry);
		free(fin.data);
		assert(!stat("covers/missing.cv2",&st) && st.st_size==CV2_BYTES && stat("covers/missing.bin",&st));
		strcpy(game.cover_key,"cover");
		// a short or foreign cache file is never trusted
		f=fopen("covers/cover.cv2","wb");assert(f);fwrite("PCV2 short",1,10,f);fclose(f);
		cover_t bad={0};
		assert(!read_cv2("covers/cover.cv2",&bad) && !bad.data);
		covers_stop_wait();
	}
	blocked=true;covers_resume();assert(paused);blocked=false;
	on_apt(APTHOOK_ONSUSPEND,NULL);on_apt(APTHOOK_ONSLEEP,NULL);
	on_apt(APTHOOK_ONRESTORE,NULL);on_apt(APTHOOK_ONWAKEUP,NULL);assert(paused); // gameplay stays stopped
	covers_resume();on_apt(APTHOOK_ONSUSPEND,NULL);on_apt(APTHOOK_ONSLEEP,NULL);
	on_apt(APTHOOK_ONRESTORE,NULL);assert(paused);on_apt(APTHOOK_ONWAKEUP,NULL);assert(!paused);
	assert(covers_refresh(0));assert(slots[0].refresh && slots[0].state==1);
	slots[0].pixels=malloc(COVER_PACK_BYTES);slots[0].state=3;slots[0].lowres=true;
	covers_release_gpu();assert(!slots[0].pixels && slots[0].state==1 && !slots[0].lowres);
	assert(!covers_refresh(-1) && !covers_refresh(1));
	covers_exit();
	puts("PASS remote covers, core0 priority, backoff, HTTPS driver cancellation, HTTP retries, nested APT gameplay pause");
}
