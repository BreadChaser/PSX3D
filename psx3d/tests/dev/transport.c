// Run the real devnet worker against loopback with host substitutes for libctru.
#define PSX3D_H
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef unsigned long long u64;
typedef pthread_mutex_t LightLock;
typedef struct {
	pthread_t id;
	void (*entry)(void *);
	void *arg;
} *Thread;
#define PATH_LEN 512
#define PSX3D_DIR "sdmc:/3ds/PSX3D"
#define PSX3D_STATE_DIR PSX3D_DIR "/states"
#define CUR_THREAD_HANDLE 0
#define U64_MAX UINT64_MAX
#define R_FAILED(x) ((x) < 0)
#define R_SUCCEEDED(x) ((x) >= 0)
#define SOL_CONFIG 1
#define NETOPT_IP_INFO 1
#define GFX_TOP 0
#define GFX_BOTTOM 1
#define GFX_LEFT 0
typedef int gfxScreen_t;
typedef struct {
	struct in_addr ip, netmask, broadcast;
} SOCU_IPInfo;
void LightLock_Init(LightLock *l)
{
	pthread_mutex_init(l, NULL);
}
void LightLock_Lock(LightLock *l)
{
	pthread_mutex_lock(l);
}
void LightLock_Unlock(LightLock *l)
{
	pthread_mutex_unlock(l);
}
u64 osGetTime(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000ULL + t.tv_nsec / 1000000;
}
void svcSleepThread(u64 ns)
{
	struct timespec t = {ns / 1000000000, ns % 1000000000};
	nanosleep(&t, NULL);
}
void svcGetThreadPriority(s32 *p, int handle)
{
	(void)handle;
	*p = 32;
}
void *trampoline(void *arg)
{
	Thread t = arg;
	t->entry(t->arg);
	return NULL;
}
Thread threadCreate(void (*entry)(void *), void *arg, size_t stack, int priority, int core, bool detach)
{
	(void)stack;
	(void)priority;
	(void)core;
	(void)detach;
	Thread t = calloc(1, sizeof(*t));
	t->entry = entry;
	t->arg = arg;
	if (pthread_create(&t->id, NULL, trampoline, t)) {
		free(t);
		return NULL;
	}
	return t;
}
void threadJoin(Thread t, u64 ns)
{
	(void)ns;
	pthread_join(t->id, NULL);
}
void threadFree(Thread t)
{
	free(t);
}
void log_printf(const char *fmt, ...)
{
	(void)fmt;
}
int socInit(void *buf, size_t size)
{
	(void)buf;
	(void)size;
	return 0;
}
void socExit(void) {}
int SOCU_GetNetworkOpt(int level, int opt, void *value, socklen_t *len)
{
	(void)level;
	(void)opt;
	(void)len;
	SOCU_IPInfo *ip = value;
	ip->ip.s_addr = inet_addr("127.0.0.1");
	ip->netmask.s_addr = inet_addr("255.0.0.0");
	return 0;
}
int test_bind(int fd, const struct sockaddr *addr, socklen_t len)
{
	struct sockaddr_in ip = *(const struct sockaddr_in *)addr;
	ip.sin_addr.s_addr = inet_addr("127.0.0.1");
	ip.sin_port = 0;
	return bind(fd, (struct sockaddr *)&ip, len);
}
#define bind test_bind
#define closesocket close
void C3D_FrameSync(void) {}
void gfxSetDoubleBuffering(int screen, bool enabled)
{
	(void)screen;
	(void)enabled;
}
u8 *gfxGetFramebuffer(int screen, int eye, u16 *rows, u16 *cols)
{
	(void)eye;
	static u8 fb[400 * 240 * 3];
	*rows = 240;
	*cols = screen ? 320 : 400;
	for (size_t i = 0; i < sizeof(fb); i += 3) {
		fb[i] = 0;
		fb[i + 1] = 128;
		fb[i + 2] = 255;
	}
	return fb;
}
void GSPGPU_InvalidateDataCache(void *buf, size_t n)
{
	(void)buf;
	(void)n;
}
int g_syscore_pct = 0;
bool dev_remote = true, dev_bench = false;
char dev_token[96] = "test-token", dev_update_url[256];
bool dev_update(const char *args, char *out, size_t n)
{
	(void)args;
	snprintf(out, n, "{\"ok\":false,\"error\":\"mock update rejected\"}");
	return false;
}
int archive_getmtime(const char *name, u64 *mtime)
{
	struct stat st;
	if (stat(name, &st))
		return -1;
	*mtime = st.st_mtime;
	return 0;
}
#include "../../source/clip.h"
#include "../../source/devnet.c"
int main(void)
{
    mkdir("sdmc:",0777); mkdir("sdmc:/3ds",0777); mkdir(PSX3D_DIR,0777);
    mkdir(PSX3D_DIR "/recordings",0777); mkdir(PSX3D_DIR "/recordings/Crash",0777);
    FILE *clip=fopen(PSX3D_DIR "/recordings/Crash/1.inp","wb");
    if (!clip) return 1;
    fputs("input fixture",clip); fclose(clip);
    clip=fopen(PSX3D_DIR "/recordings/Crash/1.state","wb");
    if (!clip) return 1;
    char chunk[8192]; memset(chunk,42,sizeof(chunk));
    for (int k=0;k<544;k++) fwrite(chunk,1,sizeof(chunk),clip);
    fclose(clip);
	/* File API fixtures: states, Luma dumps, rotated logs, a large file. */
	mkdir(PSX3D_STATE_DIR, 0777);
	FILE *f = fopen(PSX3D_STATE_DIR "/Silent Hill (USA).st2", "wb");
	for (int k = 0; f && k < 544; k++) fwrite(chunk, 1, sizeof(chunk), f);
	if (f) fclose(f);
	f = fopen(PSX3D_STATE_DIR "/Silent Hill (USA).st2.thm", "wb");
	u32 thm[3] = {0x48543350, 256 | 192 << 16, 1792400000};
	if (f) { fwrite(thm, 1, sizeof(thm), f); fclose(f); }
	f = fopen(PSX3D_STATE_DIR "/Silent Hill (USA).sta", "wb");
	if (f) { fputs("auto", f); fclose(f); }
	mkdir("sdmc:/luma", 0777); mkdir("sdmc:/luma/dumps", 0777); mkdir("sdmc:/luma/dumps/arm11", 0777);
	f = fopen("sdmc:/luma/dumps/arm11/crash_dump_00000004.dmp", "wb");
	if (f) { fputs("DUMP", f); fclose(f); }
	f = fopen(PSX3D_DIR "/log.txt", "w"); if (f) { fputs("now\n", f); fclose(f); }
	f = fopen(PSX3D_DIR "/log.1.txt", "w"); if (f) { fputs("crashed\n", f); fclose(f); }
	f = fopen(PSX3D_DIR "/config.ini", "w"); if (f) { fputs("old=1\n", f); fclose(f); }
	mkdir("sdmc:/roms", 0777);
	f = fopen("sdmc:/roms/big.bin", "wb");
	for (int k = 0; f && k < 2048; k++) fwrite(chunk, 1, sizeof(chunk), f);
	if (f) fclose(f);
	devnet_init();
	if (server < 0 || !worker)
		return 1;
	struct sockaddr_in addr;
	socklen_t n = sizeof(addr);
	getsockname(server, (struct sockaddr *)&addr, &n);
	printf("%u\n", ntohs(addr.sin_port));
	fflush(stdout);
	devnet_status("{\"ok\":true,\"game\":\"Crash\"}");
	for (int i = 0; i < 65; i++) {
		dev_measurement m = {.seq = i + 1, .ms = i * 1000};
		m.v[0] = 59.5;
		m.v[6] = -1;
		strcpy(m.audio, "audio \"quoted\"\nline");
		devnet_sample(&m, false);
	}
	while (true) {
		char command[DEV_COMMAND_MAX];
		if (devnet_take(command)) {
			if (!strcmp(command, "shot both")) {
				devnet_shot(NULL, true);
				devnet_reply("{\"ok\":true}");
			} else if (!strcmp(command, "busy on")) {
				devnet_busy(true);
				devnet_reply("{\"ok\":true}");
			} else if (!strcmp(command, "busy off")) {
				devnet_busy(false);
				devnet_reply("{\"ok\":true}");
			} else if (!strcmp(command, "update mock"))
				devnet_update_reply("mock");
			else if (!strcmp(command, "_update_failed"))
				devnet_notification_done();
			else if (!strcmp(command, "exit")) {
				devnet_reply("{\"ok\":true}");
				break;
			} else
				devnet_reply("{\"ok\":true,\"applied_on_main\":true}");
		}
		svcSleepThread(1000000);
	}
	devnet_exit();
	return 0;
}
