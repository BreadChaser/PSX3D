// Curated data is separate from the user's ordinary cheat list.
#ifndef PSX3D_WIDESCREEN_H
#define PSX3D_WIDESCREEN_H
#include <stdbool.h>
#include <stdint.h>
// console_policy: this console's WS_* value from the game's preset.
bool ws_load(const char *directory, const char *serial, uint32_t fingerprint, int console_policy);
void ws_unload(void);
bool ws_enable(bool enabled);
bool ws_available(void);
void ws_policy_override(bool on);
// The preset's widescreen value for this console. Speed decisions stay
// visible to the user: a costly bundle is offered with a warning, and one
// held back for speed is explained instead of called missing.
enum { WS_HIDDEN, WS_OFFERED, WS_COSTLY, WS_TOO_SLOW };
int ws_console_policy(void);   // -1 when the game has no bundle
const char *ws_identity(void);
float ws_aspect(void);
void ws_frame(void);
void ws_state_loaded(void);
// Provided by the host/core, including the simulator.
void *ws_memory(void);
void ws_invalidate(uint32_t address, unsigned bytes);
#endif
