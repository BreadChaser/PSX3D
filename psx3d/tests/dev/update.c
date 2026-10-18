// Actual updater, fake httpc/AM/FS. Proves preflight failures never open install.
#define PSX3D_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef unsigned long long u64;
typedef int Result;
typedef unsigned Handle;
typedef int httpcContext;
typedef int FS_Path;
typedef struct {
	u64 titleID;
} AM_TitleInfo;
#define PATH_LEN 512
#define PSX3D_DIR "sdmc:/3ds/PSX3D"
#define R_FAILED(x) ((x) < 0)
#define R_SUCCEEDED(x) ((x) >= 0)
#define HTTPC_METHOD_GET 0
#define HTTPC_KEEPALIVE_DISABLED 0
#define HTTPC_RESULTCODE_DOWNLOADPENDING -2
#define ARCHIVE_SDMC 0
#define PATH_EMPTY 0
#define PATH_ASCII 1
#define FS_OPEN_READ 1
#define MEDIATYPE_SD 1
static bool homebrew, fail_write, fail_finish;
static u64 running_id = 0x000400000C3D3D00ULL, cia_id = 0x000400000C3D3D00ULL;
static u32 offset;
static unsigned starts, cancels, finishes, am_inits;
static u8 fixture[0x5000];
bool envIsHomebrew(void) { return homebrew; }
Result APT_GetProgramID(u64 *id)
{
	*id = running_id;
	return 0;
}
bool devnet_stopping(void) { return false; }
Result httpcInit(unsigned size)
{
	(void)size;
	offset = 0;
	return 0;
}
void httpcExit(void) {}
Result httpcOpenContext(httpcContext *c, int method, const char *url, int proxy)
{
	(void)c;
	(void)method;
	(void)url;
	(void)proxy;
	return 0;
}
void httpcCloseContext(httpcContext *c) { (void)c; }
void httpcCancelConnection(httpcContext *c) { (void)c; }
void httpcSetKeepAlive(httpcContext *c, int mode)
{
	(void)c;
	(void)mode;
}
Result httpcBeginRequest(httpcContext *c)
{
	(void)c;
	return 0;
}
Result httpcGetResponseStatusCodeTimeout(httpcContext *c, u32 *code, u64 ns)
{
	(void)c;
	(void)ns;
	*code = 200;
	return 0;
}
Result httpcGetResponseHeader(httpcContext *c, const char *name, char *out, size_t n)
{
	(void)c;
	(void)name;
	snprintf(out, n, "%zu", sizeof(fixture));
	return 0;
}
Result httpcGetDownloadSizeState(httpcContext *c, u32 *done, u32 *size)
{
	(void)c;
	if (done)
		*done = offset;
	if (size)
		*size = sizeof(fixture);
	return 0;
}
Result httpcReceiveDataTimeout(httpcContext *c, u8 *out, u32 n, u64 ns)
{
	(void)c;
	(void)ns;
	assert(offset + n <= sizeof(fixture));
	memcpy(out, fixture + offset, n);
	offset += n;
	return offset == sizeof(fixture) ? 0 : HTTPC_RESULTCODE_DOWNLOADPENDING;
}
Result amInit(void)
{
	am_inits++;
	return 0;
}
void amExit(void) {}
FS_Path fsMakePath(int type, const char *path)
{
	(void)type;
	(void)path;
	return 0;
}
Result FSUSER_OpenFileDirectly(Handle *handle, int archive, FS_Path ap, FS_Path p, int flags, int attr)
{
	(void)archive;
	(void)ap;
	(void)p;
	(void)flags;
	(void)attr;
	*handle = 1;
	return 0;
}
Result AM_GetCiaFileInfo(int media, AM_TitleInfo *info, Handle h)
{
	(void)media;
	(void)h;
	info->titleID = cia_id;
	return 0;
}
Result AM_StartCiaInstall(int media, Handle *handle)
{
	assert(media == MEDIATYPE_SD);
	starts++;
	*handle = 2;
	return 0;
}
Result AM_CancelCIAInstall(Handle h)
{
	assert(h == 2);
	cancels++;
	return 0;
}
Result AM_FinishCiaInstall(Handle h)
{
	assert(h == 2);
	finishes++;
	return fail_finish ? -8 : 0;
}
Result FSFILE_Close(Handle h)
{
	(void)h;
	return 0;
}
Result FSFILE_Read(Handle h, u32 *done, u64 off, void *out, u32 n)
{
	assert(h == 1);
	memcpy(out, fixture + off, n);
	*done = n;
	return 0;
}
Result FSFILE_Write(Handle h, u32 *done, u64 off, const void *data, u32 n, int flags)
{
	assert(h == 2);
	(void)flags;
	assert(!memcmp(data, fixture + off, n));
	*done = fail_write ? 0 : n;
	return fail_write ? -7 : 0;
}
#include "../../source/devupdate.c"
static void reset(void)
{
	starts = cancels = finishes = am_inits = 0;
	fail_write = fail_finish = false;
}
int main(void)
{
	mkdir("sdmc:", 0777);
	mkdir("sdmc:/3ds", 0777);
	mkdir(PSX3D_DIR, 0777);
	memset(fixture, 'Z', sizeof(fixture));
	char result[512], args[512];
	assert(!dev_update("https://192.168.1.225/x", result, sizeof(result)));
	assert(!starts);
	assert(!dev_update("http://8.8.8.8/x", result, sizeof(result)));
	assert(!starts);
	assert(!dev_update("http://example.com/x", result, sizeof(result)));
	assert(!starts);
	homebrew = true;
	assert(!dev_update("http://192.168.1.225/x", result, sizeof(result)));
	homebrew = false;
	assert(!starts);
	assert(!dev_update("http://192.168.1.225/x size=99", result, sizeof(result)));
	assert(!starts && !am_inits);
	assert(!dev_update(
	    "http://192.168.1.225/x sha256=0000000000000000000000000000000000000000000000000000000000000000",
	    result, sizeof(result)));
	assert(!starts && !am_inits);
	assert(strstr(result, "SHA256 mismatch"));
	cia_id = 99;
	assert(!dev_update("http://192.168.1.225/x", result, sizeof(result)));
	assert(!starts);
	cia_id = SELF_TITLE;
	reset();
	fail_write = true;
	assert(!dev_update("http://192.168.1.225/x", result, sizeof(result)));
	assert(starts == 1 && cancels == 1 && !finishes);
	reset();
	fail_finish = true;
	assert(!dev_update("http://192.168.1.225/x", result, sizeof(result)));
	assert(starts == 1 && cancels == 1 && finishes == 1);
	reset();
	// Obtain a real SHA256 of the fixture with the same host crypto adapter.
	mbedtls_sha256_context hash;
	u8 digest[32];
	char hex[65];
	mbedtls_sha256_init(&hash);
	mbedtls_sha256_starts_ret(&hash, 0);
	mbedtls_sha256_update_ret(&hash, fixture, sizeof(fixture));
	mbedtls_sha256_finish_ret(&hash, digest);
	mbedtls_sha256_free(&hash);
	for (int i = 0; i < 32; i++)
		sprintf(hex + i * 2, "%02x", digest[i]);
	snprintf(args, sizeof(args), "http://192.168.1.225/x size=%zu sha256=%s", sizeof(fixture), hex);
	FILE *f = fopen(PSX3D_DIR "/switches.txt", "w");
	assert(f);
	fputs("remote token=secret update=http://192.168.1.225/x\n", f);
	fclose(f);
	assert(dev_update(args, result, sizeof(result)));
	assert(starts == 1 && finishes == 1 && !cancels);
	assert(strstr(result, "\"relaunch\":true"));
	assert(access(CIA_TEMP, F_OK));
	char text[256] = {0};
	f = fopen(PSX3D_DIR "/switches.txt", "r");
	assert(f);
	assert(fread(text, 1, sizeof(text) - 1, f));
	fclose(f);
	assert(strstr(text, "remote") && strstr(text, "token=secret") && !strstr(text, "update="));
	puts("updater LAN/size/hash/title guards, cancellation and successful AM order: PASS");
}
