/* PICA's CPU owner. No citro3d types: usable by gpulib and host runners. */
#ifndef GPU_PICA_THREAD_H
#define GPU_PICA_THREAD_H

// NULL on Old 3DS and when disabled/creation failed. Returns 1 after executing
// on the GPU worker, 0 when the caller should execute locally (already owner).
extern int (*pica_thread_call)(void (*call)(void *), void *data);
void pica_thread_sync(void);

#endif
