// Host-only HID substitute for the frontend input/config regression checks.
#ifndef HOST_3DS_H
#define HOST_3DS_H
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { u16 px, py; } touchPosition;
typedef struct { int dx, dy; } circlePosition;
enum {
 KEY_A=1<<0, KEY_B=1<<1, KEY_X=1<<2, KEY_Y=1<<3,
 KEY_L=1<<4, KEY_R=1<<5, KEY_ZL=1<<6, KEY_ZR=1<<7,
 KEY_START=1<<8, KEY_SELECT=1<<9, KEY_DUP=1<<10, KEY_DDOWN=1<<11,
 KEY_DLEFT=1<<12, KEY_DRIGHT=1<<13, KEY_TOUCH=1<<14,
};
void hidScanInput(void);
u32 hidKeysHeld(void);
u32 hidKeysDown(void);
void hidCircleRead(circlePosition *p);
void irrstCstickRead(circlePosition *p);
void hidTouchRead(touchPosition *p);
bool aptMainLoop(void);
size_t linearSpaceFree(void);
#endif
