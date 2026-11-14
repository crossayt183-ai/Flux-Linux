#ifndef FLUX_WAYLAND_H
#define FLUX_WAYLAND_H

#include "types.h"
#include "task.h"

#define WL_MAX_SURFACES 4
#define WL_MAX_W        240
#define WL_MAX_H        120
#define WL_MAX_PAGES    32

void wayland_init(void);
int  wl_surface_create_for_task(task_t *t, const char *title, int w, int h, uint64_t *user_va_out);
int  wl_surface_commit(int surf_id);
void wayland_stats(uint32_t *surfaces_out, uint32_t *commits_out);
bool wayland_blit_latest(int32_t dst_x, int32_t dst_y, int32_t max_w, int32_t max_h,
                         char *title_out, size_t title_cap, uint32_t *commits_out,
                         int *surf_w_out, int *surf_h_out);

#endif
