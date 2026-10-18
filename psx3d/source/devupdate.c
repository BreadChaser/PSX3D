// LAN-only developer updater. Download and validate completely before opening an
// AM install handle; never delete a running title/ticket. No NAND destinations.
#include "devnet.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SELF_TITLE 0x000400000C3D3D00ULL
#define CIA_TEMP PSX3D_DIR "/update.cia.part"

static bool lan_url(const char *url)
{
	if (strncmp(url, "http://", 7))
		return false;
	char ip[32];
	size_t n = strcspn(url + 7, ":/");
	if (!n || n >= sizeof(ip))
		return false;
	memcpy(ip, url + 7, n);
	ip[n] = 0;
	struct in_addr a;
	if (inet_pton(AF_INET, ip, &a) != 1)
		return false;
	u32 x = ntohl(a.s_addr);
	// Numeric LAN addresses only; no DNS, redirects or public update endpoints.
	return (x >> 24) == 10 || (x >> 20) == 0xac1 || (x >> 16) == 0xc0a8;
}
static bool consume_switch_url(void)
{
	FILE *in = fopen(PSX3D_DIR "/switches.txt", "r");
	if (!in)
		return true; // remote-only invocation need not create a file
	// Most remote updates have no startup URL. Avoid rewriting their credentials.
	char word[512];
	bool has_update = false;
	while (fscanf(in, "%511s", word) == 1)
		if (!strncmp(word, "update=", 7)) has_update = true;
	if (!has_update) { bool ok = !ferror(in); fclose(in); return ok; }
	rewind(in);
	FILE *out = fopen(PSX3D_DIR "/switches.update.tmp", "w");
	if (!out) {
		fclose(in);
		return false;
	}
	bool ok = true;
	while (fscanf(in, "%511s", word) == 1)
		if (strncmp(word, "update=", 7) && fprintf(out, "%s\n", word) < 0)
			ok = false;
	if (ferror(in))
		ok = false;
	fclose(in);
	if (fclose(out))
		ok = false;
	if (ok) {
		// 3DS FS rename does not replace an existing FAT file.
		const char *path = PSX3D_DIR "/switches.txt", *backup = PSX3D_DIR "/switches.update.bak";
		remove(backup);
		ok = !rename(path, backup);
		if (ok && rename(PSX3D_DIR "/switches.update.tmp", path)) {
			rename(backup, path);
			ok = false;
		}
		if (ok) remove(backup);
	}
	return ok;
}
bool dev_update(const char *args, char *result, size_t capacity)
{
	bool ok = false, http = false, context = false, am = false, installing = false;
	const char *failure = "invalid update arguments";
	Result rc = 0;
	httpcContext ctx;
	FILE *file = NULL;
	Handle source = 0, destination = 0;
	u8 *buffer = NULL;
	char copy[1024], url[256] = "", expected_hash[65] = "", actual_hash[65];
	u32 expected_size = 0, total = 0;
	if (!dev_remote || !dev_token[0]) {
		failure = "self-update requires remote and token in switches.txt";
		goto done;
	}
	snprintf(copy, sizeof(copy), "%s", args);
	char *save, *w = strtok_r(copy, " ", &save);
	if (!w || strlen(w) >= sizeof(url))
		goto done;
	strcpy(url, w);
	for (w = strtok_r(NULL, " ", &save); w; w = strtok_r(NULL, " ", &save)) {
		if (!strncmp(w, "size=", 5)) {
			char *end;
			unsigned long n = strtoul(w + 5, &end, 10);
			if (*end || !n || n > 64 * 1024 * 1024)
				goto done;
			expected_size = n;
		} else if (!strncmp(w, "sha256=", 7) && strlen(w + 7) == 64) {
			for (int i = 0; i < 64; i++) {
				if (!isxdigit((unsigned char)w[i + 7]))
					goto done;
				expected_hash[i] = tolower((unsigned char)w[i + 7]);
			}
			expected_hash[64] = 0;
		} else
			goto done;
	}
	if (!lan_url(url)) {
		failure = "only numeric private-LAN HTTP URLs allowed";
		goto done;
	}
	if (envIsHomebrew()) {
		failure = "CIA self-update requires installed CIA; launch it once from HOME";
		goto done;
	}
	u64 self = 0;
	if (R_FAILED(rc = APT_GetProgramID(&self)) || self != SELF_TITLE) {
		failure = "running title is not PSX3D CIA";
		goto done;
	}
	if (!(buffer = malloc(16384))) {
		failure = "out of memory";
		goto done;
	}
	failure = "HTTP initialization failed";
	if (R_FAILED(rc = httpcInit(0x10000)))
		goto done;
	http = true;
	if (R_FAILED(rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 1)))
		goto done;
	context = true;
	httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);
	failure = "HTTP request failed";
	if (R_FAILED(rc = httpcBeginRequest(&ctx)))
		goto done;
	u32 status = 0;
	if (R_FAILED(rc = httpcGetResponseStatusCodeTimeout(&ctx, &status, 15000000000ULL)) || status != 200)
		goto done;
	char length[32];
	if (R_FAILED(rc = httpcGetResponseHeader(&ctx, "Content-Length", length, sizeof(length)))) {
		failure = "Content-Length required";
		goto done;
	}
	char *end;
	unsigned long n = strtoul(length, &end, 10);
	if (*end || n < 0x2020 || n > 64 * 1024 * 1024 || (expected_size && n != expected_size)) {
		failure = "CIA size mismatch or invalid length";
		goto done;
	}
	total = n;
	file = fopen(CIA_TEMP, "wb");
	if (!file) {
		failure = "cannot stage update on SD";
		goto done;
	}
	mbedtls_sha256_context hash;
	mbedtls_sha256_init(&hash);
	mbedtls_sha256_starts_ret(&hash, 0);
	u32 offset = 0;
	while (offset < total && !devnet_stopping()) {
		u32 chunk = total - offset;
		if (chunk > 16384)
			chunk = 16384;
		u32 before = 0, after = 0;
		rc = httpcGetDownloadSizeState(&ctx, &before, NULL);
		if (R_SUCCEEDED(rc))
			rc = httpcReceiveDataTimeout(&ctx, buffer, chunk, 15000000000ULL);
		if (R_FAILED(rc) && rc != HTTPC_RESULTCODE_DOWNLOADPENDING)
			break;
		rc = httpcGetDownloadSizeState(&ctx, &after, NULL);
		if (R_FAILED(rc) || after - before != chunk) {
			rc = -1;
			break;
		}
		if (fwrite(buffer, 1, chunk, file) != chunk) {
			rc = -1;
			break;
		}
		mbedtls_sha256_update_ret(&hash, buffer, chunk);
		offset += chunk;
	}
	u8 digest[32];
	mbedtls_sha256_finish_ret(&hash, digest);
	mbedtls_sha256_free(&hash);
	for (int i = 0; i < 32; i++)
		sprintf(actual_hash + i * 2, "%02x", digest[i]);
	int close_result = fclose(file);
	file = NULL;
	if (offset != total || R_FAILED(rc) || close_result) {
		failure = "download incomplete or SD write failed";
		goto done;
	}
	if (expected_hash[0] && strcmp(expected_hash, actual_hash)) {
		failure = "SHA256 mismatch; title untouched";
		goto done;
	}
	httpcCloseContext(&ctx);
	context = false;
	httpcExit();
	http = false;
	failure = "AM service unavailable";
	if (R_FAILED(rc = amInit()))
		goto done;
	am = true;
	failure = "cannot open staged CIA";
	if (R_FAILED(rc = FSUSER_OpenFileDirectly(&source, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
	                                          fsMakePath(PATH_ASCII, "/3ds/PSX3D/update.cia.part"),
	                                          FS_OPEN_READ, 0)))
		goto done;
	AM_TitleInfo info;
	failure = "CIA title ID mismatch; title untouched";
	if (R_FAILED(rc = AM_GetCiaFileInfo(MEDIATYPE_SD, &info, source)) || info.titleID != SELF_TITLE)
		goto done;
	// Consume any startup update= before installing so relaunch cannot loop.
	if (!consume_switch_url()) {
		failure = "cannot consume update switch; title untouched";
		goto done;
	}
	failure = "AM_StartCiaInstall failed; use FBI recovery";
	if (R_FAILED(rc = AM_StartCiaInstall(MEDIATYPE_SD, &destination)))
		goto done;
	installing = true;
	for (offset = 0; offset < total;) {
		if (devnet_stopping()) {
			failure = "install cancelled on exit";
			goto done;
		}
		u32 chunk = total - offset, read = 0, written = 0;
		if (chunk > 16384)
			chunk = 16384;
		failure = "CIA install read/write failed; use FBI recovery";
		if (R_FAILED(rc = FSFILE_Read(source, &read, offset, buffer, chunk)) || read != chunk ||
		    R_FAILED(rc = FSFILE_Write(destination, &written, offset, buffer, chunk, 0)) || written != chunk)
			goto done;
		offset += chunk;
	}
	failure = "AM_FinishCiaInstall failed; use FBI recovery";
	rc = AM_FinishCiaInstall(destination);
	if (R_FAILED(rc))
		goto done;
	installing = false;
	ok = true;
done:
	if (installing)
		AM_CancelCIAInstall(destination);
	if (source)
		FSFILE_Close(source);
	if (am)
		amExit();
	if (file)
		fclose(file);
	if (context) {
		httpcCancelConnection(&ctx);
		httpcCloseContext(&ctx);
	}
	if (http)
		httpcExit();
	free(buffer);
	remove(CIA_TEMP);
	if (ok)
		snprintf(result, capacity,
		         "{\"ok\":true,\"installed\":true,\"relaunch\":true,\"size\":%lu,\"sha256\":\"%s\"}",
		         (unsigned long)total, actual_hash);
	else
		snprintf(result, capacity, "{\"ok\":false,\"error\":\"%s\",\"result\":\"%08lx\"}", failure,
		         (unsigned long)rc);
	return ok;
}
