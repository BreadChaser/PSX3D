// Host test for statefile.c (run.sh: ASan/UBSan): the real stream functions
// and retro_(un)serialize wrappers, driven by a model core that makes the
// same SaveFuncs calls as misc.c SaveState/LoadState and their component
// freezes (sizes from the same core headers). The real core runs through the
// same code in the ARM simulator gate (devtools/safety). Checked: truncation
// at every section boundary, bad SPU sizes, negative and oversized seeks,
// damaged tail records and random corruption; no access outside the buffer
// (ASan), and every refusal before the core changes anything leaves the
// "running game" exactly as it was.
#ifndef STATEFILE_C
#define STATEFILE_C "../../source/statefile.c"
#endif
#define STATE_TAIL (300 + 2000 + 4 + 200 + 1000)   // the model's sio, cdr, rcnt, mdec
#include STATEFILE_C
#include <assert.h>
#include <stdarg.h>
#include <limits.h>
#include <stdio.h>

struct PcsxSaveFuncs SaveFuncs;
PcsxConfig Config;
static char logged[256];
void log_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(logged, sizeof(logged), fmt, ap);
	va_end(ap);
}
#define VERSION 0x8b410006u
#define STATE_SIZE 0x440000u
#define TAIL_FIXED (300 + 2000 + 4 + 200 + 1000)   // sio, cdr, rcnt, mdec
#define PAD_SIZE ((int)sizeof(PadDataS))

// the "running game": everything LoadState restores
static struct {
	unsigned char *ram, *bios, *hw, *regs, *gpu, *vram, *spu_hdr, *spu_ram, *spu2, *tail, *addrs;
	PadDataS pad[PADS];
	unsigned spu_size, ndrc_blocks;
} live;
static size_t sizes[] = { 0x200000, 0x80000, 0x10000, offsetof(psxRegisters, gteBusyCycle),
	sizeof(GPUFreeze_t), 1024 * 512 * 2, sizeof(SPUFreeze_t), 512 * 1024, SPUFREEZE_F2_MAX_SIZE,
	TAIL_FIXED, 16384 };

static void live_alloc(void)
{
	unsigned char **p[] = { &live.ram, &live.bios, &live.hw, &live.regs, &live.gpu, &live.vram,
		&live.spu_hdr, &live.spu_ram, &live.spu2, &live.tail, &live.addrs };
	for (size_t i = 0; i < sizeof(p) / sizeof(p[0]); i++) {
		*p[i] = malloc(sizes[i]);   // exact sizes: ASan sees any overrun
		for (size_t k = 0; k < sizes[i]; k++) (*p[i])[k] = (unsigned char)(k * (i + 3));
	}
	live.spu_size = sizeof(SPUFreeze_t) + 512 * 1024 + 0x1000;
	live.ndrc_blocks = 40;
}

static unsigned long long live_hash(void)
{
	unsigned long long h = 1469598103934665603ull;
	unsigned char *p[] = { live.ram, live.bios, live.hw, live.regs, live.gpu, live.vram,
		live.spu_hdr, live.spu_ram, live.spu2, live.tail, live.addrs };
	for (size_t i = 0; i < sizeof(p) / sizeof(p[0]); i++) {
		size_t k = 0;
		for (; k + 8 <= sizes[i]; k += 8) {   // words: fast enough under ASan
			unsigned long long w;
			memcpy(&w, p[i] + k, 8);
			h = (h ^ w) * 1099511628211ull;
		}
		for (; k < sizes[i]; k++) h = (h ^ p[i][k]) * 1099511628211ull;
	}
	for (int i = 0; i < PADS; i++)
		for (int k = 0; k < PAD_SIZE; k++) h = (h ^ ((unsigned char *)&live.pad[i])[k]) * 1099511628211ull;
	return (h ^ live.spu_size) * 31 + live.ndrc_blocks;
}

