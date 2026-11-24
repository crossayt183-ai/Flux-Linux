#ifndef FLUX_UNKILLABLE_FAULT_HEALER_H
#define FLUX_UNKILLABLE_FAULT_HEALER_H

#include "types.h"

#define LOCKDEP_MAX_CLASSES 16U

typedef struct {
    uint32_t heal_id;
    uint8_t  fault_domain; /* 1=Extable-SafeProbe, 2=Lockdep-Cycle, 3=IRQ-Storm-Dampener, 4=OOM-Immunity */
    uint8_t  recovery_op;
    uint16_t max_retries;
    const char *fault_signature;
    const char *containment_barrier;
    const char *continuity_action;
} unkillable_heal_rule_t;

void   unkillable_fault_healer_init(void);
bool   kernel_safe_peek64(uint64_t unsafe_va, uint64_t *out_val);
bool   lockdep_register_edge(uint32_t held_lock_id, uint32_t next_lock_id);
size_t unkillable_heal_db_size(void);
bool   unkillable_fault_healer_selftest(void);

#endif
