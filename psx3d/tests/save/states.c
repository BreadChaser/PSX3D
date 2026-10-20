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
#include STATEFILE_C
#include <assert.h>
#include <limits.h>
#include <stdio.h>

struct PcsxSaveFuncs SaveFuncs;
#define VERSION 0x8b410006u
#define STATE_SIZE 0x440000u
#define TAIL_FIXED (300 + 2000 + 4 + 200 + 1000)   // sio, cdr, rcnt, mdec
#define PAD_SIZE 64

// the "running game": everything LoadState restores
static struct {
	unsigned char *ram, *bios, *hw, *regs, *gpu, *vram, *spu_hdr, *spu_ram, *spu2, *tail, *addrs;
	unsigned char pad[2][PAD_SIZE];
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
	for (int i = 0; i < 2; i++)
		for (int k = 0; k < PAD_SIZE; k++) h = (h ^ live.pad[i][k]) * 1099511628211ull;
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
	for (int i = 0; i < 2; i++) {                    // pad.c: saveSize first
		u32 sz = PAD_SIZE;
		memcpy(live.pad[i], &sz, 4);
		SaveFuncs.write(f, live.pad[i], PAD_SIZE);
	}
	SaveFuncs.close(f);
	return true;
}

static int load_calls;
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
	for (int i = 0; i < 2; i++) {
		SaveFuncs.read(f, live.pad[i], PAD_SIZE);
		u32 sz;
		memcpy(&sz, live.pad[i], 4);
		if (sz != PAD_SIZE) SaveFuncs.seek(f, (long)sz - PAD_SIZE, SEEK_CUR);
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
		pad_at, pad_at + PAD_SIZE, pad_at + 2 * PAD_SIZE - 1 };
	for (size_t i = 0; i < sizeof(b) / sizeof(b[0]); i++)
		for (int d = -1; d <= 1; d++) {
			size_t n = b[i] + d;
			if ((long)n < 0 || n >= pad_at + 2 * PAD_SIZE) continue;
			unsigned char *cut = malloc(n ? n : 1);   // exactly n bytes: ASan guards the end
			memcpy(cut, good, n);
			snprintf(ctx, sizeof(ctx), "cut at %zu", n);
			state_result_t r = try_load(cut, n);
			CHECK(r != STATE_OK);
			if (n < spu_end)   // everything up to the SPU block: nothing changed
				CHECK(r == STATE_NOT_A_STATE || r == STATE_DAMAGED);
			else
				CHECK(r == STATE_PARTLY);
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
		CHECK(r == STATE_OK || r == STATE_PARTLY);
		CHECK(try_load(good, STATE_SIZE) == STATE_OK);
	}
	u32 pads[] = { 0, 1, 0xffffffff, 0x80000000, STATE_SIZE };
	for (size_t i = 0; i < sizeof(pads) / sizeof(pads[0]); i++) {
		memcpy(s, good, STATE_SIZE);
		memcpy(s + pad_at, &pads[i], 4);
		snprintf(ctx, sizeof(ctx), "pad size %u", pads[i]);
		state_result_t r = try_load(s, STATE_SIZE);
		CHECK(r == STATE_OK || r == STATE_PARTLY);   // in-bounds garbage, as in pad.c
		CHECK(pads[i] < 0x10000 || r == STATE_PARTLY);
		CHECK(try_load(good, STATE_SIZE) == STATE_OK);
	}
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
	free(s);
}

int main(void)
{
	state_stream_install();
	live_alloc();
	good = malloc(STATE_SIZE);
	CHECK(__wrap_retro_serialize(good, STATE_SIZE));
	layout();
	unsigned long long h = live_hash();
	CHECK(try_load(good, STATE_SIZE) == STATE_OK && live_hash() == h);
	CHECK(good[STATE_SIZE - 1] == 0);   // the unused space is zero-filled
	streams();
	boundaries();
	bad_sizes();
	fuzz();
	printf("state streams: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
