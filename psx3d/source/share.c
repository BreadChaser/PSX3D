// Settings > About (experimental): share PSX3D itself. The bottom screen shows
// a QR code for FBI's "Scan QR code": the official release CIA, or, while
// "Share from this 3DS" is on, this console serving its own copy of the
// PSX3D CIA over the local Wi-Fi (HTTP, LAN only). Only that one file is ever
// served, and only after AM confirms it is a PSX3D CIA: never games or saves.
#include "psx3d.h"
#include "devtools.h"
#include "qrcode.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define RELEASE_URL "https://github.com/BreadChaser/PSX3D/releases/latest/download/PSX3D.cia"
#define SELF_TITLE 0x000400000C3D3D00ULL
#define SHARE_PORT 8080

// A kept copy first (a future self-update can leave its download here), then
// the folders people usually put a CIA in before installing it with FBI.
static const char *const cia_paths[] = {
	PSX3D_DIR "/PSX3D.cia", "sdmc:/PSX3D.cia", "sdmc:/cias/PSX3D.cia", "sdmc:/cia/PSX3D.cia",
	"sdmc:/CIA/PSX3D.cia", "sdmc:/3ds/PSX3D.cia", "sdmc:/Downloads/PSX3D.cia"};

static char cia_path[64], cia_note[96], url[64];
static u64 cia_size;
static volatile bool serving, server_stop;
static volatile u32 served_count;
static Thread server_thread;
static int listen_fd = -1;

// Title version as the Makefile builds it: (major*16+minor)<<10 | patch+3.
static const char *version_text(u16 v)
{
	static char s[16];
	int p = (v & 15) - 3;
	snprintf(s, sizeof(s), "%d.%d.%d", (v >> 10) / 16, (v >> 10) % 16, p < 0 ? 0 : p);
	return s;
}

// AM reads the CIA's own title ID and version: only a real PSX3D CIA is served.
static bool find_cia(void)
{
	cia_path[0] = 0;
	snprintf(cia_note, sizeof(cia_note), "No PSX3D.cia on the SD card to share");
	if (R_FAILED(amInit()))
		return false;
	AM_TitleEntry self = {0};
	u64 tid = SELF_TITLE;
	bool have_self = R_SUCCEEDED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &tid, &self));
	for (unsigned i = 0; i < sizeof(cia_paths) / sizeof(cia_paths[0]) && !cia_path[0]; i++) {
		Handle h;
		if (R_FAILED(FSUSER_OpenFileDirectly(&h, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
		                                     fsMakePath(PATH_ASCII, cia_paths[i] + 5), FS_OPEN_READ, 0)))
			continue;
		AM_TitleEntry info;
		u64 bytes = 0;
		if (R_SUCCEEDED(AM_GetCiaFileInfo(MEDIATYPE_SD, &info, h)) && info.titleID == SELF_TITLE &&
		    R_SUCCEEDED(FSFILE_GetSize(h, &bytes)) && bytes) {
			snprintf(cia_path, sizeof(cia_path), "%s", cia_paths[i]);
			cia_size = bytes;
			snprintf(cia_note, sizeof(cia_note), "PSX3D %s%s", version_text(info.version),
			         have_self && info.version != self.version ? " (not the version installed here)" : "");
		}
		FSFILE_Close(h);
	}
	amExit();
	return cia_path[0] != 0;
}

static bool lan_ok(const struct sockaddr_in *a, u32 *self_ip)
{
	SOCU_IPInfo ip;
	socklen_t n = sizeof(ip);
	if (SOCU_GetNetworkOpt(SOL_CONFIG, NETOPT_IP_INFO, &ip, &n))
		return false;
	if (self_ip)
		*self_ip = ip.ip.s_addr;
	return !a || (a->sin_addr.s_addr & ip.netmask.s_addr) == (ip.ip.s_addr & ip.netmask.s_addr);
}

