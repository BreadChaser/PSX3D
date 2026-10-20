// Debug-only, single-client TCP transport. SOC calls never run on the emulator
// thread. All sockets are nonblocking: libctru SOC shares an IPC session, so even
// a blocking select/poll in this thread could stall another subsystem.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* strcasestr, memmem */
#endif
#include "devnet.h"
#include "clip.h"
#include "benchstate.h"
#include <mbedtls/sha256.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <png.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *const dev_columns[DEV_METRICS] = {
    "fps",       "speed_pct",     "emu_ms",  "present_ms", "wait_ms",   "gpu_ms",  "mcycles",
    "ev0_k",     "ev1_k",         "skipped", "late",       "batches",   "prims",   "decodes",
    "evictions", "invalidations", "uploads", "syncs",      "readbacks", "pic_gpu", "pic_none",
    "lum",       "top_lum",       "xfer_ms", "heap_ms",    "queue_ms",  "gap_ms", "front_run_ms", "front_present_ms", "audio_ms", "loop_ms", "pace_ms", "top_count", "bottom_count", "held_count", "ff", "spu_ms", "spu_wait_ms", "skip_level", "drawn_count", "skipped_count", "audio_underruns", "audio_drops", "audio_gap_ms", "pica_cpu_ms", "phase_list_ms", "phase_prim_ms", "phase_tex_ms", "phase_decode_ms", "phase_state_ms", "phase_submit_ms", "phase_read_ms", "phase_vertex_ms", "draw_mcycles", "skip_mcycles", "idle_mcycles", "draw_vblanks", "skip_vblanks", "idle_vblanks", "renderer_mcycles", "renderer_ev0_k", "renderer_ev1_k", "renderer_samples", "draw_ev0_k", "draw_ev1_k", "draw_spu_ms", "draw_spuwait_ms", "draw_pica_ms", "draw_audio_ms", "draw_dma_ms", "skip_ev0_k", "skip_ev1_k", "skip_spu_ms", "skip_spuwait_ms", "skip_pica_ms", "skip_audio_ms", "skip_dma_ms", "idle_ev0_k", "idle_ev1_k", "idle_spu_ms", "idle_spuwait_ms", "idle_pica_ms", "idle_audio_ms", "idle_dma_ms", "draw_fraction", "picture_interval_ms", "picture_interval_ms2", "picture_intervals"};
static LightLock lock;
static Thread worker;
static u32 *soc_buffer;
static bool soc_owned, stopping;
static int server = -1;
static int mailbox; /* 0 idle, 1 request, 2 main owns, 3 reply ready */
static char request[DEV_COMMAND_MAX], status_json[12288] = "{\"mode\":\"starting\"}";
static char *reply;
static dev_measurement *history;
static unsigned history_count, history_pos;
static u8 *shot_pixels;
static int shot_height;
static char shot_path[PATH_LEN];
static bool shot_reply;
static char update_args[1024];
static bool background_update;
static char import_path[PATH_LEN], import_hash[65];
static bool importing;
bool devnet_import_active(void)
{
    LightLock_Lock(&lock); bool active=importing; LightLock_Unlock(&lock); return active;
}
void devnet_import_reply(const char *path, const char *sha256)
{
    LightLock_Lock(&lock);
    importing=true;
    snprintf(import_path,sizeof(import_path),"%s",path);
    snprintf(import_hash,sizeof(import_hash),"%s",sha256);
    mailbox=3;
    LightLock_Unlock(&lock);
}


/* JSON escapes all control bytes, quotes and backslashes (game/option names). */
static char *json_string(char *p, const char *s)
{
	*p++ = '"';
	for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
		if (*c == '"' || *c == '\\') {
			*p++ = '\\';
			*p++ = *c;
		} else if (*c < 32) {
			sprintf(p, "\\u%04x", *c);
			p += 6;
		} else
			*p++ = *c;
	}
	*p++ = '"';
	*p = 0;
	return p;
}

static char *stats_json(void)
{
	// Strings can expand 6x under JSON escaping; bounded fixed input fields.
	char *out = malloc(DEV_HISTORY * 20000 + 128);
	if (!out)
		return NULL;
	dev_measurement *snapshot = malloc(DEV_HISTORY * sizeof(*history));
	if (!snapshot) {
		free(out);
		return NULL;
	}
	LightLock_Lock(&lock);
	unsigned count = history_count, pos = history_pos;
	memcpy(snapshot, history, DEV_HISTORY * sizeof(*history));
	LightLock_Unlock(&lock);
	char *p = out;
	p += sprintf(p, "{\"history\":[");
	for (unsigned j = 0; j < count; j++) {
		const dev_measurement *s = &snapshot[(pos + DEV_HISTORY - count + j) % DEV_HISTORY];
		p += sprintf(p, "%s{\"seq\":%llu,\"ms\":%llu", j ? "," : "", s->seq, s->ms);
		for (int i = 0; i < DEV_METRICS; i++) {
			// Negative values mean unavailable, represented as null rather than a fake zero.
			if (s->v[i] < 0)
				p += sprintf(p, ",\"%s\":null", dev_columns[i]);
			else
				p += sprintf(p, ",\"%s\":%.6f", dev_columns[i], s->v[i]);
		}
		p += sprintf(p, ",\"audio\":");
		p = json_string(p, s->audio);
		p += sprintf(p, ",\"pica\":");
		p = json_string(p, s->pica);
		p += sprintf(p, ",\"probe\":");
		p = json_string(p, s->probe);
		*p++ = '}';
	}
	free(snapshot);
	strcpy(p, "]}");
	return out;
}

