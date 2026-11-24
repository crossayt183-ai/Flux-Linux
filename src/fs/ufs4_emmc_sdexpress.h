#ifndef FLUX_UFS4_EMMC_SDEXPRESS_H
#define FLUX_UFS4_EMMC_SDEXPRESS_H

#include "types.h"

typedef struct {
    uint32_t rule_id;
    uint16_t tier_code;
    uint16_t queue_depth;
    const char *profile_name;
    const char *io_pipeline_op;
    const char *durability_contract;
} ufs4_emmc_sdexpress_rule_t;

void   ufs4_emmc_sdexpress_init(void);
size_t ufs4_emmc_sdexpress_db_size(void);
bool   ufs4_emmc_sdexpress_selftest(void);

#endif
