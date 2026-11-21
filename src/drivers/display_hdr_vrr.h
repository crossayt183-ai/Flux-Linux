#ifndef FLUX_DISPLAY_HDR_VRR_H
#define FLUX_DISPLAY_HDR_VRR_H

#include "types.h"

typedef struct {
    uint16_t monitor_id;
    uint16_t width;
    uint16_t height;
    uint16_t max_hz;
    uint16_t min_vrr_hz;
    uint16_t peak_nits;
    uint8_t  color_depth_bpc;
    uint8_t  conn_type; /* 1=DP 2.1 UHBR20, 2=HDMI 2.1b FRL, 3=USB4-DP-Alt */
    const char *vendor_model;
    const char *vrr_hdr_tech;
    const char *lut_calibration;
} monitor_hdr_profile_t;

typedef struct {
    bool     dp21_uhbr20_online;
    bool     gsync_freesync_lfc;
    bool     hdr10_dolby_vision;
    bool     ddcci_backlight_ctrl;
    uint32_t active_refresh_hz;
    uint32_t registered_monitors;
    char     primary_monitor[64];
} display_engine_status_t;

void   display_hdr_vrr_init(void);
bool   display_hdr_vrr_get_status(display_engine_status_t *out);
size_t display_hdr_monitor_db_size(void);
bool   display_hdr_vrr_selftest(void);

#endif
