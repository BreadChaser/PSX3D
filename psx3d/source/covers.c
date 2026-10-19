// Box art. A low-priority worker thread fetches covers from the free
// libretro-thumbnails database (by No-Intro name), decodes them, shrinks them
// to 128x128 and caches that on the SD card in GPU-ready form, so each cover
// is downloaded and decoded once. A user's own PNG in covers/ wins.
// The UI asks for the covers around the selection; ready ones get uploaded
// to textures on the main thread.
#include <ctype.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include "covers_png.h"
#include "psx3d.h"
#include "devtools.h"

#define COVER_DIR PSX3D_DIR "/covers"
#ifndef THUMB_URL
#define THUMB_URL "https://thumbnails.libretro.com/Sony%20-%20PlayStation/Named_Boxarts/"
#endif
#define SLOTS 24

typedef struct {
	int game;            // library index, -1 = free
	volatile int state;  // 0 empty, 1 wanted, 2 pixels ready, 3 uploaded, 4 failed, 5 loading
	u32 *pixels;         // COVER_SIZE^2, texture byte order
	C3D_Tex tex;
	bool tex_ok;
	bool refresh;        // developer-requested HTTPS refresh; keep disk cache until success
	u32 *staging;        // linear copy the GPU reads from (kept for reuse)
	u32 last_used;
	u64 retry_at;        // transient failures retry without scrolling away
	u32 rank;            // order the UI asked for it in its latest frame (0 = selected)
} slot_t;

static slot_t slots[SLOTS];
#define WORKERS 2
static Thread workers[WORKERS];
static LightLock lock;
static LightEvent wake;
static volatile bool quit;
static u64 net_retry_at;   // after a network failure, wait before trying again
static bool apt_was_paused;
static unsigned apt_suspended;
static unsigned attempts, downloaded, cache_hits, aborted_count, failures;
static int last_curl;
static long last_http;
static unsigned stop_ms, max_stop_ms;
static volatile bool paused; // gameplay / HOME / sleep: do not start work
static volatile int busy;    // workers currently fetching/decoding a cover
static aptHookCookie apt_cookie;
void covers_stop_wait(void);

// The system tells us about HOME, sleep and closing right away (from inside
// aptMainLoop). Stop network work immediately instead of whenever the main
// loop next looks.
static void on_apt(APT_HookType hook, void *param)
{
	log_printf("covers: apt hook %d, busy %d\n", hook, busy);
	(void)param;

	switch (hook) {
	case APTHOOK_ONSUSPEND:
	case APTHOOK_ONSLEEP:
		if (!apt_suspended) apt_was_paused = paused;
		apt_suspended |= hook == APTHOOK_ONSUSPEND ? 1 : 2;
		covers_stop_wait();
		break;
	case APTHOOK_ONRESTORE:
	case APTHOOK_ONWAKEUP:
		apt_suspended &= ~(hook == APTHOOK_ONRESTORE ? 1u : 2u);
		if (apt_suspended) break;
		paused = apt_was_paused;
		net_retry_at = osGetTime() + 1000;   // the network needs a moment back
		LightEvent_Signal(&wake);
		LightEvent_Signal(&wake);
		break;
	case APTHOOK_ONEXIT:
		quit = true;                         // aborts transfers in progress
		LightEvent_Signal(&wake);
		LightEvent_Signal(&wake);
		break;
	default:
		break;
	}
}
static u32 use_clock;
static volatile u32 frame_base;   // use_clock when the UI's current frame began (covers_focus)
static u32 *soc_buf;
static bool soc_started;

// The network. Only one thread at a time may use it (net_lock): all our
// socket calls share one session with the 3DS socket service, and a thread
// blocked in poll() there holds up every other socket call, ours and, while
// suspended, the HOME Menu's. That froze the 3DS on HOME. So curl is driven
// by hand with nonblocking polls and 10ms sleeps, and every socket
// is closed (net_close) before HOME or sleep.
static LightLock net_lock;
static CURL *easy;
static CURLM *multi;
static CURLSH *share;            // DNS and TLS sessions, kept across net_close
static volatile bool net_open;   // multi holds connections

// ---------------------------------------------------------------- names

// libretro-thumbnails replaces these characters with '_' in file names
static void thumb_name(const char *key, char *out, size_t len)
{
	size_t n = 0;
	for (const char *p = key; *p && n < len - 1; p++)
		out[n++] = strchr("&*/:`<>?\\|\"", *p) ? '_' : *p;
	out[n] = 0;
}