void devnet_status(const char *json)
{
	if (!dev_remote)
		return;
	LightLock_Lock(&lock);
	snprintf(status_json, sizeof(status_json), "%s", json);
	LightLock_Unlock(&lock);
}
void devnet_sample(const dev_measurement *s, bool reset)
{
	if (!dev_remote || !history)
		return;
	LightLock_Lock(&lock);
	if (reset)
		history_pos = history_count = 0;
	if (s) {
		history[history_pos] = *s;
		history_pos = (history_pos + 1) % DEV_HISTORY;
		if (history_count < DEV_HISTORY)
			history_count++;
	}
	LightLock_Unlock(&lock);
}
bool devnet_take(char *out)
{
	if (!worker)
		return false;
	bool got = false;
	LightLock_Lock(&lock);
	if (mailbox == 1) {
		strcpy(out, request);
		mailbox = 2;
		got = true;
	}
	LightLock_Unlock(&lock);
	return got;
}
void devnet_notification_done(void)
{
	LightLock_Lock(&lock);
	mailbox = 0;
	LightLock_Unlock(&lock);
}
void devnet_reply(const char *json)
{
	char *r = strdup(json);
	LightLock_Lock(&lock);
	free(reply);
	reply = r;
	mailbox = 3;
	LightLock_Unlock(&lock);
}
void devnet_start_update(const char *args)
{
	LightLock_Lock(&lock);
	snprintf(update_args, sizeof(update_args), "%s", args);
	background_update = true;
	LightLock_Unlock(&lock);
}
void devnet_update_reply(const char *args)
{
	LightLock_Lock(&lock);
	snprintf(update_args, sizeof(update_args), "%s", args);
	mailbox = 3;
	LightLock_Unlock(&lock);
}

// gfx's display-transfer output is already CPU-addressable RGB8. This copies
// exactly once per requested shot, not every frame. PNG compression is worker-only.
bool devnet_shot(const char *path, bool bottom)
{
	int height = bottom ? 480 : 240;
	u8 *rgb = calloc(400 * height, 3);
	if (!rgb)
		return false;
	C3D_FrameSync();
	for (int screen = 0; screen <= (int)bottom; screen++) {
		u16 rows, cols;
		gfxScreen_t scr = screen ? GFX_BOTTOM : GFX_TOP;
		gfxSetDoubleBuffering(scr, false);
		u8 *fb = gfxGetFramebuffer(scr, GFX_LEFT, &rows, &cols);
		gfxSetDoubleBuffering(scr, true);
		if (!fb || rows != 240 || cols != (screen ? 320 : 400)) {
			free(rgb);
			return false;
		}
		GSPGPU_InvalidateDataCache(fb, rows * cols * 3);
		for (int y = 0; y < 240; y++)
			for (int x = 0; x < cols; x++) {
				const u8 *src = fb + (x * 240 + 239 - y) * 3;
				u8 *dst = rgb + ((y + screen * 240) * 400 + x) * 3;
				dst[0] = src[2];
				dst[1] = src[1];
				dst[2] = src[0];
			}
	}
	LightLock_Lock(&lock);
	if (shot_pixels) {
		LightLock_Unlock(&lock);
		free(rgb);
		return false;
	}
	shot_pixels = rgb;
	shot_height = height;
	shot_reply = !path;
	snprintf(shot_path, sizeof(shot_path), "%s", path ? path : "");
	LightLock_Unlock(&lock);
	return true;
}

bool devnet_stopping(void)
{
	LightLock_Lock(&lock);
	bool b = stopping;
	LightLock_Unlock(&lock);
	return b;
}
static void nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
static bool transfer(int fd, void *buf, size_t size, bool sending)
{
	u64 deadline = osGetTime() + 15000;
	u8 *p = buf;
	while (size && !devnet_stopping()) {
		int n = sending ? send(fd, p, size > 8192 ? 8192 : size, 0) : recv(fd, p, 1, 0);
		if (n > 0) {
			p += n;
			size -= n;
			deadline = osGetTime() + 15000;
		} else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
			return false;
		else if (osGetTime() > deadline)
			return false;
		else
			svcSleepThread(2000000);
	}
	return !size;
}
static void send_payload(int fd, const char *kind, void *data, size_t len)
{
	char h[64];
	int n = snprintf(h, sizeof(h), "%s %u\n", kind, (unsigned)len);
	if (transfer(fd, h, n, true))
		transfer(fd, data, len, true);
}
static void send_json(int fd, const char *s)
{
	send_payload(fd, "JSON", (void *)(s ? s : "{\"error\":\"out of memory\"}"),
	             s ? strlen(s) : strlen("{\"error\":\"out of memory\"}"));
}
/* The main thread authorizes a concrete empty benchmark destination before
   this worker receives any bytes. Bounded 8 KiB writes; no allocation of a full
   state, and no sockets or core calls on the main thread. */
