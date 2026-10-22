// Rewind history with the real worker thread: rewind.c's own worker_main,
// encode and apply run on a host pthread. Delta allocations fail on demand.
// Every step back must land exactly on an earlier captured state, never on a
// mix of two histories (harsh review M1: 0 -> 1, failed 1 -> 2, then 3).
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "psx3d.h"
#include "statefile.h"
typedef int s32;
typedef struct { pthread_mutex_t m; pthread_cond_t c; bool set; } LightEvent;
typedef pthread_t *Thread;
#define RESET_ONESHOT 0
#define CUR_THREAD_HANDLE 0
#define U64_MAX UINT64_MAX
bool g_new3ds = true;
game_preset_options_t game_capabilities(void) { return (game_preset_options_t){ .rewind = true }; }

#define WORDS 64
static u32 game[WORDS];            // the "emulated game": its whole state
static int fail_deltas;            // next N delta allocations fail
static bool counting;              // after rewind_start: allocations are deltas
size_t retro_serialize_size(void) { return sizeof(game); }
bool retro_serialize(void *p, size_t n) { assert(n >= sizeof(game)); memcpy(p, game, sizeof(game)); return true; }
state_result_t state_last_result;
static int fail_loads;             // next N loads fail (half-restored)
bool retro_unserialize(const void *p, size_t n)
{
	assert(n >= sizeof(game));
	if (fail_loads > 0) {
		fail_loads--;
		game[0] ^= 0xdead;   // the game was partly overwritten
		state_last_result = STATE_PARTLY;
		return false;
	}
	memcpy(game, p, sizeof(game));
	state_last_result = STATE_OK;
	return true;
}
void log_printf(const char *fmt, ...) { (void)fmt; }

static void LightEvent_Init(LightEvent *e, int mode)
{
	(void)mode;
	pthread_mutex_init(&e->m, NULL);
	pthread_cond_init(&e->c, NULL);
	e->set = false;
}
static void LightEvent_Signal(LightEvent *e)
{
	pthread_mutex_lock(&e->m);
	e->set = true;
	pthread_cond_signal(&e->c);
	pthread_mutex_unlock(&e->m);
}
static void LightEvent_WaitTimeout(LightEvent *e, long long ns)
{
	struct timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	t.tv_nsec += ns % 1000000000LL;
	t.tv_sec += ns / 1000000000LL + t.tv_nsec / 1000000000LL;
	t.tv_nsec %= 1000000000LL;
	pthread_mutex_lock(&e->m);
	while (!e->set && pthread_cond_timedwait(&e->c, &e->m, &t) == 0)
		;
	e->set = false;   // one-shot
	pthread_mutex_unlock(&e->m);
}
static void svcGetThreadPriority(s32 *p, int handle) { (void)handle; *p = 0x30; }
static void svcSleepThread(long long ns) { usleep(ns / 1000); }
struct start { void (*fn)(void *); void *arg; };
static void *trampoline(void *p)
{
	struct start s = *(struct start *)p;
	free(p);
	s.fn(s.arg);
	return NULL;
}
static Thread threadCreate(void (*fn)(void *), void *arg, size_t stack, int prio, int cpu, bool detached)
{
	(void)stack; (void)prio; (void)cpu; (void)detached;
	Thread t = malloc(sizeof(*t));
	struct start *s = malloc(sizeof(*s));
	*s = (struct start){ fn, arg };
	if (pthread_create(t, NULL, trampoline, s)) { free(t); free(s); return NULL; }
	return t;
}
static void threadJoin(Thread t, u64 timeout) { (void)timeout; pthread_join(*t, NULL); }
static void threadFree(Thread t) { free(t); }
static void *test_malloc(size_t n)
{
	if (counting && fail_deltas > 0) {
		fail_deltas--;
		return NULL;
	}
	return malloc(n);
}
#define malloc test_malloc
#include "../../psx3d/source/rewind.c"
#undef malloc

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void set_game(u32 v)
{
	for (int i = 0; i < WORDS; i++)
		game[i] = i % 3 ? v * 2654435761u + i : v;   // some words same, some different
}

