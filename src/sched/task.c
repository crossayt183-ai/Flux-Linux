#include "task.h"
#include "syscall.h"
#include "vfs.h"
#include "net.h"
#include "heap.h"
#include "gdt.h"
#include "timer.h"
#include "cpu.h"
#include "serial.h"
#include "string.h"

extern void task_kthread_trampoline(void);

typedef struct PACKED {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rip;
} switch_frame_t;

static task_t   *task_list_head = NULL;
static task_t   *current_task   = NULL;
static task_t   *idle_task      = NULL;
static uint64_t  next_pid       = 1;
static uint64_t  ctx_switches   = 0;
static spinlock_t sched_lock    = SPINLOCK_INIT;
static volatile bool sched_started = false;

static void init_default_fds(task_t *t) {
    memset(t->fds, 0, sizeof(t->fds));
    t->fds[0].type = FD_CONSOLE;
    t->fds[1].type = FD_CONSOLE;
    t->fds[2].type = FD_CONSOLE;
}

static task_t *alloc_task_struct(const char *name, uint8_t priority, vm_space_t *vm, bool is_user) {
    task_t *t = (task_t *)kcalloc(1, sizeof(task_t));
    if (!t) return NULL;

    uint64_t ks = (uint64_t)(uintptr_t)kmalloc(TASK_KSTACK_SIZE);
    if (!ks) {
        kfree(t);
        return NULL;
    }

    t->pid          = __atomic_fetch_add(&next_pid, 1, __ATOMIC_SEQ_CST);
    t->ppid         = current_task ? current_task->pid : 0;
    strncpy(t->name, name ? name : "task", sizeof(t->name) - 1);
    t->priority     = (priority > MAX_PRIO) ? MAX_PRIO : priority;
    t->quantum_left = (uint32_t)(t->priority + 1) * 2U;
    t->state        = TASK_READY;
    t->is_user      = is_user;
    t->kstack_base   = ks;
    t->kstack_top    = ks + TASK_KSTACK_SIZE;
    t->vm            = vm ? vm : vmm_kernel_space();
    t->sandbox_perms = 0xFFFFFFFFU;
    strcpy(t->sec_ctx, is_user ? "user_u:user_r:unconfined_t:s0" : "system_u:system_r:kernel_t:s0");
    t->sandbox_app[0] = '\0';
    *(uint16_t *)(t->fpu_state + 0)  = 0x037FU; /* default x87 CW */
    *(uint32_t *)(t->fpu_state + 24) = 0x1F80U; /* default SSE MXCSR (exceptions masked) */
    init_default_fds(t);
    return t;
}

static void enqueue_task(task_t *t) {
    spin_lock_irqsave(&sched_lock);
    t->next = task_list_head;
    task_list_head = t;
    spin_unlock_irqrestore(&sched_lock);
}

static task_t *pick_next_locked(void) {
    uint64_t now = timer_uptime_ms();

    /* Wake expired sleepers */
    for (task_t *t = task_list_head; t; t = t->next) {
        if (t->state == TASK_SLEEPING && now >= t->sleep_until_ms) {
            t->state = TASK_READY;
        }
    }

    /* Priority-aware Round-Robin: scan priority 3 -> 0 starting after current */
    task_t *best = NULL;
    for (int prio = MAX_PRIO; prio >= 0; prio--) {
        task_t *start = (current_task && current_task->next) ? current_task->next : task_list_head;
        task_t *cur = start;
        bool wrapped = false;
        while (cur) {
            if (cur != idle_task && cur->priority == (uint8_t)prio &&
                (cur->state == TASK_READY || cur->state == TASK_RUNNING)) {
                best = cur;
                break;
            }
            cur = cur->next;
            if (!cur && !wrapped) {
                cur = task_list_head;
                wrapped = true;
            }
            if (wrapped && cur == start) break;
        }
        if (best) break;
    }

    return best ? best : idle_task;
}

static void schedule_core(void) {
    if (!sched_started || !current_task) return;

    uint64_t rf = irq_save_disable();
    task_t *prev = current_task;
    task_t *next = pick_next_locked();

    if (!next || next == prev) {
        if (prev->state == TASK_RUNNING) {
            prev->quantum_left = (uint32_t)(prev->priority + 1) * 2U;
        }
        irq_restore(rf);
        return;
    }

    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }
    next->state = TASK_RUNNING;
    next->quantum_left = (uint32_t)(next->priority + 1) * 2U;
    current_task = next;
    ctx_switches++;

    __asm__ volatile ("fxsave %0" : "=m"(prev->fpu_state));
    tss_set_rsp0(next->kstack_top);
    syscall_set_kstack(next->kstack_top);
    vmm_switch_space(next->vm);
    context_switch(&prev->rsp, next->rsp);
    __asm__ volatile ("fxrstor %0" :: "m"(current_task->fpu_state));

    irq_restore(rf);
}

static void on_timer_tick(void) {
    if (!sched_started || !current_task) return;
    current_task->total_ticks++;
    if (current_task->quantum_left > 0) {
        current_task->quantum_left--;
    }
    if (current_task->quantum_left == 0) {
        schedule_core();
    }
}

