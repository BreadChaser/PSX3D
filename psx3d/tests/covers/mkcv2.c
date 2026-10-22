// Dev tool: PNG -> the app's cover cache file (.cv2), the way the worker makes it.
// cc -O2 mkcv2.c ../../source/covers_pack.c ../../source/covers_png.c $(pkg-config --cflags --libs libpng) -lm -o mkcv2
// mkcv2 in.png out.cv2
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../source/covers_pack.h"
#include "../../source/covers_png.h"

int main(int argc, char **argv)
{
	if (argc != 3) return 2;
	int w, h;
	uint32_t *px = covers_png_decode(NULL, 0, argv[1], COVER_TEX, &w, &h);
	uint8_t *data = malloc(COVER_PACK_BYTES);
	if (!px || !data || !covers_pack(px, w, h, data)) return 1;
	struct { uint32_t magic; uint16_t w, h; uint32_t levels, bytes; } head = { 0x32564350u, w, h, COVER_LEVELS, COVER_PACK_BYTES };
	FILE *f = fopen(argv[2], "wb");
	if (!f || fwrite(&head, sizeof(head), 1, f) != 1 || fwrite(data, 1, COVER_PACK_BYTES, f) != COVER_PACK_BYTES) return 1;
	return fclose(f) ? 1 : 0;
}