// misc.c SaveState: the same writes in the same order
bool __real_retro_serialize(void *data, size_t size)
{
	(void)size;
	void *f = SaveFuncs.open(data, "wb");
	if (!f) return false;
	unsigned char head[32] = "STv4 PCSX v5.0";
	u32 version = VERSION;
	boolean hle = 1;
	unsigned char pic[EX_SCREENPIC_SIZE] = { 0 };
	SaveFuncs.write(f, head, 32);
	SaveFuncs.write(f, &version, 4);
	SaveFuncs.write(f, &hle, sizeof(hle));
	SaveFuncs.write(f, pic, sizeof(pic));
	SaveFuncs.write(f, live.ram, sizes[0]);
	SaveFuncs.write(f, live.bios, sizes[1]);
	SaveFuncs.write(f, live.hw, sizes[2]);
	SaveFuncs.write(f, live.regs, sizes[3]);
	SaveFuncs.write(f, live.gpu, sizes[4]);
	SaveFuncs.write(f, live.vram, sizes[5]);
	SaveFuncs.write(f, &live.spu_size, 4);
	SaveFuncs.write(f, live.spu_hdr, sizes[6]);
	SaveFuncs.write(f, live.spu_ram, sizes[7]);
	SaveFuncs.write(f, live.spu2, live.spu_size - sizes[6] - sizes[7]);
	SaveFuncs.write(f, live.tail, TAIL_FIXED);
	SaveFuncs.write(f, "ariblks", 8);               // emu_if.c ndrc_freeze
	int32_t n = live.ndrc_blocks * 8;
	SaveFuncs.write(f, &n, 4);
	SaveFuncs.write(f, live.addrs, n);
	for (int i = 0; i < PADS; i++) {                 // pad.c padFreeze
		live.pad[i].saveSize = sizeof(PadDataS);
		SaveFuncs.write(f, &live.pad[i], PAD_SIZE);
	}
	SaveFuncs.close(f);
	return true;
}

static int load_calls;
static int model_extra;   // a core with one more record the check doesn't know
// misc.c LoadState and the component freezes: the same reads/seeks, the
// same early refusals (header, version), the same "skip a broken SPU size"
bool __real_retro_unserialize(const void *data, size_t size)
{
	(void)size;
	load_calls++;
	void *f = SaveFuncs.open(data, "rb");
	if (!f) return false;
	char header[32] = { 0 };
	u32 version = 0;
	boolean hle;
	SaveFuncs.read(f, header, 16);
	if (!strncmp("RASTATE", header, 7))
		SaveFuncs.read(f, header, 16);
	SaveFuncs.read(f, header + 16, 16);
	SaveFuncs.read(f, &version, 4);
	SaveFuncs.read(f, &hle, sizeof(hle));
	if (strncmp("STv4 PCSX", header, 9) || version != VERSION) {
		SaveFuncs.close(f);
		return false;
	}
	Config.HLE = hle;   // as LoadState: before anything can fail
	SaveFuncs.seek(f, EX_SCREENPIC_SIZE, SEEK_CUR);
	for (int i = 0; i < 6; i++)
		SaveFuncs.read(f, (unsigned char *[]){ live.ram, live.bios, live.hw, live.regs, live.gpu, live.vram }[i], sizes[i]);
	int Size = 0;
	SaveFuncs.read(f, &Size, 4);
	if (sizeof(SPUFreeze_t) + 512 * 1024 < (size_t)Size && (uint32_t)Size <= SPU_MAX) {
		SaveFuncs.read(f, live.spu_hdr, sizes[6]);
		SaveFuncs.read(f, live.spu_ram, sizes[7]);
		SaveFuncs.read(f, live.spu2, Size - sizes[6] - sizes[7]);
		live.spu_size = Size;
	} else
		SaveFuncs.seek(f, Size, SEEK_CUR);
	SaveFuncs.read(f, live.tail, TAIL_FIXED);
	char nh[8];
	int bytes = SaveFuncs.read(f, nh, 8);
	if (bytes != 8 || strcmp(nh, "ariblks")) {
		if (bytes > 0) SaveFuncs.seek(f, -bytes, SEEK_CUR);
	} else {
		int32_t n = 0;
		SaveFuncs.read(f, &n, 4);
		if (n > 0) {
			if (n > 16384) { SaveFuncs.seek(f, n - 16384, SEEK_CUR); n = 16384; }
			if (SaveFuncs.read(f, live.addrs, n) == n) live.ndrc_blocks = n / 8;
		}
	}
	for (int i = 0; i < PADS; i++) {
		SaveFuncs.read(f, &live.pad[i], PAD_SIZE);
		if (live.pad[i].saveSize != sizeof(PadDataS))   // as pad.c (32-bit ARM long)
			SaveFuncs.seek(f, (long)live.pad[i].saveSize - PAD_SIZE, SEEK_CUR);
	}
	if (model_extra) {
		unsigned char extra[64];
		SaveFuncs.read(f, extra, model_extra);
		live.ram[0] ^= extra[0] | 1;   // restored into the game
	}
	SaveFuncs.close(f);
	return true;
}

