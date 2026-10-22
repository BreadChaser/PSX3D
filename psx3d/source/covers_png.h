#ifndef PSX3D_COVERS_PNG_H
#define PSX3D_COVERS_PNG_H
#include <stddef.h>
#include <stdint.h>
/* Opaque RGBA8 (R in the top byte), area-averaged in linear light to fit
 * max x max with the picture's own aspect (never enlarged); *ow x *oh is its
 * size. Transparent pixels are flattened onto the library's card colour.
 * Caller frees. */
uint32_t *covers_png_decode(const void *data, size_t len, const char *file, int max, int *ow, int *oh);
#endif
