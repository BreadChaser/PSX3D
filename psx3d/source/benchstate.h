#ifndef PSX3D_BENCHSTATE_H
#define PSX3D_BENCHSTATE_H
#include <stdbool.h>
#include <stddef.h>
#define BENCH_STATE_BYTES 4456448u
/* No caller-supplied directories: the wire contains a library title only. */
bool bench_state_name(const char *name);
bool bench_state_header(const unsigned char *data, size_t size);
bool bench_state_commit(const char *staged, const char *destination);
#endif