static bool send_all(int fd, const void *b, size_t n)
{
	const u8 *p = b;
	u64 deadline = osGetTime() + 20000;
	while (n && !server_stop) {
		int r = send(fd, p, n > 16384 ? 16384 : n, 0);
		if (r > 0) {
			p += r;
			n -= r;
			deadline = osGetTime() + 20000;
		} else if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
			return false;
		else if (osGetTime() > deadline)
			return false;
		else
			svcSleepThread(1000000);
	}
	return !n;
}

static void serve(int fd)
{
	char req[1024];
	size_t used = 0;
	req[0] = 0;
	u64 deadline = osGetTime() + 10000;
	while (used < sizeof(req) - 1 && !strstr(req, "\r\n\r\n") && osGetTime() < deadline && !server_stop) {
		int r = recv(fd, req + used, sizeof(req) - 1 - used, 0);
		if (r > 0) {
			used += r;
			req[used] = 0;
		} else if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
			return;
		else
			svcSleepThread(2000000);
	}
	bool head = !strncmp(req, "HEAD ", 5);
	bool ok = (head || !strncmp(req, "GET ", 4)) && !strncmp(req + (head ? 5 : 4), "/PSX3D.cia ", 11);
	FILE *f = ok ? fopen(cia_path, "rb") : NULL;
	if (!f) {
		static const char nf[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		send_all(fd, nf, sizeof(nf) - 1);
		return;
	}
	char hdr[256];
	int n = snprintf(hdr, sizeof(hdr),
	                 "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %llu\r\n"
	                 "Content-Disposition: attachment; filename=\"PSX3D.cia\"\r\nConnection: close\r\n\r\n",
	                 (unsigned long long)cia_size);
	bool good = send_all(fd, hdr, n);
	static u8 buf[32768];
	u64 left = head ? 0 : cia_size;
	while (good && left) {
		size_t chunk = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
		good = fread(buf, 1, chunk, f) == chunk && send_all(fd, buf, chunk);
		left -= chunk;
	}
	fclose(f);
	if (good && !head)
		served_count++;
	log_printf("share: %s %s\n", head ? "HEAD" : "GET", good ? "sent" : "failed");
}

static void server_main(void *unused)
{
	(void)unused;
	while (!server_stop) {
		struct sockaddr_in peer;
		socklen_t n = sizeof(peer);
		int fd = accept(listen_fd, (struct sockaddr *)&peer, &n);
		if (fd >= 0) {
			fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
			if (lan_ok(&peer, NULL))
				serve(fd);
			shutdown(fd, SHUT_WR);
			u64 end = osGetTime() + 1000;
			char discard[64];
			while (osGetTime() < end) {   // let the reader drain, then close
				int r = recv(fd, discard, sizeof(discard), 0);
				if (!r || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
					break;
				svcSleepThread(2000000);
			}
			close(fd);
		}
		svcSleepThread(50000000);
	}
}

static u32 *soc_own;
static bool share_start(void)
{
	if (!find_cia())
		return false;
	int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0 && !soc_own) {
		soc_own = memalign(0x1000, 0x40000);
		if (soc_own && R_FAILED(socInit(soc_own, 0x40000))) {
			free(soc_own);
			soc_own = NULL;
		}
		s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	}
	u32 ip = 0;
	if (s < 0 || !lan_ok(NULL, &ip) || !ip) {
		if (s >= 0)
			close(s);
		snprintf(cia_note, sizeof(cia_note), "No Wi-Fi connection");
		return false;
	}
	fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
	int yes = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(SHARE_PORT), .sin_addr = {INADDR_ANY}};
	if (bind(s, (struct sockaddr *)&a, sizeof(a)) || listen(s, 2)) {
		close(s);
		snprintf(cia_note, sizeof(cia_note), "Port %d is busy", SHARE_PORT);
		return false;
	}
	listen_fd = s;
	struct in_addr in = {ip};
	snprintf(url, sizeof(url), "http://%s:%d/PSX3D.cia", inet_ntoa(in), SHARE_PORT);
	server_stop = false;
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	server_thread = threadCreate(server_main, NULL, 16 * 1024, prio + 4, g_syscore_pct > 0 ? 1 : 0, false);
	if (!server_thread) {
		close(s);
		listen_fd = -1;
		return false;
	}
	serving = true;
	log_printf("share: serving %s (%llu bytes) at %s\n", cia_path, (unsigned long long)cia_size, url);
	return true;
}

