#ifndef PSX3D_COVERS_PACK_H
#define PSX3D_COVERS_PACK_H
#include <stdbool.h>
#include <stdint.h>

// A cover as the GPU wants it: a 256x256 RGB565 texture in the PICA200's
// tiled order, so the library loads it with a plain copy. The picture keeps
// its own aspect: it sits in the top left of the texture (w x h texels) and
// the rest repeats its edge pixels.
// No mip chain: on the console the GPU sampled the 128 level even for the
// selected cover (no sharper than the old 128x128 covers), and the
// supersampled top screen already averages the shrunk covers.
#define COVER_TEX      256
#define COVER_LEVELS   1
#define COVER_PACK_BYTES (COVER_TEX * COVER_TEX * 2)
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
void covers_pack(const uint32_t *rgba, int w, int h, uint8_t *out);
#endif