static void receive_benchmark_state(int fd, const char *path, const char *expected)
{
    char temp[PATH_LEN + 16], actual[65];
    unsigned char buffer[8192], header[40], digest[32];
    snprintf(temp,sizeof(temp),"%s.import",path);
    int staging = open(temp,O_CREAT|O_EXCL|O_WRONLY,0600);
    FILE *file = staging < 0 ? NULL : fdopen(staging,"wb");
    if (!file) {
        if (staging >= 0) { close(staging); unlink(temp); }
        send_json(fd,"{\"ok\":false,\"error\":\"cannot create exclusive state staging file\"}");
        return;
    }
    send_json(fd,"{\"ok\":true,\"ready\":true,\"size\":4456448}");
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    mbedtls_sha256_starts_ret(&hash,0);
    unsigned received = 0;
    u64 deadline = osGetTime()+15000;
    bool ok = true;
    while (received < BENCH_STATE_BYTES && !devnet_stopping()) {
        unsigned want = BENCH_STATE_BYTES-received;
        if (want > sizeof(buffer)) want = sizeof(buffer);
        int n = recv(fd,buffer,want,0);
        if (n > 0) {
            if (received < sizeof(header)) {
                unsigned copy = sizeof(header)-received;
                if (copy > (unsigned)n) copy=n;
                memcpy(header+received,buffer,copy);
            }
            if (fwrite(buffer,1,n,file) != (size_t)n) { ok=false; break; }
            mbedtls_sha256_update_ret(&hash,buffer,n);
            received += n; deadline=osGetTime()+15000;
        } else if (!n || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) || osGetTime()>deadline) {
            ok=false; break;
        } else svcSleepThread(2000000);
    }
    mbedtls_sha256_finish_ret(&hash,digest);
    mbedtls_sha256_free(&hash);
    for (unsigned i=0;i<32;i++) sprintf(actual+2*i,"%02x",digest[i]);
    if (fclose(file)) ok=false;
    ok = ok && received == BENCH_STATE_BYTES && bench_state_header(header,received) && !strcasecmp(expected,actual);
    if (ok) ok=bench_state_commit(temp,path);
    unlink(temp);
    if (ok) send_json(fd,"{\"ok\":true,\"imported\":true,\"size\":4456448}");
    else send_json(fd,"{\"ok\":false,\"error\":\"state transfer/format/hash failed or destination exists; existing slots untouched\"}");
}

static bool encode_shot(int client)
{
	LightLock_Lock(&lock);
	u8 *pixels = shot_pixels;
	int height = shot_height;
	bool response = shot_reply;
	char path[PATH_LEN];
	strcpy(path, shot_path);
	shot_pixels = NULL;
	LightLock_Unlock(&lock);
	if (!pixels)
		return false;
	png_image img = {0};
	img.version = PNG_IMAGE_VERSION;
	img.width = 400;
	img.height = height;
	img.format = PNG_FORMAT_RGB;
	// Remote diagnostics are lossless intermediate captures. Avoid spending
	// seconds compressing on Old 3DS while its main core is saturated.
	img.flags |= PNG_IMAGE_FLAG_FAST;
	png_alloc_size_t bytes = 0;
	void *png = NULL;
	bool ok = png_image_write_to_memory(&img, NULL, &bytes, 0, pixels, 0, NULL);
	if (ok)
		png = malloc(bytes);
	ok = ok && png && png_image_write_to_memory(&img, png, &bytes, 0, pixels, 0, NULL);
	if (response && client >= 0) {
		if (ok)
			send_payload(client, "PNG", png, bytes);
		else
			send_json(client, "{\"error\":\"PNG encoding failed\"}");
	} else if (ok && path[0]) {
		FILE *f = fopen(path, "wb");
		if (f) {
			fwrite(png, 1, bytes, f);
			fclose(f);
		}
	}
	png_image_free(&img);
	free(png);
	free(pixels);
	return response;
}

/* Remote file and diagnostics API: token-gated, LAN-only like every command.
 * Runs on this worker thread, never on the emulator thread. While a game or
 * batch runs, SD access here could delay the emulator's own CD/save reads, so
 * every file command waits for the library. */