static void url_escape(const char *in, char *out, size_t len)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t n = 0;
	for (const unsigned char *p = (const unsigned char *)in; *p && n < len - 4; p++) {
		if (isalnum(*p) || strchr("-_.~", *p)) {
			out[n++] = *p;
		} else {
			out[n++] = '%';
			out[n++] = hex[*p >> 4];
			out[n++] = hex[*p & 15];
		}
	}
	out[n] = 0;
}

static void cache_path(const char *key, const char *ext, char *out, size_t len)
{
	char name[256];
	thumb_name(key, name, sizeof(name));
	snprintf(out, len, COVER_DIR "/%s%s", name, ext);
}

// ---------------------------------------------------------------- decode

static u32 *decode_png(const void *data, size_t size, const char *file)
{
	return covers_png_decode(data, size, file, COVER_SIZE);
}

// ---------------------------------------------------------------- download

// Stage the compressed file on SD rather than retaining another 0.5 MiB in
// Old 3DS's small library heap while TLS and decoded covers are resident.
typedef struct { FILE *file; size_t len; } cover_file_t;
static size_t on_data(void *p, size_t sz, size_t n, void *ud)
{
	cover_file_t *m = ud;
	if (sz && n > (4 * 1024 * 1024 - m->len) / sz) return 0;
	size_t add = sz * n;
	size_t wrote = fwrite(p, 1, add, m->file);
	m->len += wrote;
	return wrote;
}

// close every connection (caller holds net_lock)
static void net_close(void)
{
	if (multi)
		curl_multi_cleanup(multi);
	multi = NULL;
	net_open = false;
}

// caller holds net_lock
static int transfer_progress(void *unused, curl_off_t total, curl_off_t now,
                             curl_off_t up_total, curl_off_t up_now)
{
	(void)unused; (void)total; (void)now; (void)up_total; (void)up_now;
	return quit || paused;
}

