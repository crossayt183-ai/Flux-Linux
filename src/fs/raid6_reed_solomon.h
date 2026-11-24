#ifndef FLUX_RAID6_REED_SOLOMON_H
#define FLUX_RAID6_REED_SOLOMON_H

#include "types.h"

typedef struct {
    uint32_t stripe_rule_id;
    uint8_t  data_disks;
    uint8_t  parity_disks;
    uint16_t chunk_kb;
    const char *array_topology;
    const char *gf256_syndrome_op;
    const char *rebuild_strategy;
} raid6_stripe_rule_t;

void   raid6_reed_solomon_init(void);
void   raid6_encode_pq(int data_disks, size_t len, const uint8_t *const *data, uint8_t *p_out, uint8_t *q_out);
bool   raid6_recover_two_data(int data_disks, size_t len, int fail_a, int fail_b,
                              uint8_t **disks, const uint8_t *p_buf, const uint8_t *q_buf);
size_t raid6_stripe_db_size(void);
bool   raid6_reed_solomon_selftest(void);

#endif