static int failures, checks;
static char ctx[120];
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #c, ctx); } } while (0)

static unsigned char *good;   // a good state of the "running game"
static size_t spu_end, tail_at, ndrc_size_at, pad_at;

// a load that fails before the core changes anything leaves the game as it was
static state_result_t try_load(const void *buf, size_t size)
{
	unsigned long long before = live_hash();
	bool ok = __wrap_retro_unserialize(buf, size);
	state_result_t r = state_last_result;
	CHECK(ok == (r == STATE_OK));
	if (r == STATE_NOT_A_STATE || r == STATE_DAMAGED || r == STATE_VERSION)
		CHECK(live_hash() == before);
	return r;
}

static void layout(void)
{
	tail_at = SPU_SIZE_AT + 4 + live.spu_size;
	spu_end = tail_at;
	ndrc_size_at = tail_at + TAIL_FIXED + 8;
	pad_at = ndrc_size_at + 4 + live.ndrc_blocks * 8;
}

static void streams(void)
{
	// the stream itself: negative, oversized and out-of-range accesses are
	// refused before any memory is touched, and stay refused
	unsigned char buf[17] = "0123456789abcdef", out[4] = { 9, 9, 9, 9 };
	stream_len = 16;
	void *f = s_open((const char *)buf, "rb");
	snprintf(ctx, sizeof(ctx), "stream");
	CHECK(s_seek(f, 16, SEEK_SET) == 16);   // the end itself is fine
	CHECK(s_read(f, out, 1) == -1 && out[0] == 9);   // the reviewer's probe
	s_close(f);
	CHECK(stream_refused);
	long bad[][2] = { { -1, SEEK_SET }, { 17, SEEK_SET }, { -9, SEEK_CUR }, { 9, SEEK_CUR },
		{ LONG_MIN, SEEK_CUR }, { LONG_MAX, SEEK_CUR }, { 0, SEEK_END } };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		f = s_open((const char *)buf, "rb");
		CHECK(s_seek(f, 8, SEEK_SET) == 8);
		CHECK(s_seek(f, bad[i][0], (int)bad[i][1]) == -1);
		CHECK(s_read(f, out, 1) == -1 && out[0] == 9);   // sticky
		CHECK(s_seek(f, 0, SEEK_SET) == -1);
		s_close(f);
		CHECK(stream_refused);
	}
	f = s_open((const char *)buf, "rb");
	CHECK(s_seek(f, -8, SEEK_CUR) == -1);   // before the start
	s_close(f);
	f = s_open((const char *)buf, "rb");
	CHECK(s_seek(f, 8, SEEK_SET) == 8 && s_seek(f, -8, SEEK_CUR) == 0 && s_read(f, out, 4) == 4);
	CHECK(s_write(f, out, 1) == -1);   // a read stream never writes
	s_close(f);
	f = s_open((const char *)buf, "wb");
	CHECK(s_write(f, "xyz", 3) == 3 && s_write(f, buf, 14) == -1 && buf[3] == '3');
	s_close(f);
	// serialize honours its size: too small fails, never writes past it
	unsigned char *small = malloc(SPU_SIZE_AT);
	CHECK(!__wrap_retro_serialize(small, SPU_SIZE_AT));
	free(small);
}

