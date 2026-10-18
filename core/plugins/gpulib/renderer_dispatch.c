/* PSX3D renderer selection. Dispatch at the command-list boundary; neither
 * renderer's primitive loop pays an indirect call or selection check. */
#include "gpu.h"

int psx3d_renderer_software = 1;
static int live;

int psx3d_renderer_select(int software)
{
  if (live)
    return -1;
  psx3d_renderer_software = !!software;
  return 0;
}

int pica_renderer_init(void);
int unai_renderer_init(void);
void pica_renderer_finish(void);
void unai_renderer_finish(void);

int renderer_init(void)
{
  int ret = psx3d_renderer_software ? unai_renderer_init() : pica_renderer_init();
  live = ret == 0;
  return ret;
}

void renderer_finish(void)
{
  if (psx3d_renderer_software)
    unai_renderer_finish();
  else
    pica_renderer_finish();
  live = 0;
}

// Direct calls allow tail branches, including the hot command-list entry.
#define DISPATCH(ret, name, args, call) \
  ret pica_renderer_##name args; \
  ret unai_renderer_##name args; \
  ret renderer_##name args { \
    return psx3d_renderer_software ? unai_renderer_##name call : pica_renderer_##name call; \
  }

DISPATCH(int, do_cmd_list,
  (uint32_t *list, int count, uint32_t *ex, int *sum, int *last, int *cmd),
  (list, count, ex, sum, last, cmd))
DISPATCH(void, sync_ecmds, (uint32_t *ex), (ex))
DISPATCH(void, update_caches, (int x, int y, int w, int h, int changed), (x, y, w, h, changed))
DISPATCH(void, flush_queues, (void), ())
DISPATCH(void, set_interlace, (int enable, int odd), (enable, odd))
DISPATCH(void, set_config, (const struct rearmed_cbs *cbs), (cbs))
DISPATCH(void, notify_screen_change, (const struct psx_gpu_screen *screen), (screen))