// one capture: INTERVAL frames, then wait for the worker to finish
static void capture(void)
{
	for (int f = 0; f < INTERVAL; f++)
		rewind_capture();
	while (__atomic_load_n(&busy, __ATOMIC_ACQUIRE))
		usleep(100);
}

static bool is_state(u32 v)
{
	u32 want[WORDS];
	memcpy(want, game, sizeof(want));
	set_game(v);
	bool same = !memcmp(want, game, sizeof(want));
	memcpy(game, want, sizeof(want));
	return same;
}

static void start(void)
{
	counting = false;
	CHECK(rewind_start());
	counting = true;
}

int main(void)
{
	// The reviewer's case: 0 -> 1, the 1 -> 2 delta can't be allocated, then 3
	start();
	set_game(0); capture();               // base 0
	set_game(1); capture();               // step 0 -> 1
	fail_deltas = 1;
	set_game(2); capture();               // 1 -> 2 lost: history must restart at 2
	set_game(3); capture();               // step 2 -> 3
	CHECK(rewind_step() && is_state(2));  // the fix: 2, not 3 ^ (0 ^ 1)
	CHECK(!rewind_step() && is_state(2)); // nothing older is valid any more
	rewind_stop();

	// Random histories with random failures, against a model of what is valid
	srand(1234);
	for (int round = 0; round < 200; round++) {
		start();
		u32 model[MAX_STEPS + 1];
		int n = 0;   // model[0..n-1]: states a step back may reach, newest last
		u32 v = round * 1000;
		set_game(v); capture();
		model[n++] = v;
		int steps = 1 + rand() % 40;
		for (int s = 0; s < steps; s++) {
			if (rand() % 6 == 0) {   // step back
				bool ok = rewind_step();
				CHECK(ok == (n > 1));
				if (n > 1) {
					n--;
					CHECK(is_state(model[n - 1]));
					v = model[n - 1];
				}
				continue;
			}
			if (rand() % 5 == 0)
				fail_deltas = 1;
			bool fails = fail_deltas > 0;
			set_game(++v); capture();
			if (fails)
				n = 0;   // history starts over at the new capture
			model[n++] = v;
		}
		while (n > 1) {   // walk all the way back
			CHECK(rewind_step());
			n--;
			CHECK(is_state(model[n - 1]));
		}
		CHECK(!rewind_step());
		fail_deltas = 0;
		if (round % 3 == 0)
			rewind_reset();   // a loaded state: the history no longer leads here
		if (round % 3 == 0)
			CHECK(!rewind_step());
		set_game(++v); capture();   // stop while the worker may be busy
		rewind_stop();
	}
	// A full ring: the oldest steps go, the rest still lead back exactly
	start();
	set_game(0); capture();
	for (u32 k = 1; k <= MAX_STEPS + 100; k++) {
		set_game(k); capture();
	}
	int back = 0;
	while (rewind_step()) {
		back++;
		CHECK(is_state(MAX_STEPS + 100 - back));
	}
	CHECK(back == MAX_STEPS);
	rewind_stop();
	// Review 3 F7c: a step that can't be loaded is reported (-1), the history
	// is dropped (nothing older leads anywhere valid), and the caller is told
	// the game was left half-restored
	start();
	set_game(0); capture();
	set_game(1); capture();
	set_game(2); capture();
	fail_loads = 1;
	CHECK(rewind_step() == -1 && state_last_result == STATE_PARTLY);
	CHECK(rewind_step() == 0);
	set_game(7); capture();
	set_game(8); capture();
	CHECK(rewind_step() == 1 && is_state(7));   // a new history from the next capture
	rewind_stop();
	if (failures) {
		fprintf(stderr, "FAIL: %d rewind worker checks\n", failures);
		return 1;
	}
	puts("PASS: rewind worker (real thread, encode/apply) never restores a state outside its history, incl. failed delta allocations, reset and stop");
	return 0;
}
