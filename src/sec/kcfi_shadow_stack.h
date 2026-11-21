#ifndef FLUX_KCFI_SHADOW_STACK_H
#define FLUX_KCFI_SHADOW_STACK_H

#include "types.h"

typedef struct {
    uint32_t cfi_rule_id;
    uint8_t  defense_layer; /* 1=kCFI-FineIBT, 2=CET-ShadowStack-IBT, 3=SMEP-SMAP-UMIP-KPTI, 4=SLAB-Hardened-Canary */
    uint8_t  trap_vector;
    uint32_t type_hash_tag;
    const char *protected_target;
    const char *hardware_or_compiler_check;
    const char *exploit_class_blocked;
} kcfi_cet_rule_t;

typedef struct {
    bool     kcfi_fineibt_enforced;
    bool     cet_shadow_stack_ibt;
    bool     smep_smap_umip_kpti_locked;
    bool     slab_freelist_xor_canary;
    uint32_t verified_call_targets;
} kcfi_shadow_stack_status_t;

void   kcfi_shadow_stack_init(void);
bool   kcfi_shadow_stack_get_status(kcfi_shadow_stack_status_t *out);
size_t kcfi_shadow_stack_db_size(void);
bool   kcfi_shadow_stack_selftest(void);

#endif
