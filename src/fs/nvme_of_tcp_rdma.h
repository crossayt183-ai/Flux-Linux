#ifndef FLUX_NVME_OF_TCP_RDMA_H
#define FLUX_NVME_OF_TCP_RDMA_H

#include "types.h"

typedef struct {
    uint32_t rule_id;
    uint16_t tier_code;
    uint16_t queue_depth;
    const char *profile_name;
    const char *io_pipeline_op;
    const char *durability_contract;
} nvme_of_tcp_rdma_rule_t;

void   nvme_of_tcp_rdma_init(void);
size_t nvme_of_tcp_rdma_db_size(void);
bool   nvme_of_tcp_rdma_selftest(void);

#endif
