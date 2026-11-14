#ifndef FLUX_TASK_H
#define FLUX_TASK_H

#include "types.h"
#include "vmm.h"
#include "idt.h"
#include "spinlock.h"

#define TASK_KSTACK_SIZE 16384
#define MAX_TASKS        64
#define MAX_FDS          16
#define MAX_PRIO         3

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_ZOMBIE
} task_state_t;

typedef enum {
    FD_NONE = 0,
    FD_CONSOLE,
    FD_PIPE_READ,
    FD_PIPE_WRITE,
    FD_MEMFILE,
    FD_VFS_FILE,
    FD_SOCKET
} fd_type_t;

struct vfs_inode;

typedef struct pipe_buf {
    uint8_t  data[1024];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    uint32_t readers;
    uint32_t writers;
    spinlock_t lock;
} pipe_buf_t;

typedef struct {
    fd_type_t         type;
    uint32_t          flags;
    uint64_t          offset;
    pipe_buf_t       *pipe;
    struct vfs_inode *vfs_node;
    int32_t           sock_id;
    const uint8_t    *mem_data;
    size_t            mem_size;
} fd_entry_t;

typedef struct task {
    uint64_t     pid;
    uint64_t     ppid;
    char         name[32];
    task_state_t state;
    uint8_t      priority;
    uint32_t     quantum_left;
    uint64_t     sleep_until_ms;
    int32_t      exit_code;
    bool         is_user;

    uint64_t     rsp;
    uint64_t     kstack_base;
    uint64_t     kstack_top;
    vm_space_t  *vm;
    uint8_t      fpu_state[512] ALIGNED(16);

    fd_entry_t   fds[MAX_FDS];
    uint64_t     total_ticks;

    /* SELinux MAC context & Flatpak sandbox state */
    char         sec_ctx[40];
    uint32_t     sandbox_perms;
    char         sandbox_app[32];

    struct task *next;
} task_t;

typedef void (*kthread_fn_t)(void *arg);

void    sched_init(void);
task_t *task_current(void);
task_t *task_find_by_pid(uint64_t pid);
task_t *task_create_kernel(const char *name, kthread_fn_t entry, void *arg, uint8_t priority);
task_t *task_create_user(const char *name, uint64_t user_rip, uint64_t user_rsp, vm_space_t *vm, uint8_t priority);
task_t *task_fork_current(cpu_regs_t *regs);

void    task_yield(void);
void    task_sleep_ms(uint64_t ms);
void    task_exit(int32_t code) NORETURN;
int64_t task_waitpid(int64_t pid, int32_t *status_out);
bool    task_kill(uint64_t pid, int32_t code);

void    task_block_current_locked(spinlock_t *lk);
void    task_wake(task_t *t);

size_t  sched_list_tasks(task_t **out_ptrs, size_t max_count);
uint64_t sched_context_switches(void);

/* Assembly helpers */
extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
extern void user_iret_trampoline(void);

#endif