void sched_init(void) {
    /* Wrap boot execution context as idle/boot task (pid 0) */
    idle_task = (task_t *)kcalloc(1, sizeof(task_t));
    idle_task->pid      = 0;
    strcpy(idle_task->name, "kernel-idle");
    idle_task->state    = TASK_RUNNING;
    idle_task->priority = 0;
    idle_task->quantum_left = 2;
    idle_task->vm       = vmm_kernel_space();
    idle_task->sandbox_perms = 0xFFFFFFFFU;
    strcpy(idle_task->sec_ctx, "system_u:system_r:kernel_t:s0");
    init_default_fds(idle_task);

    task_list_head = idle_task;
    current_task   = idle_task;
    sched_started  = true;

    timer_set_tick_hook(on_timer_tick);
    kprintf("[SCHED] Preemptive priority Round-Robin scheduler initialized\n");
}

task_t *task_current(void) {
    return current_task;
}

task_t *task_find_by_pid(uint64_t pid) {
    uint64_t rf = irq_save_disable();
    task_t *found = NULL;
    for (task_t *t = task_list_head; t; t = t->next) {
        if (t->pid == pid && t->state != TASK_UNUSED) {
            found = t;
            break;
        }
    }
    irq_restore(rf);
    return found;
}

task_t *task_create_kernel(const char *name, kthread_fn_t entry, void *arg, uint8_t priority) {
    task_t *t = alloc_task_struct(name, priority, vmm_kernel_space(), false);
    if (!t) return NULL;

    uint64_t sp = t->kstack_top - sizeof(switch_frame_t);
    switch_frame_t *sf = (switch_frame_t *)(uintptr_t)sp;
    memset(sf, 0, sizeof(*sf));
    sf->r12 = (uint64_t)(uintptr_t)entry;
    sf->r13 = (uint64_t)(uintptr_t)arg;
    sf->rip = (uint64_t)(uintptr_t)task_kthread_trampoline;
    t->rsp = sp;

    enqueue_task(t);
    return t;
}

task_t *task_create_user(const char *name, uint64_t user_rip, uint64_t user_rsp, vm_space_t *vm, uint8_t priority) {
    task_t *t = alloc_task_struct(name, priority, vm, true);
    if (!t) return NULL;

    uint64_t regs_sp = t->kstack_top - sizeof(cpu_regs_t);
    cpu_regs_t *regs = (cpu_regs_t *)(uintptr_t)regs_sp;
    memset(regs, 0, sizeof(*regs));
    regs->rip    = user_rip;
    regs->cs     = GDT_USER_CODE | 3;
    regs->rflags = 0x202ULL; /* IF=1 */
    regs->rsp    = user_rsp;
    regs->ss     = GDT_USER_DATA | 3;

    uint64_t sw_sp = regs_sp - sizeof(switch_frame_t);
    switch_frame_t *sf = (switch_frame_t *)(uintptr_t)sw_sp;
    memset(sf, 0, sizeof(*sf));
    sf->rip = (uint64_t)(uintptr_t)user_iret_trampoline;
    t->rsp = sw_sp;

    enqueue_task(t);
    return t;
}

task_t *task_fork_current(cpu_regs_t *regs) {
    task_t *parent = task_current();
    if (!parent || !parent->vm) return NULL;

    vm_space_t *child_vm = vmm_clone_cow(parent->vm);
    if (!child_vm) return NULL;

    task_t *child = alloc_task_struct(parent->name, parent->priority, child_vm, parent->is_user);
    if (!child) {
        vmm_destroy_space(child_vm);
        return NULL;
    }

    /* Clone open FDs & bump pipe/VFS refcounts */
    memcpy(child->fds, parent->fds, sizeof(parent->fds));
    memcpy(child->sec_ctx, parent->sec_ctx, sizeof(child->sec_ctx));
    child->sandbox_perms = parent->sandbox_perms;
    memcpy(child->sandbox_app, parent->sandbox_app, sizeof(child->sandbox_app));
    for (int i = 0; i < MAX_FDS; i++) {
        if (child->fds[i].pipe) {
            if (child->fds[i].type == FD_PIPE_READ)  child->fds[i].pipe->readers++;
            if (child->fds[i].type == FD_PIPE_WRITE) child->fds[i].pipe->writers++;
        }
        if (child->fds[i].type == FD_VFS_FILE && child->fds[i].vfs_node) {
            vfs_inode_ref(child->fds[i].vfs_node);
        }
        if (child->fds[i].type == FD_SOCKET) {
            sock_ref(child->fds[i].sock_id);
        }
    }

    uint64_t regs_sp = child->kstack_top - sizeof(cpu_regs_t);
    cpu_regs_t *cregs = (cpu_regs_t *)(uintptr_t)regs_sp;
    *cregs = *regs;
    cregs->rax = 0; /* child fork() returns 0 */

    uint64_t sw_sp = regs_sp - sizeof(switch_frame_t);
    switch_frame_t *sf = (switch_frame_t *)(uintptr_t)sw_sp;
    memset(sf, 0, sizeof(*sf));
    sf->rip = (uint64_t)(uintptr_t)user_iret_trampoline;
    child->rsp = sw_sp;

    enqueue_task(child);
    return child;
}

