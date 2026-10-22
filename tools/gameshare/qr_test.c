// PC test for psx3d/source/qrcode.c: writes a PGM (4 px per module, 4-module
// quiet zone) for zbarimg to read back.
// cc -O2 -I psx3d/source tools/gameshare/qr_test.c psx3d/source/qrcode.c -o qr_test
#include "qrcode.h"
#include <stdio.h>

int main(int argc, char **argv)
{
	if (argc < 3)
		return 2;
	static uint8_t m[QR_MAX_SIZE * QR_MAX_SIZE];
	int size, v = qr_encode(argv[1], m, &size);
	if (!v)
		return 1;
	int px = 4, q = 4, w = (size + 2 * q) * px;
	FILE *f = fopen(argv[2], "wb");
	fprintf(f, "P5 %d %d 255\n", w, w);
	for (int y = 0; y < w; y++)
		for (int x = 0; x < w; x++) {
			int mx = x / px - q, my = y / px - q;
			int dark = mx >= 0 && my >= 0 && mx < size && my < size && m[my * size + mx];
			fputc(dark ? 0 : 255, f);
		}
	fclose(f);
	printf("version %d size %d\n", v, size);
	return 0;
}