static void boundaries(void)
{
	// cut off at every section boundary (and one byte around it)
	size_t b[] = { 0, 15, 16, 31, 32, 36, 37, STATE_HEAD + EX_SCREENPIC_SIZE, STATE_HEAD + EX_SCREENPIC_SIZE + 0x200000,
		SPU_SIZE_AT - 1, SPU_SIZE_AT, SPU_SIZE_AT + 3, SPU_SIZE_AT + 4, SPU_SIZE_AT + 4 + sizeof(SPUFreeze_t),
		spu_end - 1, spu_end, tail_at + 300, tail_at + TAIL_FIXED, ndrc_size_at, ndrc_size_at + 4,
		pad_at, pad_at + PAD_SIZE, pad_at + PADS * PAD_SIZE - 1 };
	for (size_t i = 0; i < sizeof(b) / sizeof(b[0]); i++)
		for (int d = -1; d <= 1; d++) {
			size_t n = b[i] + d;
			if ((long)n < 0 || n >= pad_at + PADS * PAD_SIZE) continue;
			unsigned char *cut = malloc(n ? n : 1);   // exactly n bytes: ASan guards the end
			memcpy(cut, good, n);
			snprintf(ctx, sizeof(ctx), "cut at %zu", n);
			state_result_t r = try_load(cut, n);
			CHECK(r == STATE_NOT_A_STATE || r == STATE_DAMAGED);   // nothing changed
			free(cut);
			CHECK(try_load(good, STATE_SIZE) == STATE_OK);   // back to the good state
		}
}

static void bad_sizes(void)
{
	long spu[] = { -1, 0, 1, (long)SPU_MIN, (long)SPU_MAX + 1, 0x7fffffff, (long)STATE_SIZE };
	unsigned char *s = malloc(STATE_SIZE);
	for (size_t i = 0; i < sizeof(spu) / sizeof(spu[0]); i++) {
		memcpy(s, good, STATE_SIZE);
		int32_t v = (int32_t)spu[i];
		memcpy(s + SPU_SIZE_AT, &v, 4);
		snprintf(ctx, sizeof(ctx), "spu size %ld", spu[i]);
		int calls = load_calls;
		CHECK(try_load(s, STATE_SIZE) == STATE_DAMAGED);
		CHECK(load_calls == calls);   // the core was never asked
	}
	memcpy(s, good, STATE_SIZE);
	snprintf(ctx, sizeof(ctx), "header");
	memcpy(s, "STv3", 4);
	CHECK(try_load(s, STATE_SIZE) == STATE_NOT_A_STATE);
	memcpy(s, good, STATE_SIZE);
	u32 v = VERSION + 1;
	memcpy(s + 32, &v, 4);
	CHECK(try_load(s, STATE_SIZE) == STATE_VERSION);
	// RetroArch header in front: accepted the same way
	unsigned char *ra = malloc(STATE_SIZE + 16);
	memcpy(ra, "RASTATE\001", 8);
	memset(ra + 8, 0, 8);
	memcpy(ra + 16, good, STATE_SIZE);
	snprintf(ctx, sizeof(ctx), "rastate");
	CHECK(try_load(ra, STATE_SIZE + 16) == STATE_OK);
	free(ra);
	// damaged records after the SPU block: the core's seeks are refused
	int32_t ndrc[] = { -5, 0x7fffffff, 16385, (int32_t)STATE_SIZE };
	for (size_t i = 0; i < sizeof(ndrc) / sizeof(ndrc[0]); i++) {
		memcpy(s, good, STATE_SIZE);
		memcpy(s + ndrc_size_at, &ndrc[i], 4);
		snprintf(ctx, sizeof(ctx), "ndrc size %d", ndrc[i]);
		state_result_t r = try_load(s, STATE_SIZE);
		CHECK(r == STATE_OK || r == STATE_DAMAGED);
		CHECK(try_load(good, STATE_SIZE) == STATE_OK);
	}
	unsigned short pads[] = { 0, 1, PAD_SIZE - 1, PAD_SIZE + 1, 0x8000, 0xffff };
	for (size_t i = 0; i < sizeof(pads) / sizeof(pads[0]); i++)
		for (int k = 0; k < PADS; k += 7) {
			memcpy(s, good, STATE_SIZE);
			memcpy(s + pad_at + k * PAD_SIZE + offsetof(PadDataS, saveSize), &pads[i], 2);
			snprintf(ctx, sizeof(ctx), "pad %d size %u", k, pads[i]);
			state_result_t r = try_load(s, STATE_SIZE);
			CHECK(r == STATE_OK || r == STATE_DAMAGED);   // in-bounds garbage loads, as in pad.c
		}
	// the end of the last pad record cut off: refused before anything changes
	{
		size_t end = pad_at + PADS * PAD_SIZE;
		memcpy(s, good, STATE_SIZE);
		unsigned short big = PAD_SIZE + 64;
		memcpy(s + end - PAD_SIZE + offsetof(PadDataS, saveSize), &big, 2);
		snprintf(ctx, sizeof(ctx), "last pad past the end");
		CHECK(try_load(s, end + 63) == STATE_DAMAGED);
		CHECK(try_load(s, end + 64) == STATE_OK);
		CHECK(try_load(good, STATE_SIZE) == STATE_OK);
	}
	// pad.c indexes its 8-byte rxData with respSize: a record of this
	// build's size with a longer response is refused first
	for (int k = 0; k < PADS; k += 3) {
		memcpy(s, good, STATE_SIZE);
		s[pad_at + k * PAD_SIZE + offsetof(PadDataS, respSize)] = 9;
		snprintf(ctx, sizeof(ctx), "pad %d respSize", k);
		CHECK(try_load(s, STATE_SIZE) == STATE_DAMAGED);
		memcpy(s, good, STATE_SIZE);
		s[pad_at + k * PAD_SIZE + offsetof(PadDataS, respSizeOld)] = 200;
		CHECK(try_load(s, STATE_SIZE) == STATE_DAMAGED);
		memcpy(s, good, STATE_SIZE);
		s[pad_at + k * PAD_SIZE + offsetof(PadDataS, respSize)] = 8;
		CHECK(try_load(s, STATE_SIZE) == STATE_OK);
	}
	CHECK(try_load(good, STATE_SIZE) == STATE_OK);
	free(s);
}

