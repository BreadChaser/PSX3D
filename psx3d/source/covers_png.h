#ifndef PSX3D_COVERS_PNG_H
#define PSX3D_COVERS_PNG_H
#include <stddef.h>
#include <stdint.h>
/* RGBA8 in GPU byte order, box-filtered to size*size; caller frees. */
uint32_t *covers_png_decode(const void *data, size_t len, const char *file, int size);
#endif