static u32 *download(const char *key, bool *retry)
{
	char name[256], esc[768], url[1024];
	thumb_name(key, name, sizeof(name));
	url_escape(name, esc, sizeof(esc));
	snprintf(url, sizeof(url), "%s%s.png", THUMB_URL, esc);

	CURL *c = easy;
	if (!c || (!multi && !(multi = curl_multi_init())))
		return NULL;
	cover_file_t m = { .file = fopen(COVER_DIR "/download.tmp", "wb") };
	if (!m.file) return NULL;
	long code = 0;
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
	curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/certs.pem");
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, transfer_progress);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "PSX3D/" PSX3D_VERSION);
	// the handle is reused for every cover: one TLS handshake (seconds on
	// the 3DS CPU) instead of one per cover
	curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(c, CURLOPT_SHARE, share);
	curl_easy_setopt(c, CURLOPT_DNS_CACHE_TIMEOUT, -1L);   // lookups can't be aborted: do them once

	CURLcode rc = CURLE_ABORTED_BY_CALLBACK;
	bool aborted = false;
	int running = 1;
	curl_multi_add_handle(multi, c);
	net_open = true;
	__atomic_add_fetch(&attempts, 1, __ATOMIC_RELAXED);
	for (;;) {
		if (quit || paused) {   // HOME, sleep or exit: retried later
			aborted = true;
			break;
		}
		if (curl_multi_perform(multi, &running) != CURLM_OK || !running)
			break;
		// Never wait inside SOC: a blocking poll monopolizes its shared IPC
		// session, including the developer server and HOME socket calls.
		curl_multi_poll(multi, NULL, 0, 0, NULL);
		svcSleepThread(10000000LL);
	}
	CURLMsg *msg;
	int left;
	while (!aborted && (msg = curl_multi_info_read(multi, &left)))
		if (msg->msg == CURLMSG_DONE)
			rc = msg->data.result;
	if (rc == CURLE_ABORTED_BY_CALLBACK && (quit || paused)) aborted = true;
	curl_multi_remove_handle(multi, c);
	if (aborted)
		net_close();
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	{
		double tc = 0, ta = 0, tt = 0, sp = 0;
		curl_easy_getinfo(c, CURLINFO_CONNECT_TIME, &tc);
		curl_easy_getinfo(c, CURLINFO_APPCONNECT_TIME, &ta);
		curl_easy_getinfo(c, CURLINFO_TOTAL_TIME, &tt);
/* Preserve the validated release layout; this supported legacy query is diagnostic only. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
		curl_easy_getinfo(c, CURLINFO_SPEED_DOWNLOAD, &sp);
#pragma GCC diagnostic pop
		log_printf("covers: http %ld %uKB connect %.2fs tls %.2fs total %.2fs (%.0fKB/s)\n",
			code, (unsigned)(m.len / 1024), tc, ta, tt, sp / 1024);
	}
	if (fclose(m.file) && rc == CURLE_OK) rc = CURLE_WRITE_ERROR;
	last_curl = rc;
	last_http = code;
	*retry = aborted || rc != CURLE_OK || code != 404;
	if (aborted) __atomic_add_fetch(&aborted_count, 1, __ATOMIC_RELAXED);
	u64 td = osGetTime();

	u32 *px = NULL;
	if (rc == CURLE_OK && code == 200) {
		px = decode_png(NULL, 0, COVER_DIR "/download.tmp");
		log_printf("covers: decode %llums\n", osGetTime() - td);
	}
	else if (rc == CURLE_COULDNT_CONNECT || rc == CURLE_COULDNT_RESOLVE_HOST ||
	         rc == CURLE_OPERATION_TIMEDOUT) {
		log_printf("covers: network error %d, retrying in 15s\n", rc);
		net_retry_at = osGetTime() + 15000;
	} else {
		log_printf("covers: %s -> %d / HTTP %ld\n", name, rc, code);
	}
	if (px) __atomic_add_fetch(&downloaded, 1, __ATOMIC_RELAXED);
	else if (!aborted) __atomic_add_fetch(&failures, 1, __ATOMIC_RELAXED);
	remove(COVER_DIR "/download.tmp");
	return px;
}

// ---------------------------------------------------------------- worker

static u32 *load_cover(const game_t *g, bool *retry, bool refresh)
{
	char path[PATH_LEN];
	u32 *px;

	// 1. our cache (already shrunk, texture byte order)
	cache_path(g->cover_key, ".bin", path, sizeof(path));
	FILE *f = refresh ? NULL : fopen(path, "rb");
	if (f) {
		px = malloc(COVER_SIZE * COVER_SIZE * 4);
		if (px && fread(px, 4, COVER_SIZE * COVER_SIZE, f) == COVER_SIZE * COVER_SIZE) {
			fclose(f);
			__atomic_add_fetch(&cache_hits, 1, __ATOMIC_RELAXED);
			return px;
		}
		free(px);
		fclose(f);
	}
	// 2. the user's own PNG, 3. download
	cache_path(g->cover_key, ".png", path, sizeof(path));
	px = refresh ? NULL : decode_png(NULL, 0, path);
	if (!px) {
		if (!quit && !sw_nonet) {
			LightLock_Lock(&net_lock);
			if (!quit && !paused && osGetTime() >= net_retry_at)
				px = download(g->cover_key, retry);
			LightLock_Unlock(&net_lock);
		}
	}
	if (px) {
		cache_path(g->cover_key, ".bin", path, sizeof(path));
		if ((f = fopen(path, "wb"))) {
			fwrite(px, 4, COVER_SIZE * COVER_SIZE, f);
			fclose(f);
		}
	}
	return px;
}

static void worker_main(void *arg)
{
	(void)arg;
	while (!quit) {
		// timed: a one-shot signal can be lost when two arrive together
		// (LightEvent_Signal does nothing on an already-signalled event),
		// so never rely on one to see quit
		LightEvent_WaitTimeout(&wake, 100000000LL);
		for (;;) {
			while (paused && !quit)
				svcSleepThread(100000000LL);
			int pick = -1, game = -1, best = 1 << 30;
			bool refresh = false;
			LightLock_Lock(&lock);
			for (int i = 0; i < SLOTS; i++) {
				if (slots[i].state != 1 || osGetTime() < slots[i].retry_at)
					continue;
				// not asked for in the last few frames: scrolled away
				if (slots[i].last_used + 40 < use_clock) {
					slots[i].state = 0;
					slots[i].game = -1;
					continue;
				}
				int d = (int)slots[i].rank;   // the UI asks for the selection first, then outward
				if (d < best) {
					best = d;
					pick = i;
					game = slots[i].game;
				}
			}
			if (pick >= 0 && !quit && !paused) {
				__atomic_add_fetch(&busy, 1, __ATOMIC_SEQ_CST);
				refresh = slots[pick].refresh;
				slots[pick].state = 5;   // ours: the other worker skips it
			} else pick = -1;
			LightLock_Unlock(&lock);
			if (pick < 0)
				break;
			const game_t *g = library_get(game);
			u64 t0 = osGetTime();
			log_printf("covers: start #%d\n", game);
			bool retry = true;
			u32 *px = g ? load_cover(g, &retry, refresh) : NULL;
			log_printf("covers: #%d %s -> %s in %llums\n", game, g ? g->cover_key : "?",
				px ? "ok" : "none", osGetTime() - t0);
			LightLock_Lock(&lock);
			if (slots[pick].game == game && slots[pick].state == 5) {
				slots[pick].pixels = px;
				if (px) slots[pick].refresh = false;
				// interrupted by HOME/sleep: try again later, not "no cover"
				slots[pick].retry_at = paused ? 0 : osGetTime() + 15000;
				slots[pick].state = px ? 2 : (retry ? 1 : 4);
			} else {
				free(px);
			}
			LightLock_Unlock(&lock);
			__atomic_sub_fetch(&busy, 1, __ATOMIC_SEQ_CST);
		}
	}
}

// ---------------------------------------------------------------- API

void covers_init(void)
{
	mkdir(COVER_DIR, 0777);
	paused = true; // library entry explicitly resumes after SOC/control init
	for (int i = 0; i < SLOTS; i++)
		slots[i].game = -1;
	LightLock_Init(&lock);
	LightEvent_Init(&wake, RESET_ONESHOT);   // each Signal wakes one worker
	// the network: 3dslink logging may already have started it
	// (3dslink logging may already have started the network)
	if (!dev_remote && !sw_nonet && !soc_buf && (soc_buf = memalign(0x1000, 0x100000)))
		soc_started = R_SUCCEEDED(socInit(soc_buf, 0x100000));
	curl_global_init(CURL_GLOBAL_DEFAULT);
	LightLock_Init(&net_lock);
	easy = curl_easy_init();
	if ((share = curl_share_init())) {
		curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
		curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
	}
	aptHook(&apt_cookie, on_apt, NULL);
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	// The New 3DS's spare core 2 when we have it: on core 0 under the UI,
	// TLS got so little CPU that covers came in at 20KB/s. (The old HOME
	// freeze blamed on core 2 was the blocking socket calls, see net_lock.)
	// On core 0 the UI may consume the full refresh interval. A lower
	// priority worker never runs then (3DS does not age thread priorities).
	// Equal priority gives TLS/decode bounded time slices on Old 3DS.
	for (int i = 0; i < WORKERS; i++) {
		int core = 2;
		workers[i] = threadCreate(worker_main, NULL, 64 * 1024, prio + 4, core, false);
		if (!workers[i])
			workers[i] = threadCreate(worker_main, NULL, 64 * 1024, prio, core = 0, false);
		log_printf("covers: worker %d on core %d\n", i, workers[i] ? core : -1);
	}

}

void covers_exit(void)
{
	video_pre_draw = NULL;
	aptUnhook(&apt_cookie);
	quit = true;
	for (int i = 0; i < WORKERS; i++) {
		if (!workers[i])
			continue;
		// workers notice quit within 100ms; give up after 3s rather than
		// hang the exit (the system ends the thread with the app anyway)
		bool done = false;
		for (int t = 0; t < 30 && !done; t++) {
			LightEvent_Signal(&wake);
			done = threadJoin(workers[i], 100000000LL) == 0;   // a timeout is not R_FAILED
		}
		log_printf("covers: worker %d %s\n", i, done ? "stopped" : "did not stop");
		if (done)
			threadFree(workers[i]);
		workers[i] = NULL;
	}
	for (int i = 0; i < SLOTS; i++) {
		free(slots[i].pixels);
		if (slots[i].tex_ok)
			C3D_TexDelete(&slots[i].tex);
		if (slots[i].staging)
			linearFree(slots[i].staging);
	}
	net_close();
	if (easy)
		curl_easy_cleanup(easy);
	if (share)
		curl_share_cleanup(share);
	easy = NULL;
	share = NULL;
	curl_global_cleanup();
	// close the network ourselves: leaving sockets for the system to tear
	// down at exit crashed the 3DS's process manager
	if (soc_started) {
		socExit();
		soc_started = false;
	}
}

// Texture for game i if it's ready; otherwise queue it and return NULL.
C3D_Tex *covers_get(int game)
{
	if (sw_nocovers)
		return NULL;
	slot_t *s = NULL;
	use_clock++;
	LightLock_Lock(&lock);
	for (int i = 0; i < SLOTS; i++)
		if (slots[i].game == game) {
			s = &slots[i];
			break;
		}
	if (!s) {
		// reuse the least recently used slot (a queued one too: the worker
		// skips slots whose game changed under it)
		for (int i = 0; i < SLOTS; i++)
			if (!s || slots[i].last_used < s->last_used)
				s = &slots[i];
		if (s) {
			free(s->pixels);
			s->pixels = NULL;
			s->game = game;
			s->state = 1;
			s->retry_at = 0;
			s->refresh = false;
			LightEvent_Signal(&wake);
		}
	}
	if (s) {
		// keep the best rank it got this frame (drawing asks again, later)
		u32 r = use_clock - frame_base;
		if (s->last_used < frame_base || r < s->rank)
			s->rank = r;
		s->last_used = use_clock;
	}
	int state = s ? s->state : 0;
	LightLock_Unlock(&lock);
	if (!s)
		return NULL;

	(void)state;
	return s->state == 3 ? &s->tex : NULL;
}

// Upload decoded covers to textures. Called inside the frame (from
// video_present, after C3D_FrameBegin): the copy is queued on the GPU
// ahead of the draw, like the game picture, and nothing waits for it.
// (A blocking transfer, inside or outside the frame, raced citro3d's own
// GPU queue and could hang the app.)
static void covers_pump_in_frame(void)
{
	int done = 0;
	for (int i = 0; i < SLOTS && done < 2; i++) {
		slot_t *s = &slots[i];
		if (s->state != 2)
			continue;
		if (!s->tex_ok)
			s->tex_ok = C3D_TexInit(&s->tex, COVER_SIZE, COVER_SIZE, GPU_RGBA8);
		if (!s->staging)
			s->staging = linearAlloc(COVER_SIZE * COVER_SIZE * 4);
		if (!s->tex_ok || !s->staging)
			return;
		LightLock_Lock(&lock);
		bool still = s->state == 2 && s->pixels;
		if (still)
			memcpy(s->staging, s->pixels, COVER_SIZE * COVER_SIZE * 4);
		LightLock_Unlock(&lock);
		if (!still)
			continue;
		GSPGPU_FlushDataCache(s->staging, COVER_SIZE * COVER_SIZE * 4);
		GX_DisplayTransfer(s->staging, GX_BUFFER_DIM(COVER_SIZE, COVER_SIZE), (u32 *)s->tex.data,
			GX_BUFFER_DIM(COVER_SIZE, COVER_SIZE),
			GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
			GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
			GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
		C3D_TexSetFilter(&s->tex, GPU_LINEAR, GPU_LINEAR);
		LightLock_Lock(&lock);
		if (s->state == 2)
			s->state = 3;
		LightLock_Unlock(&lock);
		done++;
	}
}

void covers_pump(void)
{
	if (sw_nocovers)
		return;
	video_pre_draw = covers_pump_in_frame;   // runs at the next frame start
}

// Developer-only refresh of a library cover, without deleting any SD files.
bool covers_refresh(int game)
{
	if (sw_nocovers || sw_nonet || game < 0 || game >= library_count()) return false;
	covers_get(game);
	bool ok = false;
	LightLock_Lock(&lock);
	for (int i = 0; i < SLOTS; i++) if (slots[i].game == game && slots[i].state != 5) {
		free(slots[i].pixels);
		slots[i].pixels = NULL;
		slots[i].refresh = true;
		slots[i].retry_at = 0;
		slots[i].state = 1;
		ok = true;
		break;
	}
	LightLock_Unlock(&lock);
	LightEvent_Signal(&wake);
	return ok;
}

// true while a cover is loading or a connection is open: HOME must wait
bool covers_busy(void)
{
	if (sw_nocovers)
		return false;
	return busy > 0 || net_open;
}

// Pause the workers and close the network. Doesn't block: call it each
// frame until it returns true (the network loop checks pause every 10ms).
bool covers_stop(void)
{
	if (sw_nocovers)
		return true;
	// Serialize pause with worker claims: no worker may start between the
	// busy check and returning success to a game load or APT.
	LightLock_Lock(&lock);
	paused = true;
	bool active = busy > 0;
	LightLock_Unlock(&lock);
	if (active || LightLock_TryLock(&net_lock) != 0)
		return false;
	net_close();
	LightLock_Unlock(&net_lock);
	return true;
}

// Wait for actual quiescence. Never enter gameplay/update with a worker alive.
void covers_stop_wait(void)
{
	u64 start = osGetTime();
	while (!covers_stop())
		svcSleepThread(10000000LL);
	stop_ms = osGetTime() - start;
	if (stop_ms > max_stop_ms) max_stop_ms = stop_ms;
}

void covers_resume(void)
{
	if (sw_nocovers || apt_suspended || dev_covers_blocked())
		return;
	paused = false;
	LightEvent_Signal(&wake);
	LightEvent_Signal(&wake);
}

// a new UI frame: covers_get calls from now on rank from the selection out
void covers_focus(int game)
{
	(void)game;
	if (sw_nocovers)
		return;
	frame_base = use_clock + 1;
	LightEvent_Signal(&wake);
	LightEvent_Signal(&wake);
}

// A game needs the GPU memory: drop the cover textures (about 3 MB). The
// New 3DS keeps decoded pixels for return to the library. Old 3DS needs
// that heap for emulation too; its small GPU-ready SD files reload on return.
// Call with no GPU frame using them in flight.
void covers_release_gpu(void)
{
	if (sw_nocovers)
		return;
	LightLock_Lock(&lock);
	for (int i = 0; i < SLOTS; i++) {
		slot_t *s = &slots[i];
		if (s->tex_ok)
			C3D_TexDelete(&s->tex);
		s->tex_ok = false;
		if (s->staging)
			linearFree(s->staging);
		s->staging = NULL;
		if (!g_new3ds) {
			free(s->pixels);
			s->pixels = NULL;
			s->retry_at = 0;
			s->state = s->game >= 0 ? 1 : 0;
		}
		if (s->state == 3)
			s->state = s->pixels ? 2 : 0;
	}
	LightLock_Unlock(&lock);
}

// forget everything (after a rescan the game indexes change)
void covers_reset(void)
{
	if (sw_nocovers)
		return;
	LightLock_Lock(&lock);
	for (int i = 0; i < SLOTS; i++) {
		slots[i].game = -1;
		slots[i].state = 0;
		free(slots[i].pixels);
		slots[i].pixels = NULL;
	}
	LightLock_Unlock(&lock);
}

// Token-gated developer diagnostics. No arbitrary SD reads; report only this
// subsystem's cache files and bounded slot/counter snapshots.
void covers_debug(char *out, size_t len)
{
	int ready = 0, wanted = 0, missing = 0, cached = 0, invalid = 0;
	for (int i = 0; i < library_count(); i++) {
		char path[PATH_LEN];
		struct stat st;
		cache_path(library_get(i)->cover_key, ".bin", path, sizeof(path));
		if (stat(path, &st)) missing++;
		else if (st.st_size == COVER_SIZE * COVER_SIZE * 4) cached++;
		else invalid++;
	}
	if (!sw_nocovers) {
		LightLock_Lock(&lock);
		for (int i = 0; i < SLOTS; i++) {
			ready += slots[i].state == 3;
			wanted += slots[i].state == 1;
		}
		LightLock_Unlock(&lock);
	}
	snprintf(out, len, "{\"ok\":true,\"paused\":%s,\"busy\":%d,\"net_open\":%s,"
	         "\"attempts\":%u,\"downloaded\":%u,\"cache_hits\":%u,\"aborted\":%u,"
	         "\"failures\":%u,\"curl\":%d,\"http\":%ld,\"stop_ms\":%u,\"max_stop_ms\":%u,"
	         "\"ready\":%d,\"wanted\":%d,\"cached\":%d,\"missing\":%d,\"invalid_cache\":%d}",
	         paused ? "true" : "false", busy, net_open ? "true" : "false", attempts,
	         downloaded, cache_hits, aborted_count, failures, last_curl, last_http,
	         stop_ms, max_stop_ms, ready, wanted, cached, missing, invalid);
}