// Review 3 F7a: a state from another build whose records are 16 bytes
// longer (same core version number) must not teach its layout: before
// PSX3D's first own save and after it, it is refused and good states load.
static void poisoning(void)
{
	size_t saved = state_tail_measured;
	unsigned char *s = calloc(1, STATE_SIZE);
	for (int round = 0; round < 2; round++) {
		state_tail_measured = round ? saved : 0;   // round 0: nothing measured yet
		memcpy(s, good, tail_at + 300);
		memset(s + tail_at + 300, 0x5a, 16);   // the extra record bytes
		memcpy(s + tail_at + 300 + 16, good + tail_at + 300, pad_at + PADS * PAD_SIZE - tail_at - 300);
		memset(s + STATE_SIZE - sizeof(state_trailer_t), 0, sizeof(state_trailer_t));   // an older build: no trailer
		snprintf(ctx, sizeof(ctx), "poison round %d", round);
		state_result_t r = try_load(s, STATE_SIZE);
		CHECK(r == STATE_VERSION);   // the block list is not where this build has it
		CHECK(state_tail_measured == (round ? saved : 0));   // nothing learned
		CHECK(try_load(good, STATE_SIZE) == STATE_OK);
		for (int i = 0; i < 5; i++)   // later loads still check at the right place
			CHECK(try_load(s, STATE_SIZE) != STATE_OK);
		// without a block list (interpreter): the pads are read where this
		// build has them, and the shifted record is no pad record
		memcpy(s + tail_at + 300 + 16 + TAIL_FIXED - 300, "ARIBLKS", 8);
		CHECK(try_load(s, STATE_SIZE) != STATE_OK);
	}
	// the same build with a trailer saying so: another build's layout
	memcpy(s, good, STATE_SIZE);
	state_trailer_t t;
	memcpy(&t, s + STATE_SIZE - sizeof(t), sizeof(t));
	t.tail += 16;
	memcpy(s + STATE_SIZE - sizeof(t), &t, sizeof(t));
	snprintf(ctx, sizeof(ctx), "trailer layout");
	int calls = load_calls;
	CHECK(try_load(s, STATE_SIZE) == STATE_VERSION && load_calls == calls);
	state_tail_measured = saved;
	free(s);
}

