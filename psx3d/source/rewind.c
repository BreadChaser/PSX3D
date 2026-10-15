// Rewind (per game, off by default). Every few frames the game state is
// captured; the difference from the previous capture is compressed and kept
// in a history of about REWIND_MB megabytes. Stepping back applies those
// differences in reverse. The comparing and compressing run on a worker on
// the New 3DS's spare core, so the game only pays for the capture itself.
#include <stdlib.h>
#include <string.h>
#include "psx3d.h"

#define REWIND_MB  24
#define INTERVAL   10        // frames between captures (6 per second)
#define MAX_STEPS  1024

static u32 *cur, *next;      // newest state, and the capture being added
static u32 *scratch;         // worst-case compressed delta
static size_t words;         // state size in words
static void *steps[MAX_STEPS];
static size_t step_bytes[MAX_STEPS];
static int first, count;     // ring of compressed deltas, oldest at first
static size_t total;
static bool have_cur, active;
static volatile bool busy, quit;
static Thread worker;
static LightEvent job;
static int frame_ctr;

// delta = a XOR b as runs: [zero words][literal words][literals...]
static size_t encode(const u32 *a, const u32 *b, u32 *out)
{
	size_t i = 0, o = 0;
	while (i < words) {
		u32 z = 0, l = 0;
		while (i + z < words && a[i + z] == b[i + z])
			z++;
		size_t s = i + z;
		while (s + l < words && a[s + l] != b[s + l])
			l++;
		out[o++] = z;
		out[o++] = l;
		for (u32 k = 0; k < l; k++)
			out[o++] = a[s + k] ^ b[s + k];
		i = s + l;
	}
	return o * 4;
}

static void apply(u32 *state, const u32 *d, size_t bytes)
{
	size_t n = bytes / 4, o = 0, i = 0;
	while (o < n) {
		u32 z = d[o++], l = d[o++];
		i += z;
		for (u32 k = 0; k < l; k++)
			state[i++] ^= d[o++];
	}
}

static void drop_oldest(void)
{
	free(steps[first]);
	total -= step_bytes[first];
	first = (first + 1) % MAX_STEPS;
	count--;
}

static void worker_main(void *arg)
{
	(void)arg;
	while (!quit) {
		LightEvent_WaitTimeout(&job, 100000000LL);
		if (!busy)
			continue;
		size_t bytes = encode(cur, next, scratch);
		void *d = malloc(bytes);
		if (d) {
			memcpy(d, scratch, bytes);
			while (count && (count == MAX_STEPS || total + bytes > REWIND_MB * 1024 * 1024))
				drop_oldest();
			int at = (first + count) % MAX_STEPS;
			steps[at] = d;
			step_bytes[at] = bytes;
			count++;
			total += bytes;
		}
		u32 *t = cur; cur = next; next = t;   // the capture is the newest state now
		busy = false;
	}
}

static void free_all(void)
{
	while (count)
		drop_oldest();
	free(cur); free(next); free(scratch);
	cur = next = scratch = NULL;
}

bool rewind_start(void)
{
	rewind_stop();
	size_t size = retro_serialize_size();
	if (!size || !g_new3ds)
		return false;
	words = (size + 3) / 4;
	cur = calloc(words, 4);
	next = calloc(words, 4);
	scratch = malloc(words * 4 + words * 8 / 2 + 64);   // worst case: alternating runs
	if (!cur || !next || !scratch) {
		free_all();
		log_printf("rewind: not enough memory\n");
		return false;
	}
	LightEvent_Init(&job, RESET_ONESHOT);
	quit = busy = have_cur = false;
	first = count = 0;
	total = 0;
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	worker = threadCreate(worker_main, NULL, 16 * 1024, prio + 1, 2, false);
	if (!worker) {
		free_all();
		log_printf("rewind: no spare core\n");
		return false;
	}
	active = true;
	frame_ctr = 0;
	log_printf("rewind: on, state %u KB, history %d MB\n", (unsigned)(size / 1024), REWIND_MB);
	return true;
}

void rewind_stop(void)
{
	if (!active)
		return;
	// let a capture being compressed finish: the worker uses the buffers
	quit = true;
	LightEvent_Signal(&job);
	threadJoin(worker, U64_MAX);
	threadFree(worker);
	free_all();
	log_printf("rewind: off\n");
	active = false;
}

bool rewind_active(void) { return active; }

// after each emulated frame
void rewind_capture(void)
{
	if (!active || ++frame_ctr < INTERVAL || busy)
		return;
	frame_ctr = 0;
	if (!have_cur) {
		have_cur = retro_serialize(cur, words * 4);
		return;
	}
	if (retro_serialize(next, words * 4)) {
		busy = true;
		LightEvent_Signal(&job);
	}
}

// one step back in time; false when the history is used up
bool rewind_step(void)
{
	if (!active || !have_cur)
		return false;
	while (busy)
		svcSleepThread(1000000LL);
	if (!count)
		return false;
	int last = (first + count - 1) % MAX_STEPS;
	apply(cur, steps[last], step_bytes[last]);
	free(steps[last]);
	total -= step_bytes[last];
	count--;
	frame_ctr = 0;
	return retro_unserialize(cur, words * 4);
}

// a state was loaded or the game reset: the history no longer leads here
void rewind_reset(void)
{
	if (!active)
		return;
	while (busy)
		svcSleepThread(1000000LL);
	while (count)
		drop_oldest();
	have_cur = false;
}
