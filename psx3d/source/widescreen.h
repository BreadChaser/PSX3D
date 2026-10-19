// Curated data is separate from the user's ordinary cheat list.
#ifndef PSX3D_WIDESCREEN_H
#define PSX3D_WIDESCREEN_H
#include <stdbool.h>
#include <stdint.h>
bool ws_load(const char *directory, const char *serial, uint32_t fingerprint, bool new3ds);
void ws_unload(void);
bool ws_enable(bool enabled);
bool ws_available(void);
const char *ws_identity(void);
float ws_aspect(void);
void ws_frame(void);
void ws_state_loaded(void);
// Provided by the host/core, including the simulator.
void *ws_memory(void);
void ws_invalidate(uint32_t address, unsigned bytes);
#endif
