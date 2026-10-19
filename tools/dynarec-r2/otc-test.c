// The unrolled OT clear in psxdma.c against the original loop, every
// start/length near the base of RAM and a sample of the rest (host build).
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned int u32;
#define SWAP32(x) (x)
static u32 ram_a[0x2000], ram_b[0x2000];
static void orig(u32 *psxM, u32 *mem, u32 madr, u32 bcr)
{
	while (bcr-- && mem > psxM) {
		*mem-- = SWAP32((madr - 4) & 0xffffff);
		madr -= 4;
	}
	*++mem = SWAP32(0xffffff);
}
static void fast(u32 *psxM, u32 *mem, u32 madr, u32 bcr)
{
	u32 n = mem - psxM, v = madr - 4;
	if (n > bcr)
		n = bcr;
	for (; n >= 4; n -= 4, mem -= 4, v -= 16) {
		mem[0] = SWAP32(v & 0xffffff);
		mem[-1] = SWAP32((v - 4) & 0xffffff);
		mem[-2] = SWAP32((v - 8) & 0xffffff);
		mem[-3] = SWAP32((v - 12) & 0xffffff);
	}
	for (; n; n--, v -= 4)
		*mem-- = SWAP32(v & 0xffffff);
	*++mem = SWAP32(0xffffff);
}
int main(void)
{
	unsigned cases = 0;
	for (u32 w = 0; w < 0x1000; w += (w < 64 ? 1 : 37))
		for (u32 bcr = 0; bcr < 0x1100; bcr += (bcr < 40 ? 1 : 29)) {
			const u32 bases[] = {0, 0x00fffffc, 0x80000000, 0x1000000};
			for (unsigned b = 0; b < 4; b++) {
				u32 madr = bases[b] + w * 4;
				memset(ram_a, 0x5a, sizeof(ram_a)); memset(ram_b, 0x5a, sizeof(ram_b));
				orig(ram_a + 8, ram_a + 8 + w, madr, bcr);
				fast(ram_b + 8, ram_b + 8 + w, madr, bcr);
				assert(!memcmp(ram_a, ram_b, sizeof(ram_a)));
				cases++;
			}
		}
	printf("%u OT clears: unrolled == original\n", cases);
	return 0;
}
