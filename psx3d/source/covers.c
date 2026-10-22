// Box art. A low-priority worker thread fetches covers from the free
// libretro-thumbnails database, decodes them, shrinks them
// to fit 256x256 (keeping their aspect) and caches that on the SD card as a
// finished GPU texture (RGB565, tiled; see covers_pack.c),
// so each cover is downloaded and decoded once and the UI only copies it.
// The database names pictures after redump names, but a game's file can be
// called anything: boxart.c finds the picture by file name, disc serial or
// title (see boxart.h). A picture the database does not have is remembered
// (covers/state.txt), so it is not asked for at every start.
// A player's own PNG, covers/<file name without extension>.png, wins over
// every download. Covers cached by older versions (128x128 RGBA8 .bin) show at
// once and are replaced by a full size download in the background.
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
#include "covers_pack.h"
#include "boxart.h"
#include "safefile.h"
#include "psx3d.h"
#include "devtools.h"
#include "presetupdate.h"

#define COVER_DIR PSX3D_DIR "/covers"
#define COVER_STATE COVER_DIR "/state.txt"
#ifndef BOXART_DIR
#define BOXART_DIR "romfs:/covers"   // names.txt, serials.idx (tools/boxart/make_table.py)
#endif
#ifndef THUMB_URL
#define THUMB_URL "https://thumbnails.libretro.com/Sony%20-%20PlayStation/Named_Boxarts/"
#endif
#define SLOTS 24

// cache file: a header, then covers_pack's COVER_PACK_BYTES
#define CV2_MAGIC 0x32564350u   // "PCV2"
#define CV2_BYTES (16 + COVER_PACK_BYTES)
typedef struct { u32 magic; u16 w, h; u32 levels, bytes; } cv2_head_t;

typedef struct { u8 *data; u16 w, h; bool lowres; } cover_t;