static volatile bool files_busy;
void devnet_busy(bool busy) { files_busy = busy; }
static bool files_allowed(int fd)
{
	if (!dev_token[0]) {
		send_json(fd, "{\"error\":\"unauthorized\"}");
		return false;
	}
	if (files_busy) {
		send_json(fd, "{\"error\":\"busy: a game or batch is running; quit to the library first\"}");
		return false;
	}
	return true;
}
/* Normalise "sdmc:/a//b/" to "sdmc:/a/b". "." and ".." are refused, not resolved. */
static bool sd_path(const char *in, char *out, size_t len)
{
	if (strncmp(in, "sdmc:/", 6) || len < 8)
		return false;
	size_t o = 6;
	memcpy(out, "sdmc:/", 6);
	for (const char *p = in + 6; *p;) {
		while (*p == '/')
			p++;
		const char *e = p;
		while (*e && *e != '/')
			e++;
		size_t n = e - p;
		if (!n)
			break;
		if ((n == 1 && *p == '.') || (n == 2 && p[0] == '.' && p[1] == '.'))
			return false;
		for (const char *c = p; c < e; c++)
			if ((unsigned char)*c < 32 || *c == '\\' || *c == ':' || *c == 127)
				return false;
		if (o + (o > 6) + n + 1 > len)
			return false;
		if (o > 6)
			out[o++] = '/';
		memcpy(out + o, p, n);
		o += n;
		p = e;
	}
	out[o] = 0;
	return true;
}
static size_t json_quote(char *out, size_t len, const char *s)
{
	size_t o = 0;
	if (len < 3)
		return 0;
	out[o++] = '"';
	for (; *s && o + 8 < len; s++) {
		unsigned char c = *s;
		if (c == '"' || c == '\\')
			o += snprintf(out + o, len - o, "\\%c", c);
		else if (c < 32)
			o += snprintf(out + o, len - o, "\\u%04x", c);
		else
			out[o++] = c;
	}
	out[o++] = '"';
	out[o] = 0;
	return o;
}
/* Streams in 8 KiB chunks: any size, never held in memory. */
static void stream_file(int fd, const char *path)
{
	FILE *f = fopen(path, "rb");
	struct stat st;
	if (!f || fstat(fileno(f), &st) || S_ISDIR(st.st_mode)) {
		if (f)
			fclose(f);
		send_json(fd, "{\"error\":\"file missing\"}");
		return;
	}
	if (fseek(f, 0, SEEK_END)) {
		fclose(f);
		send_json(fd, "{\"error\":\"file read failed\"}");
		return;
	}
	long long size = ftell(f);
	rewind(f);
	char header[64];
	static char buffer[8192];
	int n = snprintf(header, sizeof(header), "FILE %lld\n", size);
	bool ok = size >= 0 && transfer(fd, header, n, true);
	while (ok && size > 0) {
		size_t chunk = size > (long long)sizeof(buffer) ? sizeof(buffer) : (size_t)size;
		ok = fread(buffer, 1, chunk, f) == chunk && transfer(fd, buffer, chunk, true);
		size -= chunk;
	}
	fclose(f);
}
static u64 file_mtime(const char *path)
{
	u64 t = 0;
	return R_SUCCEEDED(archive_getmtime(path, &t)) ? t : 0;
}
/* JSON {"ok":true,"path":..,"entries":[{name,dir,size,mtime}]}; filter keeps a name. */
static void list_dir(int fd, const char *path, bool (*filter)(const char *))
{
	DIR *dir = opendir(path);
	if (!dir) {
		send_json(fd, "{\"error\":\"folder missing\"}");
		return;
	}
	size_t cap = 64 * 1024, used = 0;
	char *json = malloc(cap);
	if (!json) {
		closedir(dir);
		send_json(fd, NULL);
		return;
	}
	used += snprintf(json, cap, "{\"ok\":true,\"path\":");
	used += json_quote(json + used, cap - used, path);
	used += snprintf(json + used, cap - used, ",\"entries\":[");
	unsigned count = 0;
	bool truncated = false;
	struct dirent *e;
	char full[PATH_LEN];
	while ((e = readdir(dir))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || (filter && !filter(e->d_name)))
			continue;
		if (used + 600 > cap) {
			truncated = true;
			break;
		}
		snprintf(full, sizeof(full), "%s%s%s", path, path[strlen(path) - 1] == '/' ? "" : "/", e->d_name);
		struct stat st;
		bool have = !stat(full, &st), is_dir = have ? S_ISDIR(st.st_mode) : e->d_type == DT_DIR;
		used += snprintf(json + used, cap - used, "%s{\"name\":", count++ ? "," : "");
		used += json_quote(json + used, cap - used, e->d_name);
		used += snprintf(json + used, cap - used, ",\"dir\":%s,\"size\":%lld,\"mtime\":%llu}",
		                 is_dir ? "true" : "false", have && !is_dir ? (long long)st.st_size : 0LL,
		                 (unsigned long long)file_mtime(full));
	}
	closedir(dir);
	snprintf(json + used, cap - used, "],\"truncated\":%s}", truncated ? "true" : "false");
	send_json(fd, json);
	free(json);
}
static bool log_name(const char *n)
{
	return !strcmp(n, "log.txt") || (!strncmp(n, "log.", 4) && n[4] >= '1' && n[4] <= '9' && !strcmp(n + 5, ".txt"));
}
/* put targets: user data folders and the frontend's own config files only. */
static bool put_allowed(const char *path)
{
	static const char *const dirs[] = {"states/", "cheats/", "saves/", "covers/", "recordings/", "games/"};
	static const char *const files[] = {"switches.txt", "config.ini", "games.ini"};
	const char *base = PSX3D_DIR "/";
	size_t n = strlen(base);
	if (strncmp(path, base, n))
		return false;
	const char *rest = path + n;
	for (unsigned i = 0; i < sizeof(files) / sizeof(*files); i++)
		if (!strcmp(rest, files[i]))
			return true;
	for (unsigned i = 0; i < sizeof(dirs) / sizeof(*dirs); i++) {
		size_t d = strlen(dirs[i]);
		if (!strncmp(rest, dirs[i], d) && rest[d])
			return strlen(rest + d) < 4 || strcmp(rest + strlen(rest) - 4, ".tmp");
	}
	return false;
}
/* Receive into <path>.tmp, then swap it in. 3DS FAT rename does not replace an
 * existing file, so keep the old one as <path>.putold until the new one is in
 * place, and put it back if that fails (the updater's switches pattern). */
