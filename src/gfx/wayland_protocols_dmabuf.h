#ifndef FLUX_WAYLAND_PROTOCOLS_DMABUF_H
#define FLUX_WAYLAND_PROTOCOLS_DMABUF_H

#include "types.h"

typedef struct {
    uint32_t wl_rule_id;
    uint8_t  protocol_group; /* 1=linux-dmabuf-v1, 2=linux-drm-syncobj-v1, 3=fractional-scale/tearing, 4=XWayland-Glamor */
    uint8_t  version_num;
    uint32_t drm_fourcc_mod;
    const char *protocol_interface;
    const char *compositor_dispatch;
    const char *zero_copy_guarantee;
} wayland_dmabuf_rule_t;

typedef struct {
    bool     linux_dmabuf_v4_feedback;
    bool     drm_syncobj_explicit_sync;
    bool     fractional_scale_tearing_ok;
    bool     xwayland_rootless_glamor;
    uint32_t registered_protocol_rules;
} wayland_protocols_status_t;

void   wayland_protocols_dmabuf_init(void);
bool   wayland_protocols_get_status(wayland_protocols_status_t *out);
size_t wayland_protocols_db_size(void);
bool   wayland_protocols_dmabuf_selftest(void);

#endif
