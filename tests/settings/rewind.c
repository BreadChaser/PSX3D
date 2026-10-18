// Exercise the actual engine entry point against a host-only worker substitute.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "psx3d.h"
typedef int s32;
typedef void *Thread;
typedef int LightEvent;
#define RESET_ONESHOT 0
#define CUR_THREAD_HANDLE 0
#define U64_MAX UINT64_MAX
bool g_new3ds;
static bool allow_rewind;
static unsigned serialized, allocated, workers;
game_preset_options_t game_capabilities(void)
{
 return (game_preset_options_t){.rewind=allow_rewind};
}
size_t retro_serialize_size(void) { serialized++; return 64; }
bool retro_serialize(void *p,size_t n) { (void)p; (void)n; serialized++; return true; }
bool retro_unserialize(const void *p,size_t n) { (void)p; (void)n; return true; }
void log_printf(const char *fmt,...) { (void)fmt; }
static void LightEvent_WaitTimeout(LightEvent *e,long long ns) { (void)e; (void)ns; }
static void LightEvent_Init(LightEvent *e,int mode) { (void)e; (void)mode; }
static void LightEvent_Signal(LightEvent *e) { (void)e; }
static void svcGetThreadPriority(s32 *p,int handle) { (void)p; (void)handle; }
static void svcSleepThread(long long ns) { (void)ns; }
static Thread threadCreate(void (*fn)(void*),void *arg,size_t stack,int priority,int cpu,bool detached)
{ (void)fn;(void)arg;(void)stack;(void)priority;(void)cpu;(void)detached;workers++;return (Thread)1; }
static void threadJoin(Thread t,u64 timeout) { (void)t;(void)timeout; }
static void threadFree(Thread t) { (void)t; }
static void *count_malloc(size_t n) { allocated++;return malloc(n); }
static void *count_calloc(size_t n,size_t s) { allocated++;return calloc(n,s); }
#define malloc count_malloc
#define calloc count_calloc
#include "../../psx3d/source/rewind.c"
#undef malloc
#undef calloc
int main(void)
{
 // A future preset/capability mistake still cannot start Old 3DS rewind.
 for(int enabled=0;enabled<2;enabled++) {
  g_new3ds=false;allow_rewind=enabled;
  assert(!rewind_start() && !rewind_active());
  rewind_capture();assert(!rewind_step());rewind_reset();
  assert(!serialized && !allocated && !workers);
 }
 g_new3ds=true;allow_rewind=false;
 assert(!rewind_start() && !serialized && !allocated && !workers);
 allow_rewind=true;
 assert(rewind_start() && rewind_active() && serialized==1 && allocated==3 && workers==1);
 rewind_stop();assert(!rewind_active());
 puts("PASS: production rewind engine rejects Old 3DS before serialization, allocation or worker creation; New start/stop retained");
}
