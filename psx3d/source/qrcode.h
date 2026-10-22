// Minimal QR code encoder (byte mode, level M, versions 1-10).
#ifndef PSX3D_QRCODE_H
#define PSX3D_QRCODE_H
#include <stdint.h>
#define QR_MAX_SIZE 57
// modules: QR_MAX_SIZE * QR_MAX_SIZE bytes, row-major, 1 = dark. Returns the
// version (0 if the text is too long) and the side length in *size.
int qr_encode(const char *text, uint8_t *modules, int *size);
#endif
