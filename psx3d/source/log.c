#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include "psx3d.h"

#include <malloc.h>
#include <3ds/3dslink.h>

static FILE *log_fp;
static aptHookCookie log_cookie;

// Close the log while suspended (HOME, sleep): an SD file held open across
// the handoff left the HOME Menu waiting forever. Reopen on return.
static void log_apt(APT_HookType hook, void *param)
{
	(void)param;
	if ((hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP || hook == APTHOOK_ONEXIT) && log_fp) {
		fclose(log_fp);
		log_fp = NULL;
	} else if ((hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP) && !log_fp) {
		log_fp = fopen(PSX3D_LOG, "a");
	}
}
// Netloaded with 3dslink: the log is also sent to the PC. Never block on
// it: if the PC stops reading, a blocking send froze the whole game.
static int link_fd = -1;
static u32 *soc_buf;

void log_init(void)
{
	log_fp = fopen(PSX3D_LOG, "w");
	aptHook(&log_cookie, log_apt, NULL);

	// launched from a PC with 3dslink: mirror the log to its console (3dslink -s)
	if (__3dslink_host.s_addr) {
		soc_buf = memalign(0x1000, 0x100000);
		if (soc_buf && R_SUCCEEDED(socInit(soc_buf, 0x100000)))
			link_fd = link3dsConnectToHost(false, false);
	}
}

void log_printf(const char *fmt, ...)
{
	va_list ap;
	unsigned ms = (unsigned)(osGetTime() % 1000000);   // stamp: seconds.millis
	if (link_fd >= 0) {
		char line[512];
		int n = snprintf(line, sizeof(line), "%3u.%03u ", ms / 1000, ms % 1000);
		va_start(ap, fmt);
		vsnprintf(line + n, sizeof(line) - n, fmt, ap);
		va_end(ap);
		send(link_fd, line, strlen(line), MSG_DONTWAIT);   // dropped if the PC is behind
	}
	if (!log_fp)
		return;
	fprintf(log_fp, "%3u.%03u ", ms / 1000, ms % 1000);
	va_start(ap, fmt);
	vfprintf(log_fp, fmt, ap);
	va_end(ap);
	// flush every line so the log survives a crash
	fflush(log_fp);
}

void log_close(void)
{
	if (log_fp)
		fclose(log_fp);
	log_fp = NULL;
	if (link_fd >= 0) {
		closesocket(link_fd);
		socExit();
	}
	link_fd = -1;
}
