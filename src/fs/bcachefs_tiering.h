#ifndef FLUX_BCACHEFS_TIERING_H
#define FLUX_BCACHEFS_TIERING_H

#include "types.h"

typedef struct {
    uint32_t rule_id;
    uint16_t tier_code;
    uint16_t queue_depth;
    const char *profile_name;
    const char *io_pipeline_op;
    const char *durability_contract;
} bcachefs_tiering_rule_t;

void   bcachefs_tiering_init(void);
size_t bcachefs_tiering_db_size(void);
bool   bcachefs_tiering_selftest(void);

#endif