/* extra: bytes the command reader already received past the '\n'. */
static void put_file(int fd, const char *args, const u8 *extra, size_t extra_len)
{
	char raw[PATH_LEN], path[PATH_LEN];
	long long size = -1;
	const char *space = strrchr(args, ' ');
	if (!space || space == args || (size_t)(space - args) >= sizeof(raw)) {
		send_json(fd, "{\"error\":\"usage: put <sdmc:/path> <size>, then the bytes\"}");
		return;
	}
	char *end;
	size = strtoll(space + 1, &end, 10);
	memcpy(raw, args, space - args);
	raw[space - args] = 0;
	if (!space[1] || *end || size < 0 || size > 8 * 1024 * 1024) {
		send_json(fd, "{\"error\":\"put size must be 0..8388608 bytes\"}");
		return;
	}
	if (!sd_path(raw, path, sizeof(path) - 8) || !put_allowed(path)) {
		send_json(fd, "{\"error\":\"put only writes sdmc:/3ds/PSX3D/{states,cheats,saves,covers,recordings,games}/ "
		              "and switches.txt, config.ini, games.ini\"}");
		return;
	}
	/* Create missing folders below the allowed root (e.g. recordings/<Game>/). */
	char dir[PATH_LEN];
	strcpy(dir, path);
	for (char *c = dir + strlen(PSX3D_DIR) + 1; (c = strchr(c, '/')); c++) {
		*c = 0;
		mkdir(dir, 0777);
		*c = '/';
	}
	char tmp[PATH_LEN + 8], backup[PATH_LEN + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	/* Not .bak: that name is the save's own backup copy (safefile.c). */
	snprintf(backup, sizeof(backup), "%s.putold", path);
	FILE *f = fopen(tmp, "wb");
	if (!f) {
		send_json(fd, "{\"error\":\"cannot create temporary file\"}");
		return;
	}
	static u8 buffer[8192];
	long long left = size;
	bool ok = true;
	size_t take = extra_len < (unsigned long long)left ? extra_len : (size_t)left;
	if (take)
		ok = fwrite(extra, 1, take, f) == take;
	left -= take;
	while (ok && left > 0) {
		size_t chunk = left > (long long)sizeof(buffer) ? sizeof(buffer) : (size_t)left;
		u64 deadline = osGetTime() + 15000;
		size_t got = 0;
		while (!got && !devnet_stopping()) {
			int n = recv(fd, buffer, chunk, 0);
			if (n > 0)
				got = n;
			else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) ||
			         osGetTime() > deadline)
				break;
			else
				svcSleepThread(2000000);
		}
		ok = got && fwrite(buffer, 1, got, f) == got;
		left -= got;
	}
	ok = !fclose(f) && ok;
	if (!ok) {
		remove(tmp);
		send_json(fd, "{\"error\":\"put transfer or write failed; target unchanged\"}");
		return;
	}
	struct stat st;
	bool existed = !stat(path, &st);
	remove(backup);
	if (existed && rename(path, backup)) {
		remove(tmp);
		send_json(fd, "{\"error\":\"cannot move the existing file aside; target unchanged\"}");
		return;
	}
	if (rename(tmp, path)) {
		if (existed)
			rename(backup, path);
		remove(tmp);
		send_json(fd, "{\"error\":\"cannot install the new file; previous file restored\"}");
		return;
	}
	if (existed)
		remove(backup);
	char json[PATH_LEN + 64];
	size_t used = snprintf(json, sizeof(json), "{\"ok\":true,\"path\":");
	used += json_quote(json + used, sizeof(json) - used, path);
	snprintf(json + used, sizeof(json) - used, ",\"size\":%lld,\"replaced\":%s}", size, existed ? "true" : "false");
	send_json(fd, json);
}
static bool file_command(int fd, const char *command, const u8 *extra, size_t extra_len)
{
	char path[PATH_LEN];
	if (!strcmp(command, "crashes") || !strcmp(command, "logs") || !strncmp(command, "ls ", 3) ||
	    !strncmp(command, "get sdmc:", 9) || !strncmp(command, "put ", 4)) {
		if (!files_allowed(fd))
			return true;
	} else
		return false;
	if (!strcmp(command, "crashes")) {
		/* Luma writes ARM11/ARM9 exception dumps here; fetch with get. */
		char json[16384] = "{\"ok\":true,\"dumps\":[";
		size_t used = strlen(json);
		unsigned count = 0;
		static const char *const dirs[] = {"sdmc:/luma/dumps/arm11", "sdmc:/luma/dumps/arm9"};
		for (unsigned i = 0; i < 2; i++) {
			DIR *dir = opendir(dirs[i]);
			struct dirent *e;
			while (dir && (e = readdir(dir))) {
				size_t n = strlen(e->d_name);
				if (n < 4 || strcmp(e->d_name + n - 4, ".dmp") || used + PATH_LEN + 96 > sizeof(json))
					continue;
				snprintf(path, sizeof(path), "%s/%s", dirs[i], e->d_name);
				struct stat st;
				long long size = stat(path, &st) ? -1 : (long long)st.st_size;
				used += snprintf(json + used, sizeof(json) - used, "%s{\"path\":", count++ ? "," : "");
				used += json_quote(json + used, sizeof(json) - used, path);
				used += snprintf(json + used, sizeof(json) - used, ",\"size\":%lld,\"mtime\":%llu}", size,
				                 (unsigned long long)file_mtime(path));
			}
			if (dir)
				closedir(dir);
		}
		snprintf(json + used, sizeof(json) - used, "]}");
		send_json(fd, json);
	} else if (!strcmp(command, "logs"))
		list_dir(fd, PSX3D_DIR, log_name);
	else if (!strncmp(command, "put ", 4))
		put_file(fd, command + 4, extra, extra_len);
	else if (!sd_path(command + (command[0] == 'l' ? 3 : 4), path, sizeof(path)))
		send_json(fd, "{\"error\":\"path must be sdmc:/..., without . or .. components\"}");
	else if (command[0] == 'l')
		list_dir(fd, path, NULL);
	else
		stream_file(fd, path);
	return true;
}
/* Save-state archive commands: token-gated and read-only. Names come from the
 * states/ directory listing itself, so a request cannot form any other path. */
