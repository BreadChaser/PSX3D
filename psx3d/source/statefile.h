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
	STATE_OTHER_DISC,    // made with a disc that is not in this game's list (nothing changed)
} state_result_t;

// the last retro_unserialize() (all PSX3D callers go through the wrapper)
extern state_result_t state_last_result;

// once, before the first state: the core's SaveFuncs become ours
void state_stream_install(void);
// the check retro_unserialize() makes first (exposed for tests)
state_result_t state_check(const void *data, size_t size);

// PSX3D's trailer, at the very end of a state's zero-filled spare space (the
// core never reads there; older PSX3D versions ignore it): the layout this
// build wrote, the disc that was in the drive and, once sealed, a CRC-32 of
// the whole state. States without it (made before) still load as before.
typedef struct {
	char magic[8];            // "PSX3Dsv1"
	unsigned version;         // 1
	unsigned data_len;        // bytes the core wrote (the rest is zero)
	unsigned tail;            // bytes from the SPU block's end to the block list / pads
	unsigned flags;           // STATE_SEALED: crc is set
	unsigned disc_index, disc_count;
	char disc[96];            // the disc's file name (no folder), "" unknown
	unsigned reason;          // STATE_WHY_*: why it was taken (save backups' labels)
	unsigned reserved[3];
	unsigned crc;             // CRC-32 of [0, data_len) and the trailer before it
} state_trailer_t;
#define STATE_SEALED 1u
enum { STATE_WHY_MANUAL, STATE_WHY_TIMER, STATE_WHY_EXIT };
// emu.c: why the next serialize happens (written into its trailer)
extern unsigned state_reason;
// the reason in a state file's trailer (STATE_WHY_MANUAL without one)
unsigned state_file_reason(const char *path);

// Before a state goes to the SD card: add the checksum (the writer thread
// can do this; ~0.1 s for 4.3 MB on an Old 3DS). false: no trailer (no room).
bool state_seal(void *data, size_t size);
// The trailer, if there is one and it is sound (NULL otherwise). *crc_ok:
// sealed and the checksum matches, or not sealed at all.
const state_trailer_t *state_trailer(const void *data, size_t size, bool *crc_ok);
// emu.c: the disc in the drive, for the trailer
extern void (*state_disc_query)(char *name, size_t len, unsigned *index, unsigned *count);
// emu.c: the index of that disc in this game's list; -1: not there; -2: can't tell
extern int (*state_disc_map)(const char *name, unsigned index, unsigned count);
// the layout this build writes (measured from its own saves; until the
// first one, the constant STATE_TAIL); 0 if nothing was ever measured
extern size_t state_tail_measured;
unsigned state_crc32(const void *data, size_t len, unsigned crc);
#endif
