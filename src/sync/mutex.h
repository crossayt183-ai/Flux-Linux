#ifndef FLUX_MUTEX_H
#define FLUX_MUTEX_H

#include "types.h"
#include "spinlock.h"

typedef struct task task_t;

typedef struct mutex_waiter {
    task_t *task;
    struct mutex_waiter *next;
} mutex_waiter_t;

typedef struct {
    volatile uint32_t locked;
    task_t *owner;
    spinlock_t guard;
    mutex_waiter_t *wait_head;
    mutex_waiter_t *wait_tail;
} mutex_t;

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
bool mutex_trylock(mutex_t *m);
void mutex_unlock(mutex_t *m);

#endif
