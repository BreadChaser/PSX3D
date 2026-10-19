// Debug-only, single-client TCP transport. SOC calls never run on the emulator
// thread. All sockets are nonblocking: libctru SOC shares an IPC session, so even
// a blocking select/poll in this thread could stall another subsystem.
#include "devnet.h"
#include "clip.h"
#include <dirent.h>
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
    "lum",       "top_lum",       "xfer_ms", "heap_ms",    "queue_ms",  "gap_ms", "front_run_ms", "front_present_ms", "audio_ms", "loop_ms", "pace_ms", "top_count", "bottom_count", "held_count", "ff", "spu_ms", "spu_wait_ms", "skip_level", "drawn_count", "skipped_count", "audio_underruns", "audio_drops", "audio_gap_ms", "pica_cpu_ms", "phase_list_ms", "phase_prim_ms", "phase_tex_ms", "phase_decode_ms", "phase_state_ms", "phase_submit_ms", "phase_read_ms", "phase_vertex_ms", "draw_mcycles", "skip_mcycles", "idle_mcycles", "draw_vblanks", "skip_vblanks", "idle_vblanks", "renderer_mcycles", "renderer_ev0_k", "renderer_ev1_k", "renderer_samples", "draw_ev0_k", "draw_ev1_k", "draw_spu_ms", "draw_spuwait_ms", "draw_pica_ms", "draw_audio_ms", "draw_dma_ms", "skip_ev0_k", "skip_ev1_k", "skip_spu_ms", "skip_spuwait_ms", "skip_pica_ms", "skip_audio_ms", "skip_dma_ms", "idle_ev0_k", "idle_ev1_k", "idle_spu_ms", "idle_spuwait_ms", "idle_pica_ms", "idle_audio_ms", "idle_dma_ms", "draw_fraction"};
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

static void serve_client(int fd)
{
	char line[DEV_COMMAND_MAX];
	size_t used = 0;
	u64 deadline = osGetTime() + 15000;
	while (used < sizeof(line) - 1 && !devnet_stopping()) {
		int n = recv(fd, line + used, sizeof(line) - 1 - used, 0);
		if (n > 0) {
			used += n;
			char *newline = memchr(line, '\n', used);
			if (newline) {
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
	} else if (!encode_shot(fd))
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
