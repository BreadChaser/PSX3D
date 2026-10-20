// Save states go through a stream PSX3D gives the core (statefile.c): it
// knows the buffer's length and refuses any access outside it, and a state is
// checked before the core changes anything.
#ifndef STATEFILE_H
#define STATEFILE_H
#include <stdbool.h>
#include <stddef.h>

typedef enum {
	STATE_OK,
	STATE_NOT_A_STATE,   // no state header (nothing changed)
	STATE_VERSION,       // another core version (nothing changed)
	STATE_DAMAGED,       // cut off or a bad size inside (nothing changed)
	STATE_PARTLY,        // damaged after the core began restoring: restart
} state_result_t;

// the last retro_unserialize() (all PSX3D callers go through the wrapper)
extern state_result_t state_last_result;

// once, before the first state: the core's SaveFuncs become ours
void state_stream_install(void);
// the check retro_unserialize() makes first (exposed for tests)
state_result_t state_check(const void *data, size_t size);
#endif
