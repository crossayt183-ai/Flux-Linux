#ifndef FLUX_ZERO_TRUST_ATTEST_AUDIT_H
#define FLUX_ZERO_TRUST_ATTEST_AUDIT_H

#include "types.h"

typedef struct {
    uint32_t policy_id;
    uint8_t  trust_tier;
    uint8_t  audit_Class;
    uint16_t max_rate_per_sec;
    const char *attested_region;
    const char *merkle_chain_rule;
    const char *tamper_verdict;
} zero_trust_attest_rule_t;

void   zero_trust_attest_audit_init(void);
void   flux_sha256_compute(const void *data, size_t len, uint8_t out_hash[32]);
bool   zero_trust_verify_kernel_text(void);
void   zero_trust_append_audit(const char *event_msg);
size_t zero_trust_attest_db_size(void);
bool   zero_trust_attest_audit_selftest(void);

#endif
