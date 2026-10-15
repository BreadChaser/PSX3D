#include <stdio.h>
#include <stdarg.h>
#include "psx3d.h"

static FILE *log_fp;

void log_init(void)
{
	log_fp = fopen(PSX3D_LOG, "w");
}

void log_printf(const char *fmt, ...)
{
	va_list ap;
	if (!log_fp)
		return;
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
}