static bool state_name_ok(const char *s, size_t n)
{
	if (!n || n > 100)
		return false;
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' ||
		      c == '_' || c == '-' || c == '(' || c == ')' || c == '.' || c == ',' || c == '\'' || c == '!' ||
		      c == '&' || c == '+'))
			return false;
	}
	return !memmem(s, n, "..", 2);
}
/* Base name and slot of "<game>.stN", "<game>.sta" or their ".thm"; slot -1 = auto. */
static bool state_entry(const char *name, size_t *base, int *slot, bool *thm)
{
	size_t n = strlen(name);
	*thm = n > 4 && !strcmp(name + n - 4, ".thm");
	if (*thm)
		n -= 4;
	if (n < 5 || memcmp(name + n - 4, ".st", 3))
		return false;
	char c = name[n - 1];
	if (c == 'a')
		*slot = -1;
	else if (c >= '0' && c <= '9')
		*slot = c - '0';
	else
		return false;
	*base = n - 4;
	return state_name_ok(name, *base);
}
/* Exact title (case-insensitive, '_' = space) wins; otherwise one unique substring. */
static bool state_game(const char *want_title, char *game, size_t len, char *error, size_t error_len)
{
	char want[104];
	size_t wn = strlen(want_title);
	if (!state_name_ok(want_title, wn)) {
		snprintf(error, error_len, "{\"error\":\"invalid game title\"}");
		return false;
	}
	memcpy(want, want_title, wn + 1);
	for (char *c = want; *c; c++)
		if (*c == '_')
			*c = ' ';
	DIR *dir = opendir(PSX3D_STATE_DIR);
	if (!dir) {
		snprintf(error, error_len, "{\"error\":\"states folder missing\"}");
		return false;
	}
	char found[104] = "";
	int matches = 0;
	bool exact = false;
	struct dirent *e;
	while ((e = readdir(dir))) {
		size_t base;
		int slot;
		bool thm;
		if (!state_entry(e->d_name, &base, &slot, &thm) || base >= sizeof(found))
			continue;
		char name[104];
		memcpy(name, e->d_name, base);
		name[base] = 0;
		if (!strcasecmp(name, want)) {
			if (!exact)
				strcpy(found, name);
			exact = true;
		} else if (!exact && strcasestr(name, want) && strcmp(name, found)) {
			strcpy(found, name);
			matches++;
		}
	}
	closedir(dir);
	if (!exact && matches != 1) {
		snprintf(error, error_len, matches ? "{\"error\":\"game title ambiguous; use the exact title\"}"
		                                   : "{\"error\":\"no save states for that game\"}");
		return false;
	}
	snprintf(game, len, "%s", found);
	return true;
}
static bool state_command(int fd, const char *command)
{
	bool list = !strncmp(command, "states ", 7), get = !strncmp(command, "get state ", 10);
	if (!list && !get)
		return false;
	if (!files_allowed(fd))
		return true;
	char title[104], slot_text[8] = "", extra[8] = "", game[104], error[96];
	const char *args = command + (list ? 7 : 10);
	int fields = sscanf(args, "%103s %7s %7s", title, slot_text, extra);
	if (fields < 1 || (list && fields != 1) || (get && fields < 2) ||
	    (fields == 3 && strcmp(extra, "thm"))) {
		send_json(fd, list ? "{\"error\":\"usage: states <Game_Title>\"}"
		                   : "{\"error\":\"usage: get state <Game_Title> <0-9|auto> [thm]\"}");
		return true;
	}
	int want_slot = -2;
	if (get) {
		if (!strcmp(slot_text, "auto"))
			want_slot = -1;
		else if (slot_text[0] >= '0' && slot_text[0] <= '9' && !slot_text[1])
			want_slot = slot_text[0] - '0';
		else {
			send_json(fd, "{\"error\":\"slot must be 0..9 or auto\"}");
			return true;
		}
	}
	if (!state_game(title, game, sizeof(game), error, sizeof(error))) {
		send_json(fd, error);
		return true;
	}
	char path[PATH_LEN];
	if (get) {
		bool thm = fields == 3;
		if (want_slot < 0)
			snprintf(path, sizeof(path), PSX3D_STATE_DIR "/%s.sta%s", game, thm ? ".thm" : "");
		else
			snprintf(path, sizeof(path), PSX3D_STATE_DIR "/%s.st%d%s", game, want_slot, thm ? ".thm" : "");
		stream_file(fd, path);
		return true;
	}
	char json[2048];
	size_t used = snprintf(json, sizeof(json), "{\"ok\":true,\"game\":\"%s\",\"slots\":[", game);
	unsigned count = 0;
	for (int slot = -1; slot <= 9; slot++) {
		for (int thm = 0; thm < 2; thm++) {
			if (slot < 0)
				snprintf(path, sizeof(path), PSX3D_STATE_DIR "/%s.sta%s", game, thm ? ".thm" : "");
			else
				snprintf(path, sizeof(path), PSX3D_STATE_DIR "/%s.st%d%s", game, slot, thm ? ".thm" : "");
			FILE *f = fopen(path, "rb");
			if (!f) {
				if (!thm)
					break;
				used += snprintf(json + used, sizeof(json) - used, ",\"thm_size\":0}");
				continue;
			}
			fseek(f, 0, SEEK_END);
			long size = ftell(f);
			/* Thumbnail header: magic, w/h, save time (seconds, 3DS clock). */
			u32 header[3] = {0};
			rewind(f);
			bool have_time = thm && fread(header, 1, sizeof(header), f) == sizeof(header);
			fclose(f);
			if (!thm) {
				char name[8];
				snprintf(name, sizeof(name), slot < 0 ? "auto" : "%d", slot);
				used += snprintf(json + used, sizeof(json) - used, "%s{\"slot\":\"%s\",\"size\":%ld", count++ ? "," : "",
				                 name, size);
			} else
				used += snprintf(json + used, sizeof(json) - used, ",\"thm_size\":%ld,\"saved\":%u}", size,
				                 have_time ? (unsigned)header[2] : 0);
		}
	}
	snprintf(json + used, sizeof(json) - used, "]}");
	send_json(fd, json);
	return true;
}
static void serve_client(int fd)
{
	char line[DEV_COMMAND_MAX];
	size_t used = 0, extra_len = 0;
	u64 deadline = osGetTime() + 15000;
	while (used < sizeof(line) - 1 && !devnet_stopping()) {
		int n = recv(fd, line + used, sizeof(line) - 1 - used, 0);
		if (n > 0) {
			used += n;
			char *newline = memchr(line, '\n', used);
			if (newline) {
				extra_len = line + used - (newline + 1);
				used = newline - line + 1;
				break;
			}
			deadline = osGetTime() + 15000;
		} else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
			return;
		else if (osGetTime() > deadline)
			return;
		else
			svcSleepThread(2000000);
	}
	if (memchr(line, 0, used) || memchr(line, '\r', used)) {
		send_json(fd, "{\"error\":\"invalid command bytes\"}");
		return;
	}
	if (!used || line[used - 1] != '\n') {
		send_json(fd, "{\"error\":\"command too long\"}");
		return;
	}
	line[used - 1] = 0;
	char *command = line;
	if (dev_token[0]) {
		size_t n = strlen(dev_token);
		if (strncmp(line, dev_token, n) || line[n] != ' ') {
			send_json(fd, "{\"error\":\"unauthorized\"}");
			return;
		}
		command = line + n + 1;
	}
	if (*command == '_') {
		send_json(fd, "{\"error\":\"reserved command\"}");
		return;
	}
	if (!strcmp(command, "status")) {
		char status[12288];
		LightLock_Lock(&lock);
		strcpy(status, status_json);
		LightLock_Unlock(&lock);
		send_json(fd, status);
		return;
	}
	if (!strcmp(command, "stats")) {
		char *s = stats_json();
		send_json(fd, s);
		free(s);
		return;
	}
	if (file_command(fd, command, (const u8 *)line + used, extra_len) || state_command(fd, command))
		return;
	/* Recording archive commands are read-only and rooted in recordings/. */
	if (!strncmp(command,"recordings ",11)) {
		const char *title=command+11;
/* Bounded truncation is intentional; preserve the validated release code layout. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
		char probe[256]; snprintf(probe,sizeof(probe),"recordings/%s/1.inp",title);
#pragma GCC diagnostic pop
		if (!clip_archive_name(probe)) { send_json(fd,"{\"error\":\"invalid recording title\"}"); return; }
		char path[PATH_LEN]; snprintf(path,sizeof(path),PSX3D_DIR "/recordings/%s",title);
		DIR *dir=opendir(path);
		if (!dir) { send_json(fd,"{\"error\":\"recording folder missing\"}"); return; }
		char json[12288]="{\"ok\":true,\"recordings\":["; size_t used=strlen(json); unsigned count=0;
		struct dirent *entry;
		while ((entry=readdir(dir))) {
/* Bounded truncation is intentional; preserve the validated release code layout. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
			snprintf(probe,sizeof(probe),"recordings/%s/%s",title,entry->d_name);
#pragma GCC diagnostic pop
			if (!clip_archive_name(probe) || !strstr(entry->d_name,".inp")) continue;
			if (used+32>=sizeof(json)) break;
			used+=snprintf(json+used,sizeof(json)-used,"%s%u",count++?",":"",(unsigned)strtoul(entry->d_name,NULL,10));
		}
		closedir(dir);
		snprintf(json+used,sizeof(json)-used,"],\"truncated\":%s}",entry?"true":"false");
		send_json(fd,json); return;
	}
	if (!strncmp(command,"get recordings/",15)) {
		const char *name=command+4;
		if (!clip_archive_name(name)) { send_json(fd,"{\"error\":\"invalid recording filename\"}"); return; }
		char path[PATH_LEN]; snprintf(path,sizeof(path),PSX3D_DIR "/%s",name);
		FILE *f=fopen(path,"rb");
		if (!f) { send_json(fd,"{\"error\":\"recording missing\"}"); return; }
		fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
		if (size<=0 || size>8*1024*1024) { fclose(f); send_json(fd,"{\"error\":\"invalid recording size\"}"); return; }
		char header[64]; int n=snprintf(header,sizeof(header),"FILE %ld\n",size);
		bool ok=transfer(fd,header,n,true); char buffer[8192];
		while (ok && size>0) {
			size_t chunk=size>(long)sizeof(buffer)?sizeof(buffer):(size_t)size;
			ok=fread(buffer,1,chunk,f)==chunk && transfer(fd,buffer,chunk,true);
			size-=chunk;
		}
		fclose(f); return;
	}
	/* End recording archive commands. */
	if (!strncmp(command, "get ", 4)) {
		const char *name = command + 4;
		bool valid = !strcmp(name, "results.tsv") || !strcmp(name, "results.md");
		size_t n = strlen(name);
		if (n > 4 && !strcmp(name + n - 4, ".png")) {
			valid = true;
			for (const char *p = name; p < name + n - 4; p++)
				if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
				      *p == '-' || *p == '_'))
					valid = false;
		}
		if (!valid || n > 100) {
			send_json(fd, "{\"error\":\"invalid result filename\"}");
			return;
		}
		char path[PATH_LEN];
		snprintf(path, sizeof(path), PSX3D_DIR "/bench/%s", name);
		FILE *f = fopen(path, "rb");
		if (!f) {
			send_json(fd, "{\"error\":\"file missing\"}");
			return;
		}
		fseek(f, 0, SEEK_END);
		long size = ftell(f);
		rewind(f);
		void *data = size > 0 && size < 2 * 1024 * 1024 ? malloc(size) : NULL;
		if (data && fread(data, 1, size, f) == (size_t)size)
			send_payload(fd, "FILE", data, size);
		else
			send_json(fd, "{\"error\":\"file too large or read failed\"}");
		free(data);
		fclose(f);
		return;
	}
	LightLock_Lock(&lock);
	if (mailbox) {
		LightLock_Unlock(&lock);
		send_json(fd, "{\"error\":\"busy\"}");
		return;
	}
	strcpy(request, command);
	mailbox = 1;
	LightLock_Unlock(&lock);
	// Only one mutation outstanding. A disconnected requester cannot cause the
	// next request to receive its response. No socket wait on the main thread.
	while (!devnet_stopping()) {
		LightLock_Lock(&lock);
		int state = mailbox;
		LightLock_Unlock(&lock);
		if (state == 3)
			break;
		svcSleepThread(2000000);
	}
	LightLock_Lock(&lock);
	char *r = reply;
	reply = NULL;
	char state_path[PATH_LEN], state_hash[65];
    strcpy(state_path,import_path); strcpy(state_hash,import_hash); import_path[0]=0;
	char update[sizeof(update_args)];
	strcpy(update, update_args);
	update_args[0] = 0;
	LightLock_Unlock(&lock);
	if (update[0]) {
		char result[512];
		bool ok = dev_update(update, result, sizeof(result));
		send_json(fd, result);
		if (ok) {
			// Installation and response are complete. Main performs orderly teardown
			// and APT relaunch, never a reboot or kernel backdoor.
			LightLock_Lock(&lock);
			strcpy(request, "_updated");
			mailbox = 1;
			LightLock_Unlock(&lock);
			free(r);
			return;
		}
	} else if (state_path[0])
        {
        receive_benchmark_state(fd,state_path,state_hash);
        LightLock_Lock(&lock); importing=false; LightLock_Unlock(&lock);
    }
    else if (!encode_shot(fd))
		send_json(fd, r);
	free(r);
	LightLock_Lock(&lock);
	if (update[0]) {
		strcpy(request, "_update_failed");
		mailbox = 1;
	} else
		mailbox = 0;
	LightLock_Unlock(&lock);
}

