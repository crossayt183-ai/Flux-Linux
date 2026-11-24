#ifndef FLUX_VFS_DAX_SPLICE_LOCK_H
#define FLUX_VFS_DAX_SPLICE_LOCK_H

#include "types.h"

typedef struct {
    uint32_t rule_id;
    uint16_t tier_code;
    uint16_t queue_depth;
    const char *profile_name;
    const char *io_pipeline_op;
    const char *durability_contract;
} vfs_dax_splice_lock_rule_t;

void   vfs_dax_splice_lock_init(void);
size_t vfs_dax_splice_lock_db_size(void);
bool   vfs_dax_splice_lock_selftest(void);

#endif
