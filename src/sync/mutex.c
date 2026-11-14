#include "mutex.h"
#include "task.h"

void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->owner = NULL;
    spinlock_init(&m->guard);
    m->wait_head = NULL;
    m->wait_tail = NULL;
}

bool mutex_trylock(mutex_t *m) {
    spin_lock_irqsave(&m->guard);
    bool ok = false;
    if (!m->locked) {
        m->locked = 1;
        m->owner = task_current();
        ok = true;
    }
    spin_unlock_irqrestore(&m->guard);
    return ok;
}

void mutex_lock(mutex_t *m) {
    for (;;) {
        spin_lock_irqsave(&m->guard);
        if (!m->locked) {
            m->locked = 1;
            m->owner = task_current();
            spin_unlock_irqrestore(&m->guard);
            return;
        }
        task_t *self = task_current();
        if (self) {
            mutex_waiter_t w = { .task = self, .next = NULL };
            if (m->wait_tail) m->wait_tail->next = &w;
            else m->wait_head = &w;
            m->wait_tail = &w;
            task_block_current_locked(&m->guard);
            /* guard unlocked by task_block_current_locked / wake path */
        } else {
            spin_unlock_irqrestore(&m->guard);
            __asm__ volatile ("pause");
        }
    }
}

void mutex_unlock(mutex_t *m) {
    spin_lock_irqsave(&m->guard);
    m->locked = 0;
    m->owner = NULL;
    mutex_waiter_t *w = m->wait_head;
    if (w) {
        m->wait_head = w->next;
        if (!m->wait_head) m->wait_tail = NULL;
        task_t *t = w->task;
        spin_unlock_irqrestore(&m->guard);
        task_wake(t);
        return;
    }
    spin_unlock_irqrestore(&m->guard);
}
