#ifndef FLUX_TPM2_IMA_EVM_H
#define FLUX_TPM2_IMA_EVM_H

#include "types.h"

typedef struct {
    uint32_t ima_rule_id;
    uint8_t  pcr_index;   /* PCR 0..23 */
    uint8_t  hash_algo;   /* 1=SHA256, 2=SHA384, 3=SHA512, 4=SM3-256, 5=BLAKE3 */
    uint16_t policy_mask;
    const char *measured_object;
    const char *signature_scheme;
    const char *appraisal_verdict;
} tpm2_ima_evm_rule_t;

typedef struct {
    bool     tpm20_crb_pcr_online;
    bool     ima_appraisal_enforcing;
    bool     evm_hmac_metadata_sealed;
    bool     fsverity_merkle_active;
    uint32_t pcr_extends_count;
    uint32_t total_ima_rules;
} tpm2_ima_evm_status_t;

void   tpm2_ima_evm_init(void);
bool   tpm2_ima_evm_get_status(tpm2_ima_evm_status_t *out);
size_t tpm2_ima_evm_db_size(void);
bool   tpm2_ima_evm_selftest(void);

#endif
