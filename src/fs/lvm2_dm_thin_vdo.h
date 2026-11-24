#ifndef FLUX_LVM2_DM_THIN_VDO_H
#define FLUX_LVM2_DM_THIN_VDO_H

#include "types.h"

typedef struct {
    uint32_t rule_id;
    uint16_t tier_code;
    uint16_t queue_depth;
    const char *profile_name;
    const char *io_pipeline_op;
    const char *durability_contract;
} lvm2_dm_thin_vdo_rule_t;

void   lvm2_dm_thin_vdo_init(void);
size_t lvm2_dm_thin_vdo_db_size(void);
bool   lvm2_dm_thin_vdo_selftest(void);

#endif
