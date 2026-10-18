/* gpu_pica: PS1 GPU on the 3DS GPU. Frontend interface (PSX3D). */
#ifndef GPU_PICA_H
#define GPU_PICA_H
#include <stdbool.h>
#include "gpu_pica_thread.h"
#include <citro3d.h>
bool pica_init(const void *shbin, unsigned size);   // the frontend's compiled pica.v.pica
// buffers for a game, including a 1 MB GPU-owned held scanout texture
bool pica_game_begin(bool small);
void pica_game_end(void);
void pica_exit(void);
void pica_flush_vertices(void);
void pica_frame_started(void);   // a GPU frame is open: drawing can go in
void pica_frame_ended(void);
void pica_state_lost(void);      // someone else drew with citro3d/citro2d
void pica_prepare_present(void); // flush PS1 work BEFORE setting up Citro2D/targets
// the displayed PS1 picture: texture, edges in texture coordinates (t > b), size
enum { PICA_PIC_NONE, PICA_PIC_GPU, PICA_PIC_CPU };
int pica_picture(C3D_Tex **tex, float *l, float *t, float *r, float *b, float *w, float *h);
void pica_stats(char *buf, int n);
// Independent experimental submission switches; zero keeps release behavior.
enum { PICA_SUBMIT_STATE = 1, PICA_SUBMIT_CLIP = 2, PICA_SUBMIT_TEX = 4 };
extern unsigned pica_prims;
extern unsigned pica_submit;
extern bool pica_async, pica_double_buffer;
extern void (*pica_wait_frame)(void);
extern bool (*pica_frame_pending)(void);
extern void (*pica_open_frame)(void);   // opens a GPU frame when drawing starts
bool pica_display_changed(void); // query only after synchronizing the GPU worker
#endif

int pica_vram_copy(int sx, int sy, int dx, int dy, int w, int h, unsigned mask);
