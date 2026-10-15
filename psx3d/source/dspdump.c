// Sound needs the DSP firmware at sdmc:/3ds/dspfirm.cdc, which can't be
// shipped (it's Nintendo's). Every 3DS has it inside the HOME Menu's code,
// so on first start we copy it from there, the way DSP1 does
// (https://github.com/zoogie/DSP1, MIT, (C) 2017 zoogie): read the HOME
// Menu's .code, decompress it, find the "DSP1" image and check its hashes.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <mbedtls/sha256.h>
#include "psx3d.h"

#define DSPFIRM "sdmc:/3ds/dspfirm.cdc"

static const u64 home_menus[] = {
	0x0004003000008F02ULL,   // USA
	0x0004003000008202ULL,   // JPN
	0x0004003000009802ULL,   // EUR
	0x000400300000A102ULL,   // CHN
	0x000400300000A902ULL,   // KOR
	0x000400300000B102ULL,   // TWN
};

static u32 le32(const u8 *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

// ExeFS .code compression (backwards LZSS), as in ctrtool
static bool lzss_decompress(const u8 *in, u32 in_size, u8 *out, u32 out_size)
{
	u32 top_bottom = le32(in + in_size - 8);
	u32 index = in_size - ((top_bottom >> 24) & 0xff);
	u32 stop = in_size - (top_bottom & 0xffffff);
	u32 pos = out_size;

	memset(out, 0, out_size);
	memcpy(out, in, in_size);
	while (index > stop) {
		u8 control = in[--index];
		for (int i = 0; i < 8 && index > stop && pos > 0; i++, control <<= 1) {
			if (control & 0x80) {
				if (index < 2)
					return false;
				index -= 2;
				u32 off = in[index] | (in[index + 1] << 8);
				u32 len = ((off >> 12) & 15) + 3;
				off = (off & 0xfff) + 2;
				if (pos < len)
					return false;
				for (u32 j = 0; j < len; j++) {
					if (pos + off >= out_size)
						return false;
					out[pos - 1] = out[pos + off];
					pos--;
				}
			} else {
				if (pos < 1)
					return false;
				out[--pos] = in[--index];
			}
		}
	}
	return true;
}

// the firmware lists up to 10 segments, each with a SHA-256
static bool hashes_ok(const u8 *firm, u32 size)
{
	const u8 *seg = firm + 0x120;
	for (int i = 0; i < 10; i++, seg += 0x30) {
		u32 off = le32(seg), len = le32(seg + 8);
		if (!len)
			break;
		if (off + len > size)
			return false;
		u8 sum[32];
		mbedtls_sha256(firm + off, len, sum, 0);
		if (memcmp(sum, seg + 0x10, 32))
			return false;
	}
	return true;
}

static bool dump_from(u64 tid)
{
	u32 archive_path[4] = { (u32)tid, (u32)(tid >> 32), MEDIATYPE_NAND, 0 };
	static const u32 code_path[5] = { 0, 0, 2, 0x646f632e, 0x65 };   // ExeFS ".code"
	Handle fh;
	Result rc = FSUSER_OpenFileDirectly(&fh, (FS_ArchiveID)0x2345678a,
		(FS_Path){ PATH_BINARY, sizeof(archive_path), archive_path },
		(FS_Path){ PATH_BINARY, sizeof(code_path), code_path }, FS_OPEN_READ, 0);
	if (R_FAILED(rc)) {
		log_printf("dsp: HOME Menu %016llx: %08lx\n", tid, rc);
		return false;
	}

	u64 size = 0;
	u32 got = 0;
	u8 *packed = NULL, *code = NULL;
	bool ok = false;
	if (R_SUCCEEDED(FSFILE_GetSize(fh, &size)) && size > 8 && size < 32 * 1024 * 1024 &&
	    (packed = malloc(size)) && R_SUCCEEDED(FSFILE_Read(fh, &got, 0, packed, size)) && got == size) {
		u32 code_size = le32(packed + size - 4) + (u32)size;
		code = malloc(code_size);
		if (code && lzss_decompress(packed, size, code, code_size)) {
			u8 *magic = memmem(code, code_size, "DSP1", 4);
			if (magic && magic - code >= 0x100) {
				u8 *firm = magic - 0x100;
				u32 firm_size = le32(magic + 4);
				if (firm_size > 0x300 && firm + firm_size <= code + code_size && hashes_ok(firm, firm_size)) {
					mkdir("sdmc:/3ds", 0777);
					FILE *f = fopen(DSPFIRM, "wb");
					if (f) {
						ok = fwrite(firm, 1, firm_size, f) == firm_size;
						fclose(f);
						if (!ok)
							remove(DSPFIRM);
					}
				}
			}
		}
		log_printf("dsp: HOME Menu %016llx: %s\n", tid, ok ? "firmware saved" : "no usable firmware");
	}
	FSFILE_Close(fh);
	free(packed);
	free(code);
	return ok;
}

// true when the firmware is there (already, or copied now)
bool dsp_firmware_ensure(void)
{
	struct stat st;
	if (stat(DSPFIRM, &st) == 0 && st.st_size > 0)
		return true;
	for (size_t i = 0; i < sizeof(home_menus) / sizeof(home_menus[0]); i++)
		if (dump_from(home_menus[i]))
			return true;
	return false;
}
