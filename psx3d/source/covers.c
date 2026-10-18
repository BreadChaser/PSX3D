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
#include <png.h>
#include "psx3d.h"
#include "devtools.h"

#define COVER_DIR PSX3D_DIR "/covers"
#define THUMB_URL "https://thumbnails.libretro.com/Sony%20-%20PlayStation/Named_Boxarts/"
#define SLOTS 24

typedef struct {
	int game;            // library index, -1 = free
	volatile int state;  // 0 empty, 1 wanted, 2 pixels ready, 3 uploaded, 4 failed, 5 loading
	u32 *pixels;         // COVER_SIZE^2, texture byte order
	C3D_Tex tex;
	bool tex_ok;
	u32 *staging;        // linear copy the GPU reads from (kept for reuse)
	u32 last_used;
	u32 rank;            // order the UI asked for it in its latest frame (0 = selected)
} slot_t;

static slot_t slots[SLOTS];
#define WORKERS 2
static Thread workers[WORKERS];
static LightLock lock;
static LightEvent wake;
static volatile bool quit;
static u64 net_retry_at;   // after a network failure, wait before trying again
static bool remote_covers;
static volatile bool paused; // HOME menu / sleep: don't start new downloads
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
		covers_stop_wait();
		break;
	case APTHOOK_ONRESTORE:
	case APTHOOK_ONWAKEUP:
		paused = dev_remote && !remote_covers;
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
// by hand with short polls, aborts within ~50ms of a pause, and every socket
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

static u32 *shrink(const u8 *rgba, int w, int h)
{
	u32 *out = malloc(COVER_SIZE * COVER_SIZE * 4);
	if (!out)
		return NULL;
	// box filter into COVER_SIZE x COVER_SIZE, stretched to fill
	for (int y = 0; y < COVER_SIZE; y++) {
		int y0 = y * h / COVER_SIZE, y1 = (y + 1) * h / COVER_SIZE;
		if (y1 <= y0) y1 = y0 + 1;
		for (int x = 0; x < COVER_SIZE; x++) {
			int x0 = x * w / COVER_SIZE, x1 = (x + 1) * w / COVER_SIZE;
			if (x1 <= x0) x1 = x0 + 1;
			u32 r = 0, g = 0, b = 0, a = 0, n = 0;
			for (int yy = y0; yy < y1; yy++)
				for (int xx = x0; xx < x1; xx++) {
					const u8 *p = rgba + (yy * w + xx) * 4;
					r += p[0]; g += p[1]; b += p[2]; a += p[3];
					n++;
				}
			// GPU RGBA8: bytes A,B,G,R
			out[y * COVER_SIZE + x] = ((r / n) << 24) | ((g / n) << 16) | ((b / n) << 8) | (a / n);
		}
	}
	return out;
}

static u32 *decode_png(const void *data, size_t size, const char *file)
{
	png_image img;
	memset(&img, 0, sizeof(img));
	img.version = PNG_IMAGE_VERSION;
	int ok = file ? png_image_begin_read_from_file(&img, file)
		: png_image_begin_read_from_memory(&img, data, size);
	if (!ok)
		return NULL;
	img.format = PNG_FORMAT_RGBA;
	u8 *rgba = malloc(PNG_IMAGE_SIZE(img));
	if (!rgba) {
		png_image_free(&img);
		return NULL;
	}
	u32 *out = NULL;
	if (png_image_finish_read(&img, NULL, rgba, 0, NULL))
		out = shrink(rgba, img.width, img.height);
	free(rgba);
	return out;
}

// ---------------------------------------------------------------- download

typedef struct { u8 *data; size_t len, cap; } membuf_t;

static size_t on_data(void *p, size_t sz, size_t n, void *ud)
{
	membuf_t *m = ud;
	size_t add = sz * n;
	if (m->len + add > 4 * 1024 * 1024)
		return 0;   // not a cover
	if (m->len + add > m->cap) {
		size_t cap = m->cap ? m->cap * 2 : 256 * 1024;
		while (cap < m->len + add) cap *= 2;
		u8 *d = realloc(m->data, cap);
		if (!d)
			return 0;
		m->data = d;
		m->cap = cap;
	}
	memcpy(m->data + m->len, p, add);
	m->len += add;
	return add;
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
static u32 *download(const char *key)
{
	char name[256], esc[768], url[1024];
	thumb_name(key, name, sizeof(name));
	url_escape(name, esc, sizeof(esc));
	snprintf(url, sizeof(url), "%s%s.png", THUMB_URL, esc);

	CURL *c = easy;
	if (!c || (!multi && !(multi = curl_multi_init())))
		return NULL;
	membuf_t m = { 0 };
	long code = 0;
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
	curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/certs.pem");
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 6L);
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
	for (;;) {
		if (curl_multi_perform(multi, &running) != CURLM_OK || !running)
			break;
		if (quit || paused) {   // HOME, sleep or exit: retried later
			aborted = true;
			break;
		}
		curl_multi_poll(multi, NULL, 0, 50, NULL);
	}
	CURLMsg *msg;
	int left;
	while (!aborted && (msg = curl_multi_info_read(multi, &left)))
		if (msg->msg == CURLMSG_DONE)
			rc = msg->data.result;
	curl_multi_remove_handle(multi, c);
	if (aborted)
		net_close();
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	{
		double tc = 0, ta = 0, tt = 0, sp = 0;
		curl_easy_getinfo(c, CURLINFO_CONNECT_TIME, &tc);
		curl_easy_getinfo(c, CURLINFO_APPCONNECT_TIME, &ta);
		curl_easy_getinfo(c, CURLINFO_TOTAL_TIME, &tt);
		curl_easy_getinfo(c, CURLINFO_SPEED_DOWNLOAD, &sp);
		log_printf("covers: http %ld %uKB connect %.2fs tls %.2fs total %.2fs (%.0fKB/s)\n",
			code, (unsigned)(m.len / 1024), tc, ta, tt, sp / 1024);
	}
	u64 td = osGetTime();

	u32 *px = NULL;
	if (rc == CURLE_OK && code == 200) {
		px = decode_png(m.data, m.len, NULL);
		log_printf("covers: decode %llums\n", osGetTime() - td);
	}
	else if (rc == CURLE_COULDNT_CONNECT || rc == CURLE_COULDNT_RESOLVE_HOST ||
	         rc == CURLE_OPERATION_TIMEDOUT) {
		log_printf("covers: network error %d, retrying in 15s\n", rc);
		net_retry_at = osGetTime() + 15000;
	} else {
		log_printf("covers: %s -> %d / HTTP %ld\n", name, rc, code);
	}
	free(m.data);
	return px;
}

