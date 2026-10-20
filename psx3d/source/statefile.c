// Save state streams (harsh review C2). The core reads and writes a state
// through its SaveFuncs table; the libretro glue's own functions know only a
// buffer and a position, so a damaged state could make it read or write past
// the buffer. PSX3D puts its own functions in that table: they know the
// buffer's length, refuse any access outside it before touching memory and
// remember the refusal. retro_serialize/retro_unserialize are wrapped at the
// final link: they honour the size they are given, and a state is checked
// before the core changes anything, so a damaged state leaves the running
// game as it was. The core's sources stay as they are (its code layout too).
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/libpcsxcore/psxcommon.h"
#include "../../core/libpcsxcore/r3000a.h"
#include "../../core/libpcsxcore/gpu.h"
#include "../../core/include/psemu_plugin_defs.h"
#include "statefile.h"

bool __real_retro_serialize(void *data, size_t size);
bool __real_retro_unserialize(const void *data, size_t size);

// The state layout before the SPU block, as misc.c SaveState writes it:
// header, version, HLE flag, the old screenshot space, RAM, BIOS, hardware,
// CPU registers, GPU header and VRAM; then the SPU block's own size.
#define STATE_HEAD (32 + sizeof(u32) + sizeof(boolean))
#define EX_SCREENPIC_SIZE (128 * 96 * 3)
#define SPU_SIZE_AT (STATE_HEAD + EX_SCREENPIC_SIZE + 0x200000 + 0x80000 + 0x10000 + \
	offsetof(psxRegisters, gteBusyCycle) + sizeof(GPUFreeze_t) + 1024 * 512 * 2)
#define SPU_MIN (sizeof(SPUFreeze_t) + 512 * 1024)   // must be larger
#define SPU_MAX (SPU_MIN + SPUFREEZE_F2_MAX_SIZE)

state_result_t state_last_result;

struct stream {
	char *buf;
	size_t pos, len;   // pos <= len always
	bool write, refused;
};
static size_t stream_len;      // the buffer given to retro_(un)serialize
static bool stream_refused;    // the last stream refused an access

static void *s_open(const char *name, const char *mode)
{
	if (!name || !mode)
		return NULL;
	struct stream *s = malloc(sizeof(*s));
	if (!s)
		return NULL;
	*s = (struct stream){ (char *)name, 0, stream_len, mode[0] == 'w' || mode[1] == 'w', false };
	return s;
}

static int s_read(void *file, void *buf, u32 len)
{
	struct stream *s = file;
	if (!s || !buf)
		return -1;
	if (s->refused || len > s->len - s->pos) {
		s->refused = true;
		return -1;
	}
	memcpy(buf, s->buf + s->pos, len);
	s->pos += len;
	return len;
}

static int s_write(void *file, const void *buf, u32 len)
{
	struct stream *s = file;
	if (!s || !buf)
		return -1;
	if (s->refused || !s->write || len > s->len - s->pos) {
		s->refused = true;
		return -1;
	}
	memcpy(s->buf + s->pos, buf, len);
	s->pos += len;
	return len;
}

static long s_seek(void *file, long offs, int whence)
{
	struct stream *s = file;
	if (!s)
		return -1;
	size_t base = whence == SEEK_CUR ? s->pos : 0;
	if (s->refused || (whence != SEEK_CUR && whence != SEEK_SET) ||
	    (offs < 0 ? (size_t)-(offs + 1) >= base : (size_t)offs > s->len - base)) {
		s->refused = true;
		return -1;
	}
	s->pos = base + offs;
	return (long)s->pos;
}

static void s_close(void *file)
{
	struct stream *s = file;
	if (!s)
		return;
	stream_refused = s->refused;
	if (s->write && !s->refused)   // no stale bytes in the unused space
		memset(s->buf + s->pos, 0, s->len - s->pos);
	free(s);
}

void state_stream_install(void)
{
	SaveFuncs = (struct PcsxSaveFuncs){ s_open, s_read, s_write, s_seek, s_close };
}

state_result_t state_check(const void *data, size_t size)
{
	const char *p = data;
	if (!p || size < 16 + STATE_HEAD)
		return STATE_NOT_A_STATE;
	if (!strncmp(p, "RASTATE", 7)) {   // a RetroArch header, the state follows
		p += 16;
		size -= 16;
	}
	if (size < STATE_HEAD || strncmp(p, "STv4 PCSX", 9))
		return STATE_NOT_A_STATE;
	u32 spu;
	if (size < SPU_SIZE_AT + sizeof(spu))
		return STATE_DAMAGED;
	memcpy(&spu, p + SPU_SIZE_AT, sizeof(spu));
	if (spu <= SPU_MIN || spu > SPU_MAX || spu > size - SPU_SIZE_AT - sizeof(spu))
		return STATE_DAMAGED;
	return STATE_OK;
}

bool __wrap_retro_serialize(void *data, size_t size)
{
	if (!data)
		return false;
	stream_len = size;
	stream_refused = true;   // stays set if the stream is never opened
	bool ok = __real_retro_serialize(data, size);
	return ok && !stream_refused;
}

bool __wrap_retro_unserialize(const void *data, size_t size)
{
	state_last_result = state_check(data, size);
	if (state_last_result != STATE_OK)
		return false;
	stream_len = size;
	stream_refused = true;
	if (!__real_retro_unserialize(data, size)) {
		// the core refuses a state of another version before changing anything
		state_last_result = STATE_VERSION;
		return false;
	}
	if (stream_refused) {   // a record after the checked part was damaged
		state_last_result = STATE_PARTLY;
		return false;
	}
	return true;
}
