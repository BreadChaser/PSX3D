#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include "psx3d.h"

#include <malloc.h>
#include <3ds/3dslink.h>

static FILE *log_fp;
static aptHookCookie log_cookie;
static bool log_held;

// A buffer that holds a whole stats report, so log_hold() makes it one write.
static FILE *log_open(const char *mode)
{
	FILE *f = fopen(PSX3D_LOG, mode);
	if (f)
		setvbuf(f, NULL, _IOFBF, 4096);
	return f;
}

// Close the log while suspended (HOME, sleep): an SD file held open across
// the handoff left the HOME Menu waiting forever. Reopen on return.
static void log_apt(APT_HookType hook, void *param)
{
	(void)param;
	if ((hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP || hook == APTHOOK_ONEXIT) && log_fp) {
		fclose(log_fp);
		log_fp = NULL;
	} else if ((hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP) && !log_fp) {
		log_fp = log_open("a");
	}
}
// The HOME Menu's Close wakes us without the restore hook, so the log would
// stay shut for the exit. log_apt closes it again at exit.
void log_resume(void)
{
	if (!log_fp)
		log_fp = log_open("a");
}

// Netloaded with 3dslink: the log is also sent to the PC. Never block on
// it: if the PC stops reading, a blocking send froze the whole game.
static int link_fd = -1;
static u32 *soc_buf;

void log_init(void)
{
	// Keep the last five sessions (log.txt newest, log.4.txt oldest), so the log
	// of a crashed session survives the relaunch. Renames only: cheap at startup.
	// FAT rename does not replace a file, so drop the oldest first.
	remove(PSX3D_DIR "/log.4.txt");
	rename(PSX3D_DIR "/log.3.txt", PSX3D_DIR "/log.4.txt");
	rename(PSX3D_DIR "/log.2.txt", PSX3D_DIR "/log.3.txt");
	rename(PSX3D_DIR "/log.1.txt", PSX3D_DIR "/log.2.txt");
	rename(PSX3D_LOG, PSX3D_DIR "/log.1.txt");
	log_fp = log_open("w");
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
	if (!log_held)
		fflush(log_fp);
}

// The once-a-second stats report is ~8 lines. Flushing each one is a separate
// SD write on the main thread: together ~66 ms, which is more than the frame
// pacer catches up, so Silent Hill lost 4 frames every second (93%). Hold the
// flushes during the report and write it once.
void log_hold(bool hold)
{
	log_held = hold;
	if (!hold && log_fp)
		fflush(log_fp);
}

// true when the log also goes to a PC (3dslink): cheap to send often
bool log_linked(void) { return link_fd >= 0; }

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