// ---------------------------------------------------------------- worker

static u32 *load_cover(const game_t *g)
{
	char path[PATH_LEN];
	u32 *px;

	// 1. our cache (already shrunk, texture byte order)
	cache_path(g->cover_key, ".bin", path, sizeof(path));
	FILE *f = fopen(path, "rb");
	if (f) {
		px = malloc(COVER_SIZE * COVER_SIZE * 4);
		if (px && fread(px, 4, COVER_SIZE * COVER_SIZE, f) == COVER_SIZE * COVER_SIZE) {
			fclose(f);
			return px;
		}
		free(px);
		fclose(f);
	}
	// 2. the user's own PNG, 3. download
	cache_path(g->cover_key, ".png", path, sizeof(path));
	px = decode_png(NULL, 0, path);
	if (!px) {
		while (!quit && osGetTime() < net_retry_at)
			svcSleepThread(200000000LL);
		if (!quit && !sw_nonet) {
			LightLock_Lock(&net_lock);
			if (!quit && !paused)
				px = download(g->cover_key);
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
			LightLock_Lock(&lock);
			for (int i = 0; i < SLOTS; i++) {
				if (slots[i].state != 1)
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
			if (pick >= 0)
				slots[pick].state = 5;   // ours: the other worker skips it
			LightLock_Unlock(&lock);
			if (pick < 0 || quit)
				break;
			const game_t *g = library_get(game);
			u64 t0 = osGetTime();
			log_printf("covers: start #%d\n", game);
			__atomic_add_fetch(&busy, 1, __ATOMIC_SEQ_CST);
			u32 *px = g ? load_cover(g) : NULL;
			__atomic_sub_fetch(&busy, 1, __ATOMIC_SEQ_CST);
			log_printf("covers: #%d %s -> %s in %llums\n", game, g ? g->cover_key : "?",
				px ? "ok" : "none", osGetTime() - t0);
			LightLock_Lock(&lock);
			if (slots[pick].game == game && slots[pick].state == 5) {
				slots[pick].pixels = px;
				// interrupted by HOME/sleep: try again later, not "no cover"
				slots[pick].state = px ? 2 : (paused ? 1 : 4);
			} else {
				free(px);
			}
			LightLock_Unlock(&lock);
		}
	}
}

// ---------------------------------------------------------------- API

void covers_init(void)
{
	mkdir(COVER_DIR, 0777);
	paused = dev_remote;
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
	if (sw_nocovers || (dev_remote && !remote_covers))
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

// true while a cover is loading or a connection is open: HOME must wait
bool covers_busy(void)
{
	if (sw_nocovers)
		return false;
	return busy > 0 || net_open;
}

// Pause the workers and close the network. Doesn't block: call it each
// frame until it returns true (a transfer aborts within ~50ms).
bool covers_stop(void)
{
	if (sw_nocovers)
		return true;
	paused = true;
	if (busy > 0 || LightLock_TryLock(&net_lock) != 0)
		return false;
	net_close();
	LightLock_Unlock(&net_lock);
	return true;
}

// covers_stop, waiting (up to 3s) until it's done
void covers_stop_wait(void)
{
	for (int i = 0; i < 300 && !covers_stop(); i++)
		svcSleepThread(10000000LL);
}

void covers_resume(void)
{
	if (sw_nocovers || (dev_remote && !remote_covers))
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
// decoded pixels stay, so back in the library they are uploaded again.
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

// Explicit developer UI capture opt-in. Keep the control server's SOC
// session; the curl worker uses short polls and closes before game/update.
void covers_remote_enable(bool enable)
{
	if (!dev_remote || sw_nocovers) return;
	remote_covers = enable;
	if (enable) covers_resume();
	else covers_stop_wait();
}
