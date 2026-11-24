#ifndef FLUX_CEPH_GLUSTER_LUSTRE_H
#define FLUX_CEPH_GLUSTER_LUSTRE_H

#include "types.h"

typedef struct {
    uint32_t rule_id;
    uint16_t tier_code;
    uint16_t queue_depth;
    const char *profile_name;
    const char *io_pipeline_op;
    const char *durability_contract;
} ceph_gluster_lustre_rule_t;

void   ceph_gluster_lustre_init(void);
size_t ceph_gluster_lustre_db_size(void);
bool   ceph_gluster_lustre_selftest(void);

#endif