// own saves: the layout is measured, the trailer written, a sealed state
// is checked whole (every damaged byte refused before the core reads it)
static void trailer(void)
{
	snprintf(ctx, sizeof(ctx), "trailer");
	CHECK(state_tail_measured == STATE_TAIL);
	bool crc_ok;
	const state_trailer_t *t = state_trailer(good, STATE_SIZE, &crc_ok);
	CHECK(t && crc_ok && !(t->flags & STATE_SEALED) && t->tail == STATE_TAIL &&
	      t->data_len == pad_at + PADS * PAD_SIZE && !strcmp(t->disc, "Game (Disc 2).cue") && t->disc_index == 1);
	CHECK(state_crc32("123456789", 9, 0) == 0xcbf43926u);   // the CRC-32 check value
	unsigned char *s = malloc(STATE_SIZE);
	memcpy(s, good, STATE_SIZE);
	CHECK(state_seal(s, STATE_SIZE));
	t = state_trailer(s, STATE_SIZE, &crc_ok);
	CHECK(t && crc_ok && (t->flags & STATE_SEALED));
	CHECK(try_load(s, STATE_SIZE) == STATE_OK);
	CHECK(!memcmp(s, good, STATE_SIZE - sizeof(state_trailer_t)));   // sealing changes only the trailer
	srand(5);
	for (int it = 0; it < 300; it++) {
		size_t at = it < 40 ? (size_t)(STATE_SIZE - sizeof(state_trailer_t) + it % sizeof(state_trailer_t))
		                    : (size_t)rand() % (pad_at + PADS * PAD_SIZE);
		unsigned char bit = (unsigned char)(1u << (rand() % 8));
		s[at] ^= bit;
		snprintf(ctx, sizeof(ctx), "sealed flip at %zu", at);
		int calls = load_calls;
		state_result_t r = try_load(s, STATE_SIZE);
		// a flip in the trailer may only make it "no trailer" (an old state)
		if (at < STATE_SIZE - sizeof(state_trailer_t))
			CHECK(r != STATE_OK && r != STATE_PARTLY && load_calls == calls);
		else
			CHECK(r != STATE_PARTLY);
		s[at] ^= bit;
	}
	CHECK(try_load(s, STATE_SIZE) == STATE_OK);
	// no room for a trailer: none written, the state still loads
	size_t full = pad_at + PADS * PAD_SIZE;
	unsigned char *tight = malloc(full + 8);
	CHECK(__wrap_retro_serialize(tight, full + 8));
	CHECK(!state_trailer(tight, full + 8, NULL) && !state_seal(tight, full + 8));
	CHECK(try_load(tight, full + 8) == STATE_OK);
	free(tight);
	free(s);
}

// the disc: mapped by file name to this launch's list, the state's index set
// for the load and put back; a disc not in the list refuses the load
static int map_result;
static char map_name[96];
static int disc_map(const char *name, unsigned index, unsigned count)
{
	(void)index; (void)count;
	snprintf(map_name, sizeof(map_name), "%s", name);
	return map_result;
}
static void disc_query(char *name, size_t len, unsigned *index, unsigned *count)
{
	snprintf(name, len, "Game (Disc 2).cue");
	*index = 1;
	*count = 2;
}
static void discs(void)
{
	unsigned char *s = malloc(STATE_SIZE);
	memcpy(s, good, STATE_SIZE);
	state_seal(s, STATE_SIZE);
	unsigned char *copy = malloc(STATE_SIZE);
	memcpy(copy, s, STATE_SIZE);
	state_disc_map = disc_map;
	int idx;
	for (map_result = -2; map_result <= 3; map_result++) {
		snprintf(ctx, sizeof(ctx), "disc map %d", map_result);
		state_result_t r = try_load(s, STATE_SIZE);
		CHECK(!strcmp(map_name, "Game (Disc 2).cue"));
		CHECK(!memcmp(s, copy, STATE_SIZE));   // the caller's copy as it was (checksum still good)
		if (map_result == -1) {
			CHECK(r == STATE_OTHER_DISC);
			continue;
		}
		CHECK(r == STATE_OK);
		memcpy(&idx, live.hw + 0xf000 + 32, sizeof(idx));
		CHECK(idx == (map_result >= 0 ? map_result : 1));   // -2: can't tell, as saved
	}
	// a state without a trailer: the core's index, no mapping
	memset(s + STATE_SIZE - sizeof(state_trailer_t), 0, sizeof(state_trailer_t));
	map_result = 0;
	map_name[0] = 0;
	CHECK(try_load(s, STATE_SIZE) == STATE_OK && !map_name[0]);
	state_disc_map = NULL;
	free(copy);
	free(s);
}

