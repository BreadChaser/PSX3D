#include <stdio.h>
#include <stdarg.h>
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
static bool log_stdout;
static u32 *soc_buf;

void log_init(void)
{
	log_fp = fopen(PSX3D_LOG, "w");
	aptHook(&log_cookie, log_apt, NULL);

	// launched from a PC with 3dslink: mirror the log to its console (3dslink -s)
	if (__3dslink_host.s_addr) {
		soc_buf = memalign(0x1000, 0x100000);
		if (soc_buf && R_SUCCEEDED(socInit(soc_buf, 0x100000)) && link3dsStdio() >= 0)
			log_stdout = true;
	}
}

void log_printf(const char *fmt, ...)
{
	va_list ap;
	unsigned ms = (unsigned)(osGetTime() % 1000000);   // stamp: seconds.millis
	if (log_stdout) {
		printf("%3u.%03u ", ms / 1000, ms % 1000);
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
		fflush(stdout);
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
	if (log_stdout)
		socExit();
	log_stdout = false;
}
