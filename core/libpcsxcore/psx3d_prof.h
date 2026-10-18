/* PSX3D: cheap section timers for finding where frame time goes.
 * Off unless psx3d_prof_on is set by the frontend. */
#ifndef PSX3D_PROF_H
#define PSX3D_PROF_H
#ifdef __cplusplus
extern "C" {
#endif
enum { PROF_COMPILE, PROF_INVAL, PROF_SPU, PROF_GPU, PROF_IRQ, PROF_CDR, PROF_N };
extern unsigned long long psx3d_prof_t[PROF_N];
extern unsigned int psx3d_prof_c[PROF_N];
extern int psx3d_prof_on;
#ifdef _3DS
unsigned long long svcGetSystemTick(void);
#define PROF_TICK() svcGetSystemTick()
#else
#define PROF_TICK() 0ull
#endif
#define PROF_BEGIN(id) unsigned long long prof_t0_##id = psx3d_prof_on ? PROF_TICK() : 0
#define PROF_END(id) do { if (psx3d_prof_on) { psx3d_prof_t[id] += PROF_TICK() - prof_t0_##id; psx3d_prof_c[id]++; } } while (0)
#ifdef __cplusplus
}
#endif
#endif
