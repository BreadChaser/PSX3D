#ifndef PSX3D_DISCART_H
#define PSX3D_DISCART_H
#include <stdbool.h>
#include <stdint.h>

// The library's stand-in for a game without box art: a photo of a black
// PlayStation disc with the PlayStation wordmark on it, both downloaded from
// Wikimedia Commons the first time (like the box art, never shipped); the
// game's title is drawn over it. See covers.c (download, cache) and
// placeholder.c (drawing; a disc drawn in code until this one is there).
#define DISC_TEX 256
#define DISC_BYTES (DISC_TEX * DISC_TEX * 4)
#define DISC_MAGIC 0x43534450u   // "PDSC"
#define DISC_VERSION 1

// where things are in the picture (texels, top left = 0,0)
typedef struct {
	uint32_t magic, version;
	uint16_t left, top, right, bottom;   // the disc
	uint16_t hub_x, hub_y;               // its centre hole
	uint16_t label_top, label_bottom;    // free space above the hole for the title
} disc_head_t;

// disc photo (JPEG) + wordmark (PNG, black on transparent) -> out: DISC_BYTES
// of RGBA8 in the PICA200's tiled order (memory row 0 = the picture's top).
bool discart_make(const char *jpg, const char *wordmark, disc_head_t *head, uint8_t *out);

#ifdef __3DS__
#include <citro3d.h>
// covers.c: the picture once it is on the GPU, else NULL
C3D_Tex *covers_disc(disc_head_t *head);
#endif
#endif