typedef struct {
	int game;            // library index, -1 = free
	volatile int state;  // 0 empty, 1 wanted, 2 pixels ready, 3 uploaded, 4 failed, 5 loading
	u8 *pixels;          // COVER_PACK_BYTES until uploaded (then freed)
	u16 w, h;            // the picture's size inside the texture
	C3D_Tex tex;
	bool tex_ok;
	bool shown;          // the texture holds this game's cover (it can be drawn)
	bool lowres;         // it is the old 128x128 cover: fetch the full size one
	bool upgrading;      // a worker is on it (state stays 3: the old picture stays up)
	bool refresh;        // developer-requested HTTPS refresh; keep disk cache until success
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
static u64 net_backoff;    // that wait: 15 s, doubling to 10 min, 0 after a success
static LightLock state_lock;   // boxart.c's memory of misses and own pictures
static bool apt_was_paused;
static unsigned apt_suspended;
static unsigned attempts, downloaded, cache_hits, aborted_count, failures, upgraded;
static unsigned miss_skips, other_names;   // not asked for (remembered miss); fetched under another name
static unsigned uploads, upload_max_us, upload_total_us;   // main thread: texture copies
// library frame intervals (a developer diagnostic: is the carousel smooth?)
static u64 frame_last;
static unsigned frames, frames_over_25ms, frames_over_40ms, frame_max_us;
static u64 frame_total_us;
static int last_curl;
static long last_http;
static unsigned stop_ms, max_stop_ms;
static volatile bool paused; // gameplay / HOME / sleep: do not start work
static volatile int busy;    // workers currently fetching/decoding a cover or presets
static bool presets_claimed; // a worker runs the preset check (under lock)
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

static u32 *decode_png(const void *data, size_t size, const char *file, int *w, int *h)
{
	return covers_png_decode(data, size, file, COVER_TEX, w, h);
}

// RGBA8 picture -> a finished cover (consumes nothing; the caller frees px)
static bool pack_cover(const u32 *px, int w, int h, cover_t *c)
{
	c->data = malloc(COVER_PACK_BYTES);
	if (!c->data)
		return false;
	covers_pack(px, w, h, c->data);
	c->w = w;
	c->h = h;
	c->lowres = false;
	return true;
}

static bool read_cv2(const char *path, cover_t *c)
{
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	cv2_head_t head;
	bool ok = fread(&head, sizeof(head), 1, f) == 1 && head.magic == CV2_MAGIC && head.w >= 1 &&
	          head.w <= COVER_TEX && head.h >= 1 && head.h <= COVER_TEX && head.levels == COVER_LEVELS &&
	          head.bytes == COVER_PACK_BYTES;
	c->data = ok ? malloc(COVER_PACK_BYTES) : NULL;
	ok = c->data && fread(c->data, 1, COVER_PACK_BYTES, f) == COVER_PACK_BYTES;
	fclose(f);
	if (!ok) {
		free(c->data);
		c->data = NULL;
		return false;
	}
	c->w = head.w;
	c->h = head.h;
	c->lowres = false;
	return true;
}

static void write_cv2(const char *path, const cover_t *c)
{
	FILE *f = fopen(path, "wb");
	if (!f) return;
	cv2_head_t head = { CV2_MAGIC, c->w, c->h, COVER_LEVELS, COVER_PACK_BYTES };
	bool ok = fwrite(&head, sizeof(head), 1, f) == 1 && fwrite(c->data, 1, COVER_PACK_BYTES, f) == COVER_PACK_BYTES;
	if (fclose(f) || !ok)
		remove(path);   // never leave a short file that looks like a cache
}

// The 128x128 RGBA8 cache of versions before this one (.bin, 64 KiB).
static bool read_legacy(const char *path, cover_t *c)
{
	const int n = COVER_LEGACY_SIZE * COVER_LEGACY_SIZE;
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	u32 *px = malloc(n * 4);
	bool ok = px && fread(px, 4, n, f) == (size_t)n;
	fclose(f);
	if (ok) {
		for (int i = 0; i < n; i++) {   // flatten like the decoder does
			u32 a = px[i] & 255;
			if (a == 255) continue;
			u32 r = ((px[i] >> 24) * a + COVER_BG_R * (255 - a) + 127) / 255;
			u32 g = ((px[i] >> 16 & 255) * a + COVER_BG_G * (255 - a) + 127) / 255;
			u32 b = ((px[i] >> 8 & 255) * a + COVER_BG_B * (255 - a) + 127) / 255;
			px[i] = r << 24 | g << 16 | b << 8 | 255;
		}
		ok = pack_cover(px, COVER_LEGACY_SIZE, COVER_LEGACY_SIZE, c);
	}
	free(px);
	return ok;
}

// ---------------------------------------------------------------- download

// Stage the compressed file on SD rather than retaining another 0.5 MiB in
// Old 3DS's small library heap while TLS and decoded covers are resident.
typedef struct { FILE *file; size_t len, limit; } cover_file_t;
static size_t on_data(void *p, size_t sz, size_t n, void *ud)
{
	cover_file_t *m = ud;
	if (sz && n > (m->limit - m->len) / sz) return 0;
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

// One transfer into a file on the SD card, at most limit bytes (caller holds
// net_lock). *aborted: HOME, sleep, exit or a game start stopped it.
static CURLcode fetch_file(const char *url, const char *path, size_t limit, long *code, bool *aborted)
{
	CURL *c = easy;
	*code = 0;
	*aborted = false;
	if (!c || (!multi && !(multi = curl_multi_init())))
		return CURLE_FAILED_INIT;
	cover_file_t m = { .file = fopen(path, "wb"), .limit = limit };
	if (!m.file) return CURLE_WRITE_ERROR;
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
	int running = 1;
	curl_multi_add_handle(multi, c);
	net_open = true;
	__atomic_add_fetch(&attempts, 1, __ATOMIC_RELAXED);
	for (;;) {
		if (quit || paused) {   // HOME, sleep or exit: retried later
			*aborted = true;
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
	while (!*aborted && (msg = curl_multi_info_read(multi, &left)))
		if (msg->msg == CURLMSG_DONE)
			rc = msg->data.result;
	if (rc == CURLE_ABORTED_BY_CALLBACK && (quit || paused)) *aborted = true;
	curl_multi_remove_handle(multi, c);
	if (*aborted)
		net_close();
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, code);
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
			*code, (unsigned)(m.len / 1024), tc, ta, tt, sp / 1024);
	}
	if (fclose(m.file) && rc == CURLE_OK) rc = CURLE_WRITE_ERROR;
	last_curl = rc;
	last_http = *code;
	if (*aborted) __atomic_add_fetch(&aborted_count, 1, __ATOMIC_RELAXED);
	return rc;
}

static bool network_error(CURLcode rc)
{
	return rc == CURLE_COULDNT_CONNECT || rc == CURLE_COULDNT_RESOLVE_HOST || rc == CURLE_OPERATION_TIMEDOUT;
}

// no Wi-Fi, no server: every try waits twice as long as the one before
static void net_failed(void)
{
	net_backoff = net_backoff ? net_backoff * 2 : 15000;
	if (net_backoff > 600000) net_backoff = 600000;
	net_retry_at = osGetTime() + net_backoff;
}

static u32 *download(const char *key, bool *retry, int *pw, int *ph)
{
	char name[256], esc[768], url[1024];
	thumb_name(key, name, sizeof(name));
	url_escape(name, esc, sizeof(esc));
	snprintf(url, sizeof(url), "%s%s.png", THUMB_URL, esc);

	long code;
	bool aborted;
	CURLcode rc = fetch_file(url, COVER_DIR "/download.tmp", 4 * 1024 * 1024, &code, &aborted);
	*retry = aborted || rc != CURLE_OK || code != 404;
	u64 td = osGetTime();

	u32 *px = NULL;
	if (rc == CURLE_OK && code == 200) {
		px = decode_png(NULL, 0, COVER_DIR "/download.tmp", pw, ph);
		log_printf("covers: decode %llums\n", osGetTime() - td);
	}
	else if (network_error(rc)) {
		net_failed();
		log_printf("covers: network error %d, retrying in %llus\n", rc, net_backoff / 1000);
	} else {
		log_printf("covers: %s -> %d / HTTP %ld\n", name, rc, code);
		if (!aborted && code != 404)
			net_failed();   // server trouble or a broken picture: not again at once
	}
	if (px) {
		net_backoff = 0;
		__atomic_add_fetch(&downloaded, 1, __ATOMIC_RELAXED);
	} else if (!aborted) __atomic_add_fetch(&failures, 1, __ATOMIC_RELAXED);
	remove(COVER_DIR "/download.tmp");
	return px;
}

// Preset updates (presetupdate.c) use the same connection, lock and pauses.
static int presets_fetch(const char *url, const char *path, size_t limit, long *http)
{
	bool aborted;
	CURLcode rc = fetch_file(url, path, limit, http, &aborted);
	if (aborted) return PU_FETCH_ABORT;
	if (network_error(rc)) {
		net_failed();
		return PU_FETCH_NET;
	}
	return rc == CURLE_OK ? PU_FETCH_OK : PU_FETCH_FAIL;
}
static bool presets_stop(void) { return quit || paused; }

// ---------------------------------------------------------------- worker

static void state_flush(void)
{
	LightLock_Lock(&state_lock);
	boxart_misses_save(COVER_STATE, PSX3D_VERSION);
	LightLock_Unlock(&state_lock);
}

// A cover that has to come from the network (nothing cached): a finished
// cover, or false. *retry false: the database has no such picture. A picture
// it did not have lately is not asked for again (boxart_miss_*).
static bool fetch_cover(const game_t *g, bool *retry, cover_t *c)
{
	if (quit || sw_nonet)
		return false;
	LightLock_Lock(&state_lock);
	bool missed = boxart_miss_known(g->cover_key, osGetTime());
	LightLock_Unlock(&state_lock);
	if (missed) {
		__atomic_add_fetch(&miss_skips, 1, __ATOMIC_RELAXED);
		*retry = false;
		return false;
	}
	// the picture named like the file, else the disc's serial, else the same title
	char own[BOXART_NAME], serial[10] = "", cand[BOXART_MAX_CANDIDATES][BOXART_NAME];
	boxart_sanitize(g->cover_key, own, sizeof(own));
	if (!boxart_name_known(BOXART_DIR "/names.txt", own) && g->ndiscs > 0)
		boxart_disc_serial(g->discs[0], serial);
	int n = boxart_candidates(BOXART_DIR "/names.txt", BOXART_DIR "/serials.idx", g->cover_key, serial,
	                          cand, BOXART_MAX_CANDIDATES);
	bool ok = false, all_404 = n > 0;
	LightLock_Lock(&net_lock);
	for (int i = 0; i < n && all_404 && !ok; i++) {
		if (quit || paused || osGetTime() < net_retry_at) {
			all_404 = false;
			break;
		}
		int w = 0, h = 0;
		bool r = true;
		u32 *px = download(cand[i], &r, &w, &h);
		ok = px && pack_cover(px, w, h, c);
		free(px);
		if (ok && strcmp(cand[i], own))
			__atomic_add_fetch(&other_names, 1, __ATOMIC_RELAXED);
		if (ok || r)
			all_404 = false;   // got it, or no word from the database: not "it has none"
	}
	LightLock_Unlock(&net_lock);
	if (!ok && all_404) {
		*retry = false;
		LightLock_Lock(&state_lock);
		boxart_miss_add(g->cover_key, osGetTime());
		LightLock_Unlock(&state_lock);
		state_flush();
		log_printf("covers: %s: the database has none (%d names tried), asking again in 14 days\n", g->cover_key, n);
	}
	return ok;
}

// The cover cache holds the picture for this game's file name; a PNG of the
// player's own beats it. Marks the cache with the PNG's size and time, so a
// changed PNG is picked up and a removed one gives the game back to the download.
static bool load_user_cover(const char *key, const char *own, const char *path, cover_t *c)
{
	struct stat ps, cs;
	long size = 0, mtime = 0;
	bool png = !stat(own, &ps) && S_ISREG(ps.st_mode);
	bool marked = boxart_user_get(key, &size, &mtime);
	if (!png) {
		if (marked) {   // the PNG is gone: the cover made from it goes too
			remove(path);
			LightLock_Lock(&state_lock);
			boxart_user_clear(key);
			LightLock_Unlock(&state_lock);
			state_flush();
		}
		return false;
	}
	if (marked && size == (long)ps.st_size && mtime == (long)ps.st_mtime && !stat(path, &cs) && read_cv2(path, c)) {
		__atomic_add_fetch(&cache_hits, 1, __ATOMIC_RELAXED);
		return true;
	}
	int w, h;
	u32 *px = decode_png(NULL, 0, own, &w, &h);
	if (!px) {
		log_printf("covers: %s: own picture cannot be read, using the usual cover\n", own);
		return false;
	}
	bool ok = pack_cover(px, w, h, c);
	free(px);
	if (ok) {
		write_cv2(path, c);
		LightLock_Lock(&state_lock);
		boxart_user_set(key, (long)ps.st_size, (long)ps.st_mtime);
		LightLock_Unlock(&state_lock);
		state_flush();
	}
	return ok;
}

static bool load_cover(const game_t *g, bool *retry, bool refresh, cover_t *c)
{
	char path[PATH_LEN], legacy[PATH_LEN], own[PATH_LEN];
	cache_path(g->cover_key, ".cv2", path, sizeof(path));
	cache_path(g->cover_key, ".png", own, sizeof(own));

	// 1. the player's own PNG, then our cache (a finished texture)
	if (!refresh && load_user_cover(g->cover_key, own, path, c))
		return true;
	if (!refresh && read_cv2(path, c)) {
		__atomic_add_fetch(&cache_hits, 1, __ATOMIC_RELAXED);
		return true;
	}
	// 2. the old small cache: show it now, the full size one comes later
	cache_path(g->cover_key, ".bin", legacy, sizeof(legacy));
	if (!refresh && read_legacy(legacy, c)) {
		c->lowres = !sw_nonet;
		__atomic_add_fetch(&cache_hits, 1, __ATOMIC_RELAXED);
		return true;
	}
	// 3. download
	if (!fetch_cover(g, retry, c))
		return false;
	write_cv2(path, c);
	remove(legacy);
	LightLock_Lock(&state_lock);
	boxart_user_clear(g->cover_key);
	LightLock_Unlock(&state_lock);
	state_flush();
	return true;
}

// Replace an old small cover with the full size one. A picture the database
// does not have (404) makes the small one final.
static bool upgrade_cover(const game_t *g, bool *retry, cover_t *c)
{
	char path[PATH_LEN], legacy[PATH_LEN];
	cache_path(g->cover_key, ".cv2", path, sizeof(path));
	cache_path(g->cover_key, ".bin", legacy, sizeof(legacy));
	*retry = true;
	bool ok = fetch_cover(g, retry, c);
	if (!ok && !*retry)
		ok = read_legacy(legacy, c);
	if (ok) {
		write_cv2(path, c);
		remove(legacy);
		__atomic_add_fetch(&upgraded, 1, __ATOMIC_RELAXED);
	}
	return ok;
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
			bool refresh = false, upgrade = false;
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
			// Nothing new to load: swap a shown old small cover for the full size one.
			if (pick < 0 && !quit && !paused && !sw_nonet) {
				best = 1 << 30;
				for (int i = 0; i < SLOTS; i++) {
					if (slots[i].state != 3 || !slots[i].lowres || slots[i].upgrading ||
					    osGetTime() < slots[i].retry_at || slots[i].last_used + 40 < use_clock)
						continue;
					if ((int)slots[i].rank < best) {
						best = slots[i].rank;
						pick = i;
						game = slots[i].game;
						upgrade = true;
					}
				}
			}
			if (pick >= 0 && !quit && !paused) {
				__atomic_add_fetch(&busy, 1, __ATOMIC_SEQ_CST);
				refresh = slots[pick].refresh;
				if (upgrade)
					slots[pick].upgrading = true;   // stays shown meanwhile
				else
					slots[pick].state = 5;   // ours: the other worker skips it
			} else pick = -1;
			// Nothing to fetch for the library: the daily preset check.
			bool presets = pick < 0 && !quit && !paused && !presets_claimed && !sw_nonet && presets_update_due();
			if (presets) {
				presets_claimed = true;
				__atomic_add_fetch(&busy, 1, __ATOMIC_SEQ_CST);
			}
			LightLock_Unlock(&lock);
			if (presets) {
				LightLock_Lock(&net_lock);
				if (!quit && !paused && osGetTime() >= net_retry_at)
					presets_update_work(presets_fetch, presets_stop);
				LightLock_Unlock(&net_lock);
				LightLock_Lock(&lock);
				presets_claimed = false;
				LightLock_Unlock(&lock);
				__atomic_sub_fetch(&busy, 1, __ATOMIC_SEQ_CST);
			}
			if (pick < 0)
				break;
			const game_t *g = library_get(game);
			u64 t0 = osGetTime();
			log_printf("covers: start #%d\n", game);
			bool retry = true;
			cover_t c = { 0 };
			bool ok = g && (upgrade ? upgrade_cover(g, &retry, &c) : load_cover(g, &retry, refresh, &c));
			log_printf("covers: #%d %s -> %s%s in %llums\n", game, g ? g->cover_key : "?",
				ok ? "ok" : "none", ok && c.lowres ? " (small)" : "", osGetTime() - t0);
			LightLock_Lock(&lock);
			if (slots[pick].game == game && (upgrade ? slots[pick].upgrading : slots[pick].state == 5)) {
				if (ok) {
					free(slots[pick].pixels);
					slots[pick].pixels = c.data;
					slots[pick].w = c.w;
					slots[pick].h = c.h;
					slots[pick].lowres = c.lowres;
					slots[pick].refresh = false;
					c.data = NULL;
				}
				// interrupted by HOME/sleep: try again later, not "no cover";
				// without a network, not before the backoff is over
				slots[pick].retry_at = paused ? 0 : (net_retry_at > osGetTime() + 15000 ? net_retry_at : osGetTime() + 15000);
				if (upgrade) {
					slots[pick].upgrading = false;
					if (ok)
						slots[pick].state = 2;   // the shown picture is replaced
					else if (!retry)
						slots[pick].lowres = false;
				} else {
					slots[pick].state = ok ? 2 : (retry ? 1 : 4);
				}
			}
			free(c.data);
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
	LightLock_Init(&state_lock);
	boxart_misses_load(COVER_STATE, PSX3D_VERSION);
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
	}
	net_close();
	state_flush();
	boxart_misses_free();
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
C3D_Tex *covers_get(int game, Tex3DS_SubTexture *sub)
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
			s->shown = false;   // the texture still holds the previous game's cover
			s->lowres = false;
			s->upgrading = false;
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
	bool shown = s && s->shown && s->tex_ok;
	if (shown && sub) {
		// the picture is the top left w x h of the texture; top = 1 is its first row
		float u = (float)s->w / COVER_TEX, v = 1.0f - (float)s->h / COVER_TEX;
		*sub = (Tex3DS_SubTexture){ s->w, s->h, 0.0f, 1.0f, u, v };
	}
	LightLock_Unlock(&lock);
	return shown ? &s->tex : NULL;
}

