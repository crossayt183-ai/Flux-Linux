#ifndef FLUX_KMS_ATOMIC_COLOR_H
#define FLUX_KMS_ATOMIC_COLOR_H

#include "types.h"

typedef struct {
    uint32_t kms_rule_id;
    uint8_t  plane_type; /* 1=Primary-Scanout, 2=Overlay-DirectScanout, 3=Hardware-Cursor, 4=Color-3DLUT-CTM */
    uint8_t  bpc_format; /* 8, 10, 12, 16 bpc */
    uint16_t refresh_target_hz;
    const char *drm_property_name;
    const char *atomic_commit_stage;
    const char *color_eotf_transform;
} kms_atomic_color_rule_t;

typedef struct {
    bool     atomic_nonblocking_flip;
    bool     fullscreen_direct_scanout;
    bool     hdr10_pq_ctm_3dlut_ready;
    bool     dsc12a_slice_compression;
    uint32_t registered_kms_rules;
} kms_atomic_color_status_t;

void   kms_atomic_color_init(void);
bool   kms_atomic_color_get_status(kms_atomic_color_status_t *out);
size_t kms_atomic_color_db_size(void);
bool   kms_atomic_color_selftest(void);

#endif
