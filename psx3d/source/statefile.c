// Save state streams (harsh review C2). The core reads and writes a state
// through its SaveFuncs table; the libretro glue's own functions know only a
// buffer and a position, so a damaged state could make it read or write past
// the buffer. PSX3D puts its own functions in that table: they know the
// buffer's length, refuse any access outside it before touching memory and
// remember the refusal. retro_serialize/retro_unserialize are wrapped at the
// final link: they honour the size they are given, and a state's layout is
// checked before the core changes anything, so a damaged state leaves the
// running game as it was. The core's sources stay as they are (its code
// layout too).
//
// Review 3 (F7): the layout after the SPU block is never learned from a
// loaded state any more. It is this build's own: the constant STATE_TAIL
// until PSX3D's first own save, then what that save measured (a mismatch is
// logged). States carry a trailer (statefile.h) with that layout, the disc
// and a CRC-32: a sealed state is checked whole before the core reads it.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

// misc.c SaveState after the SPU block, up to the dynarec's block list (or
// the pads without one): sioFreeze, cdrFreeze, psxRcntFreeze, mdecFreeze.
// Their records are private to the core; this is the ARM build's sum, the
// same in every state since v0.3 (devtools: 37018 in all saved states).
// Every own save measures it again (state_tail_measured).
#ifndef STATE_TAIL
#define STATE_TAIL 37018
#endif
#define PADS 8
#define TRAILER_MAGIC "PSX3Dsv1"
// misc.c struct misc_save_data, kept in the state's hardware page
#define MISC_AT (STATE_HEAD + EX_SCREENPIC_SIZE + 0x200000 + 0x80000 + 0xf000)
#define MISC_MAGIC 0x4353494d
#define MISC_DISC_AT (MISC_AT + 8 * sizeof(u32))   // int CdromFrontendId

state_result_t state_last_result;
size_t state_tail_measured;
unsigned state_reason;
void (*state_disc_query)(char *name, size_t len, unsigned *index, unsigned *count);
int (*state_disc_map)(const char *name, unsigned index, unsigned count);
void log_printf(const char *fmt, ...);