// Upload decoded covers to textures. Called inside the frame (from
// video_present, after C3D_FrameBegin). The cache holds the texture exactly
// as the GPU wants it, so this is one copy and a cache flush: no GX transfer
// (an old blocking one raced citro3d's own GPU queue and could hang the app)
// and no staging buffer. A cover that replaces a shown one (small -> full
// size) is the same picture, so a frame still in flight showing a mix is
// invisible.
static void covers_pump_in_frame(void)
{
	u64 now = svcGetSystemTick();
	if (frame_last) {
		u32 us = (u32)((now - frame_last) * 1000000 / SYSCLOCK_ARM11);
		if (us < 1000000) {   // longer gaps are screen changes, not frames
			frames++;
			frame_total_us += us;
			frames_over_25ms += us > 25000;
			frames_over_40ms += us > 40000;
			if (us > frame_max_us) frame_max_us = us;
		}
	}
	frame_last = now;
	int done = 0;
	for (int i = 0; i < SLOTS && done < 2; i++) {
		slot_t *s = &slots[i];
		if (s->state != 2)
			continue;
		if (!s->tex_ok) {
			s->tex_ok = C3D_TexInit(&s->tex, COVER_TEX, COVER_TEX, GPU_RGB565);
			if (s->tex_ok) {
				C3D_TexSetFilter(&s->tex, GPU_LINEAR, GPU_LINEAR);
				C3D_TexSetWrap(&s->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
			}
		}
		if (!s->tex_ok)
			return;
		u64 t0 = svcGetSystemTick();
		LightLock_Lock(&lock);
		bool still = s->state == 2 && s->pixels;
		if (still) {
			memcpy(s->tex.data, s->pixels, COVER_PACK_BYTES);
			free(s->pixels);
			s->pixels = NULL;
			s->shown = true;
			s->state = 3;
		}
		LightLock_Unlock(&lock);
		if (!still)
			continue;
		GSPGPU_FlushDataCache(s->tex.data, COVER_PACK_BYTES);
		u32 us = (u32)((svcGetSystemTick() - t0) * 1000000 / SYSCLOCK_ARM11);
		uploads++;
		upload_total_us += us;
		if (us > upload_max_us) upload_max_us = us;
		done++;
	}
}

void covers_pump(void)
{
	if (sw_nocovers)
		return;
	video_pre_draw = covers_pump_in_frame;   // runs at the next frame start
}

// Developer diagnostics: start the frame and upload counters over.
void covers_stats_reset(void)
{
	frame_last = 0;
	frames = frames_over_25ms = frames_over_40ms = frame_max_us = 0;
	frame_total_us = 0;
	uploads = upload_max_us = upload_total_us = 0;
}

// Developer-only refresh of a library cover, without deleting any SD files.
bool covers_refresh(int game)
{
	if (sw_nocovers || sw_nonet || game < 0 || game >= library_count()) return false;
	covers_get(game, NULL);
	bool ok = false;
	LightLock_Lock(&lock);
	for (int i = 0; i < SLOTS; i++) if (slots[i].game == game && slots[i].state != 5 && !slots[i].upgrading) {
		free(slots[i].pixels);
		slots[i].pixels = NULL;
		slots[i].lowres = false;
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

// A game needs the GPU memory: drop the cover textures (about 4 MB). Covers
// come back from their cache files on return to the library. The New 3DS
// keeps pixels that were decoded but not uploaded yet; the Old 3DS needs
// that heap for emulation too.
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
		s->shown = false;
		s->upgrading = false;
		s->lowres = false;   // the reload finds that out again
		if (!g_new3ds) {
			free(s->pixels);
			s->pixels = NULL;
			s->retry_at = 0;
			s->state = s->game >= 0 ? 1 : 0;
		}
		if (s->state == 3)
			s->state = 1;
	}
	LightLock_Unlock(&lock);
}

// forget everything (after a rescan the game indexes change). A rescan the
// player asked for also asks the database again for pictures it did not have.
void covers_reset(void)
{
	if (sw_nocovers)
		return;
	LightLock_Lock(&state_lock);
	boxart_misses_clear();
	LightLock_Unlock(&state_lock);
	state_flush();
	LightLock_Lock(&lock);
	for (int i = 0; i < SLOTS; i++) {
		slots[i].game = -1;
		slots[i].state = 0;
		slots[i].shown = false;
		slots[i].lowres = false;
		slots[i].upgrading = false;
		free(slots[i].pixels);
		slots[i].pixels = NULL;
	}
	LightLock_Unlock(&lock);
}

// Token-gated developer diagnostics. No arbitrary SD reads; report only this
// subsystem's cache files and bounded slot/counter snapshots.
void covers_debug(char *out, size_t len)
{
	int ready = 0, wanted = 0, missing = 0, cached = 0, invalid = 0, legacy = 0;
	for (int i = 0; i < library_count(); i++) {
		char path[PATH_LEN];
		struct stat st;
		cache_path(library_get(i)->cover_key, ".cv2", path, sizeof(path));
		if (!stat(path, &st)) {
			if (st.st_size == CV2_BYTES) cached++;
			else invalid++;
			continue;
		}
		cache_path(library_get(i)->cover_key, ".bin", path, sizeof(path));
		if (stat(path, &st)) missing++;
		else if (st.st_size == COVER_LEGACY_SIZE * COVER_LEGACY_SIZE * 4) legacy++;
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
	         "\"ready\":%d,\"wanted\":%d,\"cached\":%d,\"missing\":%d,\"invalid_cache\":%d,"
	         "\"legacy\":%d,\"upgraded\":%u,\"miss_skips\":%u,\"other_names\":%u,\"uploads\":%u,\"upload_avg_us\":%u,\"upload_max_us\":%u,"
	         "\"frames\":%u,\"frame_avg_us\":%u,\"frame_max_us\":%u,\"frames_over_25ms\":%u,\"frames_over_40ms\":%u}",
	         paused ? "true" : "false", busy, net_open ? "true" : "false", attempts,
	         downloaded, cache_hits, aborted_count, failures, last_curl, last_http,
	         stop_ms, max_stop_ms, ready, wanted, cached, missing, invalid,
	         legacy, upgraded, miss_skips, other_names, uploads, uploads ? upload_total_us / uploads : 0, upload_max_us,
	         frames, frames ? (unsigned)(frame_total_us / frames) : 0, frame_max_us, frames_over_25ms, frames_over_40ms);
}
