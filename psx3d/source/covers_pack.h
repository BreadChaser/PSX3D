#ifndef PSX3D_COVERS_PACK_H
#define PSX3D_COVERS_PACK_H
#include <stdbool.h>
#include <stdint.h>

// A cover as the GPU wants it: a 256x256 RGB565 texture, tiled, with its
// mip chain (256, 128, 64, 32, 16, 8), so the library loads it with plain
// copies. The picture keeps its own aspect: it sits in the top left of the
// texture (w x h texels) and the rest repeats its edge pixels.
#define COVER_TEX      256
#define COVER_LEVELS   6
#define COVER_PACK_BYTES 174720   // = 128 KiB * (1 + 1/4 + ... + 1/1024)
#define COVER_LEGACY_SIZE 128     // the old cache: 128x128 RGBA8 square

// what transparent pixels turn into: the library's card colour
#define COVER_BG_R 20
#define COVER_BG_G 26
#define COVER_BG_B 60

// colour averaging in linear light: sRGB byte -> 12 bit, and back
void covers_gamma_init(void);
extern uint16_t covers_lin[256];
extern uint8_t covers_srgb[4096];

// size of the largest picture that fits max x max with its aspect, never
// enlarged
void covers_fit(int w, int h, int max, int *ow, int *oh);

// rgba: w*h pixels, R in the top byte (the decoder's order), w,h <= COVER_TEX.
// out: COVER_PACK_BYTES. Transparent pixels must already be flattened.
// false: out of memory for the mip chain.
bool covers_pack(const uint32_t *rgba, int w, int h, uint8_t *out);

// start of mip level `level` in a packed cover
static inline uint32_t covers_level_offset(int level)
{
	return level ? (131072u - (131072u >> (2 * level))) * 4 / 3 : 0;
}
#endif