struct stream {
	char *buf;
	size_t pos, len;   // pos <= len always
	bool write, refused;
	// writing: where the block list and the last PADS writes (pad.c) went
	size_t ari_at, last_at[PADS];
	u32 last_len[PADS];
	unsigned writes;
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
	memset(s, 0, sizeof(*s));
	s->buf = (char *)name;
	s->len = stream_len;
	s->write = mode[0] == 'w' || mode[1] == 'w';
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
	if (len == 8 && !memcmp(buf, "ariblks", 8))
		s->ari_at = s->pos;
	s->last_at[s->writes % PADS] = s->pos;
	s->last_len[s->writes % PADS] = len;
	s->writes++;
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

// ---------------------------------------------------------------- checksum

static u32 crc_table[4][256];

static void crc_init(void)
{
	for (u32 i = 0; i < 256; i++) {
		u32 c = i;
		for (int k = 0; k < 8; k++)
			c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
		crc_table[0][i] = c;
	}
	for (u32 i = 0; i < 256; i++)
		for (int t = 1; t < 4; t++)
			crc_table[t][i] = (crc_table[t - 1][i] >> 8) ^ crc_table[0][crc_table[t - 1][i] & 0xff];
}

// standard CRC-32 (zlib's), four bytes a step; start with crc 0
unsigned state_crc32(const void *data, size_t len, unsigned crc)
{
	const u8 *p = data;
	u32 c = ~crc;
	if (!crc_table[0][1])
		crc_init();
	for (; len && ((uintptr_t)p & 3); len--)
		c = crc_table[0][(c ^ *p++) & 0xff] ^ (c >> 8);
	for (; len >= 4; len -= 4, p += 4) {
		u32 w;
		memcpy(&w, p, 4);   // little endian (ARM and the host tests)
		c ^= w;
		c = crc_table[3][c & 0xff] ^ crc_table[2][(c >> 8) & 0xff] ^
		    crc_table[1][(c >> 16) & 0xff] ^ crc_table[0][c >> 24];
	}
	for (; len; len--)
		c = crc_table[0][(c ^ *p++) & 0xff] ^ (c >> 8);
	return ~c;
}

// ---------------------------------------------------------------- trailer

static size_t tail_expected(void)
{
	return state_tail_measured ? state_tail_measured : STATE_TAIL;
}

// A state PSX3D just wrote (no RetroArch header): the layout it measured,
// recorded as this build's, then the trailer in the spare space.
static void finish_own(struct stream *s)
{
	u32 spu;
	size_t at, mark = 0;
	bool pads = s->writes >= PADS;
	for (int i = 0; pads && i < PADS; i++)
		pads = s->last_len[i] == sizeof(PadDataS);
	if (s->pos < SPU_SIZE_AT + sizeof(spu))
		return;
	memcpy(&spu, s->buf + SPU_SIZE_AT, sizeof(spu));
	at = SPU_SIZE_AT + sizeof(spu) + spu;
	if (s->ari_at > at)
		mark = s->ari_at;
	else if (pads)
		mark = s->last_at[s->writes % PADS];   // the oldest of the last 8: pad 0
	if (mark > at && mark - at < 0x100000) {
		size_t tail = mark - at;
		if (tail != tail_expected())
			log_printf("state: layout after the SPU block is %u bytes, not %u: update STATE_TAIL\n",
				(unsigned)tail, (unsigned)tail_expected());
		state_tail_measured = tail;
	}
	state_trailer_t t;
	if (s->pos + sizeof(t) > s->len)
		return;   // no room: a state without a trailer, as before
	memset(&t, 0, sizeof(t));
	memcpy(t.magic, TRAILER_MAGIC, sizeof(t.magic));
	t.version = 1;
	t.data_len = (unsigned)s->pos;
	t.tail = (unsigned)tail_expected();
	t.reason = state_reason;
	static unsigned count;
	t.saved = (unsigned)time(NULL);
	t.id = (++count * 2654435761u) ^ t.saved;   // unique within a run, the time across runs
	if (state_disc_query)
		state_disc_query(t.disc, sizeof(t.disc), &t.disc_index, &t.disc_count);
	t.disc[sizeof(t.disc) - 1] = 0;
	memcpy(s->buf + s->len - sizeof(t), &t, sizeof(t));
}

const state_trailer_t *state_trailer(const void *data, size_t size, bool *crc_ok)
{
	static state_trailer_t t;
	if (crc_ok)
		*crc_ok = false;
	if (!data || size < sizeof(t))
		return NULL;
	memcpy(&t, (const char *)data + size - sizeof(t), sizeof(t));
	if (memcmp(t.magic, TRAILER_MAGIC, sizeof(t.magic)) || t.version != 1 ||
	    t.data_len > size - sizeof(t) || t.disc[sizeof(t.disc) - 1])
		return NULL;
	if (crc_ok)
		*crc_ok = !(t.flags & STATE_SEALED) ||
			state_crc32(&t, offsetof(state_trailer_t, crc), state_crc32(data, t.data_len, 0)) == t.crc;
	return &t;
}

bool state_file_info(const char *path, unsigned *reason, unsigned *saved, unsigned *id)
{
	state_trailer_t t;
	FILE *f = fopen(path, "rb");
	bool ok = f && !fseek(f, -(long)sizeof(t), SEEK_END) && fread(&t, sizeof(t), 1, f) == 1 &&
		!memcmp(t.magic, TRAILER_MAGIC, sizeof(t.magic)) && t.version == 1;
	if (f)
		fclose(f);
	*reason = ok ? t.reason : STATE_WHY_MANUAL;
	*saved = ok ? t.saved : 0;
	*id = ok ? t.id : 0;
	return ok;
}

bool state_seal(void *data, size_t size)
{
	state_trailer_t t;
	if (!state_trailer(data, size, NULL))
		return false;
	memcpy(&t, (char *)data + size - sizeof(t), sizeof(t));
	t.flags |= STATE_SEALED;
	t.crc = state_crc32(&t, offsetof(state_trailer_t, crc), state_crc32(data, t.data_len, 0));
	memcpy((char *)data + size - sizeof(t), &t, sizeof(t));
	return true;
}

static void s_close(void *file)
{
	struct stream *s = file;
	if (!s)
		return;
	stream_refused = s->refused;
	if (s->write && !s->refused) {   // no stale bytes in the unused space
		memset(s->buf + s->pos, 0, s->len - s->pos);
		finish_own(s);
	}
	free(s);
}

void state_stream_install(void)
{
	SaveFuncs = (struct PcsxSaveFuncs){ s_open, s_read, s_write, s_seek, s_close };
	crc_init();
}

// After the SPU block: the other components' records (tail_expected() bytes
// in this build), the block list (emu_if.c: "ariblks", a size, that many
// bytes; absent with the interpreter), then pad.c's 8 PadDataS records, each
// followed by saveSize - sizeof(PadDataS) more bytes (pad.c seeks by that
// difference). A record of this build's size must have a response length
// that fits its buffers (pad.c indexes rxData with it); no pad record of any
// version is shorter than PAD_MIN.
#define TAIL_SCAN 0x10000
#define PAD_MIN 64
static state_result_t check_tail(const char *p, size_t size, size_t at)
{
	static const char magic[8] = "ariblks";
	size_t tail = tail_expected(), pos;
	if (tail > size - at)
		return STATE_DAMAGED;
	pos = at + tail;
	if (pos + sizeof(magic) <= size && !memcmp(p + pos, magic, sizeof(magic))) {
		int32_t n;
		if (pos + 12 > size)
			return STATE_DAMAGED;
		memcpy(&n, p + pos + 8, sizeof(n));
		if (n > 0 && (size_t)n > size - pos - 12)
			return STATE_DAMAGED;
		pos += 12 + (n > 0 ? (size_t)n : 0);
	} else
		// the block list elsewhere: the records before it have other sizes
		for (size_t i = at; i + sizeof(magic) <= size && i < at + TAIL_SCAN; i++)
			if (!memcmp(p + i, magic, sizeof(magic)))
				return STATE_VERSION;
	for (int i = 0; i < PADS; i++) {
		unsigned short saved;
		unsigned char resp, resp_old;
		if (pos > size || sizeof(PadDataS) > size - pos)
			return STATE_DAMAGED;
		memcpy(&saved, p + pos + offsetof(PadDataS, saveSize), sizeof(saved));
		if (saved < PAD_MIN || saved > size - pos)
			return STATE_DAMAGED;
		if (saved == sizeof(PadDataS)) {
			resp = (unsigned char)p[pos + offsetof(PadDataS, respSize)];
			resp_old = (unsigned char)p[pos + offsetof(PadDataS, respSizeOld)];
			if (resp > sizeof(((PadDataS *)0)->rxData) || resp_old > sizeof(((PadDataS *)0)->rxData))
				return STATE_DAMAGED;
		}
		pos += saved;   // the record, then the seek by saved - sizeof
	}
	return STATE_OK;
}

state_result_t state_check(const void *data, size_t size)
{
	const char *p = data;
	bool crc_ok;
	if (!p || size < 16 + STATE_HEAD)
		return STATE_NOT_A_STATE;
	const state_trailer_t *t = state_trailer(p, size, &crc_ok);
	if (!strncmp(p, "RASTATE", 7)) {   // a RetroArch header, the state follows
		p += 16;
		size -= 16;
	}
	if (size < STATE_HEAD || strncmp(p, "STv4 PCSX", 9))
		return STATE_NOT_A_STATE;
	if (t && !crc_ok)
		return STATE_DAMAGED;
	if (t && t->tail && t->tail != tail_expected())
		return STATE_VERSION;   // same core version number, other records: another build
	u32 spu;
	if (size < SPU_SIZE_AT + sizeof(spu))
		return STATE_DAMAGED;
	memcpy(&spu, p + SPU_SIZE_AT, sizeof(spu));
	if (spu <= SPU_MIN || spu > SPU_MAX || spu > size - SPU_SIZE_AT - sizeof(spu))
		return STATE_DAMAGED;
	return check_tail(p, size, SPU_SIZE_AT + sizeof(spu) + spu);
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

// The state's disc, as an index into the current game's disc list: the core
// restores the index the state was saved with, which is a different disc
// when the game was started another way (library playlist vs one disc from
// the browser). The index in the state is set to the right one for the load
// and put back afterwards (the caller's copy stays as it was, checksum too).
// -1: put nothing back; -2: the disc is not in the list.
static int disc_fix(char *p, size_t size, int *old)
{
	const state_trailer_t *t = state_trailer(p, size, NULL);
	u32 magic;
	int now;
	if (!strncmp(p, "RASTATE", 7)) {
		p += 16;
		size -= 16;
	}
	if (!t || !t->disc[0] || !state_disc_map || size < MISC_DISC_AT + sizeof(int))
		return -1;
	memcpy(&magic, p + MISC_AT, sizeof(magic));
	if (magic != MISC_MAGIC)
		return -1;
	int want = state_disc_map(t->disc, t->disc_index, t->disc_count);
	if (want == -1)
		return -2;
	memcpy(old, p + MISC_DISC_AT, sizeof(*old));
	if (want < 0 || want == *old)
		return -1;
	now = want;
	memcpy(p + MISC_DISC_AT, &now, sizeof(now));
	log_printf("state: disc %s was index %d, now %d\n", t->disc, *old, want);
	return 0;
}

bool __wrap_retro_unserialize(const void *data, size_t size)
{
	state_last_result = state_check(data, size);
	if (state_last_result != STATE_OK)
		return false;
	int old_disc = 0, fixed = disc_fix((char *)data, size, &old_disc);
	if (fixed == -2) {
		state_last_result = STATE_OTHER_DISC;
		return false;
	}
	boolean hle = Config.HLE;   // LoadState sets it from the state first
	stream_len = size;
	stream_refused = true;
	bool ok = __real_retro_unserialize(data, size);
	if (fixed == 0) {
		char *p = (char *)data + (strncmp(data, "RASTATE", 7) ? 0 : 16);
		memcpy(p + MISC_DISC_AT, &old_disc, sizeof(old_disc));
	}
	if (!ok) {
		// the core refuses a state of another version before changing anything
		state_last_result = STATE_VERSION;
		return false;
	}
	if (stream_refused) {   // a record the check could not see was damaged
		Config.HLE = hle;   // the game is restarted with the BIOS it ran with
		state_last_result = STATE_PARTLY;
		return false;
	}
	return true;
}