// a damaged record only the stream sees (a state without a trailer): the
// game is restarted, with the BIOS setting it had (LoadState set the state's)
static void partly(void)
{
	unsigned char *s = malloc(STATE_SIZE);
	memcpy(s, good, STATE_SIZE);
	memset(s + STATE_SIZE - sizeof(state_trailer_t), 0, sizeof(state_trailer_t));
	unsigned short big = 0xffff;
	memcpy(s + pad_at + PADS * PAD_SIZE - PAD_SIZE + offsetof(PadDataS, saveSize), &big, 2);
	Config.HLE = 0;
	snprintf(ctx, sizeof(ctx), "partly");
	state_result_t r = try_load(s, pad_at + PADS * PAD_SIZE + 100);
	CHECK(r == STATE_DAMAGED);   // the check sees the last pad's size
	// a core that reads one more record than the check knows: cut off there,
	// only the stream sees it, after the core began restoring
	memcpy(s, good, STATE_SIZE);
	memset(s + STATE_SIZE - sizeof(state_trailer_t), 0, sizeof(state_trailer_t));
	model_extra = 32;
	Config.HLE = 0;   // the state says HLE 1
	r = try_load(s, pad_at + PADS * PAD_SIZE + 16);
	CHECK(r == STATE_PARTLY && Config.HLE == 0);   // the game restarts with the BIOS it ran with
	CHECK(try_load(s, pad_at + PADS * PAD_SIZE + 32) == STATE_OK && Config.HLE == 1);
	model_extra = 0;
	CHECK(try_load(good, STATE_SIZE) == STATE_OK);
	free(s);
}

static void fuzz(void)
{
	unsigned char *s = malloc(STATE_SIZE);
	size_t hot[] = { 0, 8, 32, 36, SPU_SIZE_AT, SPU_SIZE_AT + 2, ndrc_size_at - 8, ndrc_size_at, pad_at, pad_at + PAD_SIZE };
	srand(77);
	int results[5] = { 0 };
	for (int it = 0; it < 1500; it++) {
		memcpy(s, good, STATE_SIZE);
		int flips = 1 + rand() % 6;
		for (int k = 0; k < flips; k++) {
			size_t at = rand() % 3 ? hot[rand() % 10] + rand() % 4 : (size_t)rand() % STATE_SIZE;
			s[at] ^= (unsigned char)(1u << (rand() % 8));
		}
		size_t n = rand() % 4 ? STATE_SIZE : (size_t)rand() % STATE_SIZE;
		unsigned char *t = malloc(n ? n : 1);
		memcpy(t, s, n);
		snprintf(ctx, sizeof(ctx), "fuzz %d", it);
		results[try_load(t, n)]++;
		free(t);
		if (state_last_result == STATE_OK || state_last_result == STATE_PARTLY)
			CHECK(try_load(good, STATE_SIZE) == STATE_OK);
	}
	printf("fuzz: ok %d, not a state %d, version %d, damaged %d, partly %d\n",
		results[0], results[1], results[2], results[3], results[4]);
	snprintf(ctx, sizeof(ctx), "fuzz totals");
	CHECK(results[STATE_PARTLY] == 0);   // the layout is known: all caught first
	free(s);
}

int main(void)
{
	state_stream_install();
	state_disc_query = disc_query;
	live_alloc();
	memcpy(live.hw + 0xf000, &(u32){ MISC_MAGIC }, 4);   // misc_save_data, disc index 1
	memcpy(live.hw + 0xf000 + 32, &(int){ 1 }, 4);
	good = malloc(STATE_SIZE);
	CHECK(__wrap_retro_serialize(good, STATE_SIZE));
	layout();
	unsigned long long h = live_hash();
	CHECK(try_load(good, STATE_SIZE) == STATE_OK && live_hash() == h);
	CHECK(good[STATE_SIZE - 1] == 0);   // the unused space is zero-filled
	streams();
	boundaries();
	bad_sizes();
	poisoning();
	trailer();
	discs();
	partly();
	fuzz();
	printf("state streams: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
