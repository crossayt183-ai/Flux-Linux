#ifndef FLUX_RCU_LOCKLESS_EPOCH_H
#define FLUX_RCU_LOCKLESS_EPOCH_H

#include "types.h"

typedef struct {
    uint32_t rule_id;
    uint8_t  flavor; /* 1=Tree-RCU-Preempt, 2=SRCU-Sleepable, 3=Tasks-RCU-Trace, 4=Lockless-MPSC */
    uint8_t  batch_shift;
    uint16_t grace_target_us;
    const char *domain_name;
    const char *quiescent_detector;
    const char *memory_barrier_order;
} rcu_epoch_rule_t;

void     rcu_lockless_epoch_init(void);
void     flux_rcu_read_lock(void);
void     flux_rcu_read_unlock(void);
uint64_t flux_synchronize_rcu(void);
bool     lockfree_ring_push(uint64_t val);
bool     lockfree_ring_pop(uint64_t *out_val);
size_t   rcu_epoch_db_size(void);
bool     rcu_lockless_epoch_selftest(void);

#endif
