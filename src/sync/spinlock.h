#ifndef FLUX_SPINLOCK_H
#define FLUX_SPINLOCK_H

#include "types.h"
#include "cpu.h"

typedef struct {
    volatile uint32_t locked;
    uint64_t saved_rflags;
} spinlock_t;

#define SPINLOCK_INIT { 0, 0 }

static inline void spinlock_init(spinlock_t *lk) {
    lk->locked = 0;
    lk->saved_rflags = 0;
}

static inline void spin_lock_irqsave(spinlock_t *lk) {
    uint64_t rf = irq_save_disable();
    while (__atomic_test_and_set(&lk->locked, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&lk->locked, __ATOMIC_RELAXED)) {
            __asm__ volatile ("pause");
        }
    }
    lk->saved_rflags = rf;
}

static inline void spin_unlock_irqrestore(spinlock_t *lk) {
    uint64_t rf = lk->saved_rflags;
    __atomic_clear(&lk->locked, __ATOMIC_RELEASE);
    irq_restore(rf);
}

#endif