void task_yield(void) {
    schedule_core();
}

void task_sleep_ms(uint64_t ms) {
    uint64_t rf = irq_save_disable();
    task_t *t = current_task;
    if (t && t != idle_task) {
        t->sleep_until_ms = timer_uptime_ms() + ms;
        t->state = TASK_SLEEPING;
        irq_restore(rf);
        schedule_core();
        return;
    }
    irq_restore(rf);
    timer_sleep_ms(ms);
}

static void close_task_fds(task_t *t) {
    for (int i = 0; i < MAX_FDS; i++) {
        if (t->fds[i].pipe) {
            pipe_buf_t *p = t->fds[i].pipe;
            if (t->fds[i].type == FD_PIPE_READ && p->readers > 0) p->readers--;
            if (t->fds[i].type == FD_PIPE_WRITE && p->writers > 0) p->writers--;
            if (p->readers == 0 && p->writers == 0) kfree(p);
            t->fds[i].pipe = NULL;
        }
        if (t->fds[i].type == FD_VFS_FILE && t->fds[i].vfs_node) {
            vfs_inode_put(t->fds[i].vfs_node);
            t->fds[i].vfs_node = NULL;
        }
        if (t->fds[i].type == FD_SOCKET) {
            sock_close(t->fds[i].sock_id);
            t->fds[i].sock_id = -1;
        }
        t->fds[i].type = FD_NONE;
    }
}

void task_exit(int32_t code) {
    task_t *t = current_task;
    if (!t || t == idle_task) {
        kpanic("Idle task attempted exit");
    }
    close_task_fds(t);
    cli();
    t->exit_code = code;
    t->state = TASK_ZOMBIE;

    /* Wake parent if blocked in waitpid */
    for (task_t *p = task_list_head; p; p = p->next) {
        if (p->pid == t->ppid && p->state == TASK_BLOCKED) {
            p->state = TASK_READY;
        }
    }

    schedule_core();
    for (;;) hlt();
}

int64_t task_waitpid(int64_t pid, int32_t *status_out) {
    task_t *self = current_task;
    for (;;) {
        uint64_t rf = irq_save_disable();
        bool has_child = false;
        task_t *prev = NULL;
        for (task_t *t = task_list_head; t; prev = t, t = t->next) {
            if (t->ppid == self->pid && (pid == -1 || (int64_t)t->pid == pid)) {
                has_child = true;
                if (t->state == TASK_ZOMBIE) {
                    int64_t cpid = (int64_t)t->pid;
                    if (status_out) *status_out = t->exit_code;

                    if (prev) prev->next = t->next;
                    else task_list_head = t->next;

                    if (t->vm && t->vm != vmm_kernel_space()) {
                        vmm_destroy_space(t->vm);
                    }
                    if (t->kstack_base) kfree((void *)(uintptr_t)t->kstack_base);
                    kfree(t);
                    irq_restore(rf);
                    return cpid;
                }
            }
        }
        if (!has_child) {
            irq_restore(rf);
            return -1;
        }
        self->state = TASK_BLOCKED;
        irq_restore(rf);
        schedule_core();
    }
}

bool task_kill(uint64_t pid, int32_t code) {
    if (pid == 0) return false;
    uint64_t rf = irq_save_disable();
    for (task_t *t = task_list_head; t; t = t->next) {
        if (t->pid == pid && t->state != TASK_ZOMBIE) {
            if (t == current_task) {
                irq_restore(rf);
                task_exit(code);
            }
            t->exit_code = code;
            t->state = TASK_ZOMBIE;
            close_task_fds(t);
            for (task_t *p = task_list_head; p; p = p->next) {
                if (p->pid == t->ppid && p->state == TASK_BLOCKED) {
                    p->state = TASK_READY;
                }
            }
            irq_restore(rf);
            return true;
        }
    }
    irq_restore(rf);
    return false;
}

void task_block_current_locked(spinlock_t *lk) {
    if (current_task) {
        current_task->state = TASK_BLOCKED;
    }
    spin_unlock_irqrestore(lk);
    schedule_core();
}

void task_wake(task_t *t) {
    if (!t) return;
    uint64_t rf = irq_save_disable();
    if (t->state == TASK_BLOCKED || t->state == TASK_SLEEPING) {
        t->state = TASK_READY;
    }
    irq_restore(rf);
}

size_t sched_list_tasks(task_t **out_ptrs, size_t max_count) {
    uint64_t rf = irq_save_disable();
    size_t n = 0;
    for (task_t *t = task_list_head; t && n < max_count; t = t->next) {
        out_ptrs[n++] = t;
    }
    irq_restore(rf);
    return n;
}

uint64_t sched_context_switches(void) {
    return ctx_switches;
}