static void network_main(void *unused)
{
	(void)unused;
	while (!devnet_stopping()) {
		LightLock_Lock(&lock);
		bool background = background_update;
		background_update = false;
		LightLock_Unlock(&lock);
		if (background) {
			char result[512];
			bool ok = dev_update(update_args, result, sizeof(result));
			log_printf("developer startup update: %s\n", result);
			LightLock_Lock(&lock);
			update_args[0] = 0;
			strcpy(request, ok ? "_updated" : "_update_failed");
			mailbox = 1;
			LightLock_Unlock(&lock);
		}
		encode_shot(-1); // queued benchmark PNGs even without a connected client
		if (server >= 0) {
			struct sockaddr_in peer;
			socklen_t size = sizeof(peer);
			int fd = accept(server, (struct sockaddr *)&peer, &size);
			if (fd >= 0) {
				nonblock(fd);
				// Restrict to this console's directly attached IPv4 subnet. Never accept
				// routed internet/Tailscale clients, even if someone forwards the port.
				SOCU_IPInfo ip;
				socklen_t ip_size = sizeof(ip);
				if (SOCU_GetNetworkOpt(SOL_CONFIG, NETOPT_IP_INFO, &ip, &ip_size) == 0 &&
				    (peer.sin_addr.s_addr & ip.netmask.s_addr) == (ip.ip.s_addr & ip.netmask.s_addr))
					serve_client(fd);
				// SOC close can reset a connection with response bytes still queued.
				// Half-close first, then let the reader consume its framed response.
				shutdown(fd, SHUT_WR);
				u64 close_deadline = osGetTime() + 1000;
				char discard[64];
				while (osGetTime() < close_deadline) {
					int n = recv(fd, discard, sizeof(discard), 0);
					if (!n || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) break;
					svcSleepThread(2000000);
				}
				closesocket(fd);
			}
		}
		svcSleepThread(50000000); // 20 cheap nonblocking accepts/s, low priority
	}
	encode_shot(-1);
}
void devnet_init(void)
{
	LightLock_Init(&lock);
	if (dev_remote) {
		history = calloc(DEV_HISTORY, sizeof(*history));
		if (!history)
			goto fail;
		soc_buffer = memalign(0x1000, 0x40000);
		if (!soc_buffer || R_FAILED(socInit(soc_buffer, 0x40000)))
			goto fail;
		soc_owned = true;
		server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (server < 0)
			goto fail;
		nonblock(server);
		struct sockaddr_in addr = {
		    .sin_family = AF_INET, .sin_port = htons(5555), .sin_addr = {.s_addr = INADDR_ANY}};
		int yes = 1;
		setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
		if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) || listen(server, 2))
			goto fail;
	}
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	int core = g_syscore_pct > 0 ? 1 : 0;
	worker = threadCreate(network_main, NULL, 64 * 1024, prio + 8, core, false);
	if (!worker && core)
		worker = threadCreate(network_main, NULL, 64 * 1024, prio + 8, 0, false);
	if (!worker)
		goto fail;
	log_printf("developer: remote=%d port=5555 bench=%d\n", dev_remote, dev_bench);
	return;
fail:
	log_printf("developer: control worker init failed (errno %d)\n", errno);
	if (server >= 0)
		closesocket(server);
	server = -1;
	if (soc_owned)
		socExit();
	soc_owned = false;
	free(soc_buffer);
	soc_buffer = NULL;
	free(history);
	history = NULL;
}
void devnet_exit(void)
{
	if (worker) {
		// Drain a final response before closing the transport.
		for (int i = 0; i < 50; i++) {
			LightLock_Lock(&lock);
			int state = mailbox;
			LightLock_Unlock(&lock);
			if (!state)
				break;
			svcSleepThread(10000000);
		}
		LightLock_Lock(&lock);
		stopping = true;
		LightLock_Unlock(&lock);
		threadJoin(worker, U64_MAX);
		threadFree(worker);
		worker = NULL;
	}
	if (server >= 0)
		closesocket(server);
	if (soc_owned)
		socExit();
	free(soc_buffer);
	free(reply);
	free(shot_pixels);
	free(history);
}