static void share_stop(void)
{
	if (!serving)
		return;
	server_stop = true;
	threadJoin(server_thread, U64_MAX);
	threadFree(server_thread);
	server_thread = NULL;
	close(listen_fd);
	listen_fd = -1;
	serving = false;
}

static uint8_t qr[QR_MAX_SIZE * QR_MAX_SIZE];
static int qr_size;
static char qr_text[96];
static bool in_game, share_failed;

static void draw_top(void *ud)
{
	(void)ud;
	u32 white = C2D_Color32(235, 238, 255, 255), dim = C2D_Color32(150, 160, 190, 255);
	C2D_DrawRectSolid(0, 0, .1f, 400, 240, C2D_Color32(6, 9, 24, 255));
	ui_text(20, 14, .7f, white, "PSX3D %s", PSX3D_VERSION);
	ui_text(20, 42, .45f, dim, "PS1 emulator for 3DS. Free software (GPL).");
	ui_text(20, 70, .5f, white, "Share PSX3D with a friend:");
	ui_text(20, 92, .45f, dim, "On their 3DS open FBI > Remote Install >");
	ui_text(20, 110, .45f, dim, "Scan QR Code, and scan the code below.");
	if (serving) {
		ui_text(20, 138, .45f, white, "From this 3DS (same Wi-Fi only):");
		ui_text(20, 156, .42f, dim, "%s", url);
		ui_text(20, 174, .42f, dim, "%s   Sent: %lu", cia_note, served_count);
	} else {
		ui_text(20, 138, .45f, white, "Official release (needs internet):");
		ui_text(20, 156, .38f, dim, "github.com/BreadChaser/PSX3D/releases");
		if (share_failed)
			ui_text(20, 174, .42f, C2D_Color32(255, 200, 120, 255), "%s", cia_note);
	}
	ui_text(20, 212, .42f, dim, in_game ? "B back" : serving ? "X stop sharing    B back" : "X share from this 3DS    B back");
}

static void draw_bottom(void *ud)
{
	(void)ud;
	C2D_DrawRectSolid(0, 0, .1f, 320, 240, C2D_Color32(255, 255, 255, 255));
	if (!qr_size)
		return;
	float px = (float)(int)(228.0f / (qr_size + 6));
	float x0 = (320 - px * qr_size) / 2, y0 = (240 - px * qr_size) / 2;
	u32 black = C2D_Color32(0, 0, 0, 255);
	for (int y = 0; y < qr_size; y++)
		for (int x = 0; x < qr_size;) {
			if (!qr[y * qr_size + x]) {
				x++;
				continue;
			}
			int run = 1;
			while (x + run < qr_size && qr[y * qr_size + x + run])
				run++;
			C2D_DrawRectSolid(x0 + x * px, y0 + y * px, .5f, run * px, px, black);
			x += run;
		}
}

static void make_qr(const char *text)
{
	if (strcmp(qr_text, text)) {
		snprintf(qr_text, sizeof(qr_text), "%s", text);
		if (!qr_encode(qr_text, qr, &qr_size))
			qr_size = 0;
	}
}

// Runs until B. Sharing is offered in the library only (no SD reads that
// could compete with a running game's disc).
void share_about_run(bool game)
{
	in_game = game;
	share_failed = false;
	cia_note[0] = 0;
	while (aptMainLoop()) {
		if (dev_tick(game) || dev_ui_pending())
			break;
		input_poll();
		u32 k = input_keys_down();
		if (k & KEY_B)
			break;
		if ((k & KEY_X) && !game) {
			if (serving)
				share_stop();
			else
				share_failed = !share_start();
		}
		make_qr(serving ? url : RELEASE_URL);
		ui_begin();
		video_present_vsync(game, draw_top, draw_bottom, NULL);
	}
	share_stop();
}

// Prototype: only on consoles with the developer remote switched on.
bool share_about_visible(void) { return dev_enabled(); }
