#include "syscall.h"
#include "security.h"
#include "wayland.h"
#include "firewall.h"
#include "task.h"
#include "vfs.h"
#include "net.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "gdt.h"
#include "cpu.h"
#include "io.h"
#include "timer.h"
#include "serial.h"
#include "string.h"

#define MSR_EFER           0xC0000080U
#define MSR_STAR           0xC0000081U
#define MSR_LSTAR          0xC0000082U
#define MSR_FMASK          0xC0000084U
#define MSR_GS_BASE        0xC0000101U
#define MSR_KERNEL_GS_BASE 0xC0000102U

#define USER_MMAP_BASE     0x0000002000000000ULL
#define USER_MMAP_LIMIT    0x0000006000000000ULL

extern void syscall_fast_entry(void);

/* Referenced by syscall_entry.S via GS:[0] and GS:[8] after swapgs */
typedef struct PACKED {
    uint64_t kstack_top;
    uint64_t scratch_user_rsp;
} syscall_cpu_ctx_t;

static syscall_cpu_ctx_t syscall_cpu_ctx ALIGNED(16);
static console_sink_fn_t console_sink = NULL;

/* Optional late-bound Stage 3/4 bridges defined in syscall.h */
static fb_info_hook_t    g_fb_info_hook = NULL;
static audio_note_hook_t g_audio_note_hook = NULL;

void syscall_register_fb_hook(fb_info_hook_t fn) {
    g_fb_info_hook = fn;
}

void syscall_register_audio_hook(audio_note_hook_t fn) {
    g_audio_note_hook = fn;
}

void syscall_set_console_sink(console_sink_fn_t fn) {
    console_sink = fn;
}

void syscall_set_kstack(uint64_t kstack_top) {
    syscall_cpu_ctx.kstack_top = kstack_top;
}

void syscall_init(void) {
    memset(&syscall_cpu_ctx, 0, sizeof(syscall_cpu_ctx));
    task_t *cur = task_current();
    if (cur) {
        syscall_cpu_ctx.kstack_top = cur->kstack_top;
    }

    /* Enable SYSCALL/SYSRET extension (EFER.SCE = bit 0) */
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1ULL);

    /* STAR[47:32] = KCODE (0x08), STAR[63:48] = KDATA (0x10) -> UDATA=0x1B, UCODE=0x23 */
    uint64_t star = ((uint64_t)GDT_KERNEL_CODE << 32) | ((uint64_t)GDT_KERNEL_DATA << 48);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_fast_entry);
    /* Mask IF (0x200), TF (0x100), DF (0x400) on SYSCALL entry */
    wrmsr(MSR_FMASK, 0x700ULL);

    wrmsr(MSR_GS_BASE, 0);
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)(uintptr_t)&syscall_cpu_ctx);

    irq_register(128, syscall_dispatch);
    kprintf("[SYSCALL] SYSCALL/SYSRET & int 0x80 online (44 syscalls)\n");
}

static bool copy_from_user(void *dst, uint64_t uaddr, size_t len) {
    task_t *t = task_current();
    if (!t || !t->vm || len == 0 || len > (16ULL << 20)) return false;
    if (!vmm_user_range_valid(t->vm, uaddr, len, false)) return false;
    memcpy(dst, (const void *)(uintptr_t)uaddr, len);
    return true;
}

static bool copy_to_user(uint64_t uaddr, const void *src, size_t len) {
    task_t *t = task_current();
    if (!t || !t->vm || len == 0 || len > (16ULL << 20)) return false;
    if (!vmm_user_range_valid(t->vm, uaddr, len, true)) return false;
    memcpy((void *)(uintptr_t)uaddr, src, len);
    return true;
}

static bool copy_user_str(char *dst, uint64_t uaddr, size_t max_len) {
    task_t *t = task_current();
    if (!t || !t->vm || max_len == 0) return false;
    for (size_t i = 0; i < max_len - 1; i++) {
        if (!vmm_user_range_valid(t->vm, uaddr + i, 1, false)) return false;
        char c = *(const volatile char *)(uintptr_t)(uaddr + i);
        dst[i] = c;
        if (c == '\0') return true;
    }
    dst[max_len - 1] = '\0';
    return true;
}

typedef struct {
    const char *path;
    const char *data;
} builtin_memfile_t;

static const builtin_memfile_t builtin_files[] = {
    { "/etc/motd",     "FluxOS x86_64 v0.2 -- Preemptive Ring3 + COW VMM + GUI + Synth\n" },
    { "/proc/version", "FluxOS 0.2.0 (x86_64, Limine, Zig/Clang freestanding)\n" },
    { "/etc/hostname", "fluxbox\n" }
};

static int alloc_fd(task_t *t, int min_fd) {
    for (int i = min_fd; i < MAX_FDS; i++) {
        if (t->fds[i].type == FD_NONE) return i;
    }
    return -1;
}

static void close_fd_slot(task_t *t, int fd) {
    if (fd < 0 || fd >= MAX_FDS) return;
    fd_entry_t *e = &t->fds[fd];
    if (e->pipe) {
        pipe_buf_t *p = e->pipe;
        spin_lock_irqsave(&p->lock);
        if (e->type == FD_PIPE_READ && p->readers > 0) p->readers--;
        if (e->type == FD_PIPE_WRITE && p->writers > 0) p->writers--;
        bool empty = (p->readers == 0 && p->writers == 0);
        spin_unlock_irqrestore(&p->lock);
        if (empty) kfree(p);
    }
    if (e->type == FD_VFS_FILE && e->vfs_node) {
        vfs_inode_put(e->vfs_node);
    }
    if (e->type == FD_SOCKET) {
        sock_close(e->sock_id);
    }
    memset(e, 0, sizeof(*e));
}

static int get_sock_id(task_t *t, int fd) {
    if (!t || fd < 0 || fd >= MAX_FDS) return -1;
    if (t->fds[fd].type != FD_SOCKET) return -1;
    return t->fds[fd].sock_id;
}

static int64_t sys_write(int fd, uint64_t ubuf, size_t len) {
    task_t *t = task_current();
    if (!t || fd < 0 || fd >= MAX_FDS) return -1;
    fd_entry_t *e = &t->fds[fd];
    if (e->type == FD_NONE || e->type == FD_MEMFILE) return -1;
    if (len == 0) return 0;
    if (len > 4096) len = 4096;

    char *kbuf = (char *)kmalloc(len + 1);
    if (!kbuf) return -1;
    if (!copy_from_user(kbuf, ubuf, len)) {
        kfree(kbuf);
        return -1;
    }
    kbuf[len] = '\0';

    if (e->type == FD_CONSOLE) {
        for (size_t i = 0; i < len; i++) serial_putc(kbuf[i]);
        if (console_sink) console_sink(kbuf, len);
        kfree(kbuf);
        return (int64_t)len;
    }

    if (e->type == FD_VFS_FILE && e->vfs_node) {
        if ((e->flags & 3U) == O_RDONLY) {
            kfree(kbuf);
            return -1;
        }
        if (e->flags & O_APPEND) e->offset = e->vfs_node->size;
        int64_t w = vfs_write(e->vfs_node, e->offset, kbuf, len);
        if (w > 0) e->offset += (uint64_t)w;
        kfree(kbuf);
        return w;
    }

    if (e->type == FD_PIPE_WRITE && e->pipe) {
        pipe_buf_t *p = e->pipe;
        size_t written = 0;
        while (written < len) {
            spin_lock_irqsave(&p->lock);
            if (p->readers == 0) {
                spin_unlock_irqrestore(&p->lock);
                kfree(kbuf);
                return written ? (int64_t)written : -1;
            }
            while (written < len && p->count < sizeof(p->data)) {
                p->data[p->write_pos] = (uint8_t)kbuf[written++];
                p->write_pos = (p->write_pos + 1) % (uint32_t)sizeof(p->data);
                p->count++;
            }
            spin_unlock_irqrestore(&p->lock);
            if (written < len) task_sleep_ms(2);
        }
        kfree(kbuf);
        return (int64_t)written;
    }

    if (e->type == FD_SOCKET) {
        int64_t s = sock_sendto(e->sock_id, kbuf, len, 0, 0);
        kfree(kbuf);
        return s;
    }

    kfree(kbuf);
    return -1;
}

static int64_t sys_read(int fd, uint64_t ubuf, size_t len) {
    task_t *t = task_current();
    if (!t || fd < 0 || fd >= MAX_FDS) return -1;
    fd_entry_t *e = &t->fds[fd];
    if (e->type == FD_NONE || e->type == FD_PIPE_WRITE) return -1;
    if (len == 0) return 0;
    if (len > 4096) len = 4096;

    if (e->type == FD_MEMFILE) {
        if (e->offset >= e->mem_size) return 0;
        size_t avail = e->mem_size - (size_t)e->offset;
        size_t n = MIN(avail, len);
        if (!copy_to_user(ubuf, e->mem_data + e->offset, n)) return -1;
        e->offset += n;
        return (int64_t)n;
    }

    if (e->type == FD_VFS_FILE && e->vfs_node) {
        char *kbuf = (char *)kmalloc(len);
        if (!kbuf) return -1;
        int64_t r = vfs_read(e->vfs_node, e->offset, kbuf, len);
        if (r > 0) {
            if (!copy_to_user(ubuf, kbuf, (size_t)r)) {
                kfree(kbuf);
                return -1;
            }
            e->offset += (uint64_t)r;
        }
        kfree(kbuf);
        return r;
    }

    if (e->type == FD_PIPE_READ && e->pipe) {
        pipe_buf_t *p = e->pipe;
        char *kbuf = (char *)kmalloc(len);
        if (!kbuf) return -1;
        size_t got = 0;
        for (;;) {
            spin_lock_irqsave(&p->lock);
            while (got < len && p->count > 0) {
                kbuf[got++] = (char)p->data[p->read_pos];
                p->read_pos = (p->read_pos + 1) % (uint32_t)sizeof(p->data);
                p->count--;
            }
            uint32_t writers = p->writers;
            spin_unlock_irqrestore(&p->lock);
            if (got > 0 || writers == 0) break;
            task_sleep_ms(2);
        }
        bool ok = (got == 0) || copy_to_user(ubuf, kbuf, got);
        kfree(kbuf);
        return ok ? (int64_t)got : -1;
    }

    if (e->type == FD_SOCKET) {
        char *kbuf = (char *)kmalloc(len);
        if (!kbuf) return -1;
        int64_t r = sock_recvfrom(e->sock_id, kbuf, len, NULL, NULL, 5000);
        if (r > 0 && !copy_to_user(ubuf, kbuf, (size_t)r)) {
            kfree(kbuf);
            return -1;
        }
        kfree(kbuf);
        return r;
    }

    return 0;
}

static int64_t sys_open(uint64_t upath, uint32_t flags) {
    char path[96];
    if (!copy_user_str(path, upath, sizeof(path))) return -1;

    task_t *t = task_current();
    if ((flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) != 0) {
        if (selinux_check_fs(t, path, true, false) < 0) {
            return -1;
        }
    }

    int fd = alloc_fd(t, 0);
    if (fd < 0) return -1;

    if (strcmp(path, "/dev/console") == 0 || strcmp(path, "/dev/tty") == 0) {
        t->fds[fd].type = FD_CONSOLE;
        t->fds[fd].flags = flags;
        return fd;
    }

    if (vfs_is_mounted()) {
        vfs_inode_t *node = vfs_open_path(path, (flags & O_CREAT) != 0, (flags & O_TRUNC) != 0, NULL);
        if (node) {
            if (node->type != VFS_FILE) {
                vfs_inode_put(node);
                return -1;
            }
            t->fds[fd].type     = FD_VFS_FILE;
            t->fds[fd].flags    = flags;
            t->fds[fd].offset   = (flags & O_APPEND) ? node->size : 0;
            t->fds[fd].vfs_node = node;
            return fd;
        }
    }

    for (size_t i = 0; i < ARRAY_LEN(builtin_files); i++) {
        if (strcmp(path, builtin_files[i].path) == 0) {
            t->fds[fd].type     = FD_MEMFILE;
            t->fds[fd].flags    = flags;
            t->fds[fd].offset   = 0;
            t->fds[fd].mem_data = (const uint8_t *)builtin_files[i].data;
            t->fds[fd].mem_size = strlen(builtin_files[i].data);
            return fd;
        }
    }
    return -1;
}

static int64_t sys_pipe(uint64_t upipefd) {
    task_t *t = task_current();
    if (!t || !vmm_user_range_valid(t->vm, upipefd, sizeof(int32_t) * 2, true)) return -1;

    int rfd = alloc_fd(t, 0);
    if (rfd < 0) return -1;
    t->fds[rfd].type = FD_PIPE_READ;

    int wfd = alloc_fd(t, rfd + 1);
    if (wfd < 0) {
        t->fds[rfd].type = FD_NONE;
        return -1;
    }

    pipe_buf_t *p = (pipe_buf_t *)kcalloc(1, sizeof(pipe_buf_t));
    if (!p) {
        t->fds[rfd].type = FD_NONE;
        return -1;
    }
    p->readers = 1;
    p->writers = 1;

    t->fds[rfd].type = FD_PIPE_READ;
    t->fds[rfd].pipe = p;
    t->fds[wfd].type = FD_PIPE_WRITE;
    t->fds[wfd].pipe = p;

    int32_t pair[2] = { rfd, wfd };
    if (!copy_to_user(upipefd, pair, sizeof(pair))) {
        close_fd_slot(t, rfd);
        close_fd_slot(t, wfd);
        return -1;
    }
    return 0;
}

static int64_t sys_dup2(int oldfd, int newfd) {
    task_t *t = task_current();
    if (!t || oldfd < 0 || oldfd >= MAX_FDS || newfd < 0 || newfd >= MAX_FDS) return -1;
    if (t->fds[oldfd].type == FD_NONE) return -1;
    if (oldfd == newfd) return newfd;

    close_fd_slot(t, newfd);
    t->fds[newfd] = t->fds[oldfd];
    if (t->fds[newfd].pipe) {
        spin_lock_irqsave(&t->fds[newfd].pipe->lock);
        if (t->fds[newfd].type == FD_PIPE_READ)  t->fds[newfd].pipe->readers++;
        if (t->fds[newfd].type == FD_PIPE_WRITE) t->fds[newfd].pipe->writers++;
        spin_unlock_irqrestore(&t->fds[newfd].pipe->lock);
    }
    if (t->fds[newfd].type == FD_VFS_FILE && t->fds[newfd].vfs_node) {
        vfs_inode_ref(t->fds[newfd].vfs_node);
    }
    if (t->fds[newfd].type == FD_SOCKET) {
        sock_ref(t->fds[newfd].sock_id);
    }
    return newfd;
}

static int64_t sys_brk(uint64_t new_brk) {
    task_t *t = task_current();
    vm_space_t *vm = t ? t->vm : NULL;
    if (!vm) return -1;
    if (new_brk == 0) return (int64_t)vm->brk_curr;
    if (new_brk < vm->brk_start || new_brk > vm->brk_start + (64ULL << 20)) return -1;

    uint64_t old_end = ALIGN_UP(vm->brk_curr, PAGE_SIZE);
    uint64_t new_end = ALIGN_UP(new_brk, PAGE_SIZE);
    if (new_end > old_end) {
        /* Lazy demand-zero VMA: physical pages faulted on first touch */
        if (!vmm_add_vma(vm, old_end, new_end, VMA_READ | VMA_WRITE | VMA_USER, VMA_DATA)) {
            return -1;
        }
    } else if (new_end < old_end) {
        vmm_remove_vma(vm, new_end, old_end);
    }
    vm->brk_curr = new_brk;
    return (int64_t)vm->brk_curr;
}

static bool range_overlaps_vma(vm_space_t *vm, uint64_t start, uint64_t end) {
    for (vma_region_t *v = vm->vmas; v; v = v->next) {
        if (start < v->end && end > v->start) return true;
    }
    return false;
}

static int64_t sys_mmap(uint64_t addr, size_t len, uint32_t prot, uint32_t flags UNUSED) {
    task_t *t = task_current();
    vm_space_t *vm = t ? t->vm : NULL;
    if (!vm || len == 0 || len > (64ULL << 20)) return -1;

    size_t alen = ALIGN_UP(len, PAGE_SIZE);
    uint64_t target = ALIGN_DOWN(addr, PAGE_SIZE);

    if (target == 0) {
        target = USER_MMAP_BASE;
        while (target + alen <= USER_MMAP_LIMIT && range_overlaps_vma(vm, target, target + alen)) {
            target += alen + PAGE_SIZE;
        }
    }
    if (target < 0x10000ULL || target + alen > USER_SPACE_MAX) return -1;
    if (range_overlaps_vma(vm, target, target + alen)) return -1;

    uint32_t vflags = VMA_USER;
    if (prot & PROT_READ)  vflags |= VMA_READ;
    if (prot & PROT_WRITE) vflags |= VMA_WRITE;
    if (prot & PROT_EXEC)  vflags |= VMA_EXEC;
    if (vflags == VMA_USER) vflags |= VMA_READ;

    if (!vmm_add_vma(vm, target, target + alen, vflags, VMA_ANON)) return -1;
    return (int64_t)target;
}

static int64_t sys_munmap(uint64_t addr, size_t len) {
    task_t *t = task_current();
    if (!t || !t->vm || len == 0) return -1;
    return vmm_remove_vma(t->vm, addr, addr + ALIGN_UP(len, PAGE_SIZE)) ? 0 : -1;
}

cpu_regs_t *syscall_dispatch(cpu_regs_t *regs) {
    /* Allow interrupts during blocking syscalls; cli before iretq return */
    sti();

    task_t *cur = task_current();
    uint64_t nr = regs->rax;
    uint64_t a1 = regs->rdi;
    uint64_t a2 = regs->rsi;
    uint64_t a3 = regs->rdx;
    uint64_t a4 = regs->r10;
    uint64_t a5 = regs->r8;
    uint64_t a6 = regs->r9;
    int64_t ret = -1;

    switch (nr) {
    case SYS_EXIT:
        task_exit((int32_t)a1);
        break;
    case SYS_WRITE:
        ret = sys_write((int)a1, a2, (size_t)a3);
        break;
    case SYS_READ:
        ret = sys_read((int)a1, a2, (size_t)a3);
        break;
    case SYS_OPEN:
        ret = sys_open(a1, (uint32_t)a2);
        break;
    case SYS_CLOSE:
        close_fd_slot(cur, (int)a1);
        ret = 0;
        break;
    case SYS_GETPID:
        ret = cur ? (int64_t)cur->pid : 0;
        break;
    case SYS_GETPPID:
        ret = cur ? (int64_t)cur->ppid : 0;
        break;
    case SYS_FORK: {
        task_t *child = task_fork_current(regs);
        ret = child ? (int64_t)child->pid : -1;
        break;
    }
    case SYS_EXECVE: {
        char path[96];
        if (copy_user_str(path, a1, sizeof(path))) {
            if (a2 == 0) {
                ret = user_replace_current(path, regs);
            } else {
                char args[12][96];
                const char *argv_ptrs[13];
                int argc = 0;
                for (; argc < 12; argc++) {
                    uint64_t up = 0;
                    if (!copy_from_user(&up, a2 + (uint64_t)argc * sizeof(uint64_t), sizeof(up)) || up == 0) {
                        break;
                    }
                    if (!copy_user_str(args[argc], up, sizeof(args[argc]))) break;
                    argv_ptrs[argc] = args[argc];
                }
                argv_ptrs[argc] = NULL;
                ret = user_replace_current_argv(path, argc, argv_ptrs, regs);
            }
            if (ret == 0) {
                cli();
                return regs;
            }
        }
        ret = -1;
        break;
    }
    case SYS_WAITPID: {
        int32_t st = 0;
        int64_t wpid = task_waitpid((int64_t)a1, &st);
        if (wpid > 0 && a2 != 0) {
            if (!copy_to_user(a2, &st, sizeof(st))) wpid = -1;
        }
        ret = wpid;
        break;
    }
    case SYS_YIELD:
        task_yield();
        ret = 0;
        break;
    case SYS_SLEEP_MS:
        task_sleep_ms(a1);
        ret = 0;
        break;
    case SYS_BRK:
        ret = sys_brk(a1);
        break;
    case SYS_MMAP:
        ret = sys_mmap(a1, (size_t)a2, (uint32_t)a3, (uint32_t)a4);
        break;
    case SYS_MUNMAP:
        ret = sys_munmap(a1, (size_t)a2);
        break;
    case SYS_PIPE:
        ret = sys_pipe(a1);
        break;
    case SYS_DUP2:
        ret = sys_dup2((int)a1, (int)a2);
        break;
    case SYS_UPTIME_MS:
        ret = (int64_t)timer_uptime_ms();
        break;
    case SYS_GETTASKS: {
        size_t max_n = MIN((size_t)a2, MAX_TASKS);
        if (a1 && max_n > 0) {
            task_t *ptrs[MAX_TASKS];
            size_t count = sched_list_tasks(ptrs, max_n);
            sys_taskinfo_t info[MAX_TASKS];
            for (size_t i = 0; i < count; i++) {
                info[i].pid = ptrs[i]->pid;
                info[i].ppid = ptrs[i]->ppid;
                info[i].state = (uint8_t)ptrs[i]->state;
                info[i].priority = ptrs[i]->priority;
                info[i].is_user = ptrs[i]->is_user ? 1 : 0;
                info[i].reserved = 0;
                info[i].ticks = ptrs[i]->total_ticks;
                strncpy(info[i].name, ptrs[i]->name, sizeof(info[i].name) - 1);
                info[i].name[sizeof(info[i].name) - 1] = '\0';
            }
            ret = copy_to_user(a1, info, count * sizeof(sys_taskinfo_t)) ? (int64_t)count : -1;
        }
        break;
    }
    case SYS_KILL:
        ret = task_kill(a1, (int32_t)a2) ? 0 : -1;
        break;
    case SYS_MEMINFO: {
        pmm_stats_t ps;
        heap_stats_t hs;
        pmm_get_stats(&ps);
        heap_get_stats(&hs);
        sys_meminfo_t mi = {
            .total_pages = ps.total_pages,
            .free_pages  = ps.free_pages,
            .used_pages  = ps.used_pages,
            .heap_used   = hs.bytes_allocated,
            .cow_faults  = (cur && cur->vm) ? cur->vm->cow_faults : 0,
            .lazy_faults = (cur && cur->vm) ? cur->vm->lazy_faults : 0
        };
        ret = copy_to_user(a1, &mi, sizeof(mi)) ? 0 : -1;
        break;
    }
    case SYS_REBOOT:
        outb(0x64, 0xFE);
        outb(0xCF9, 0x06);
        ret = 0;
        break;
    case SYS_UNAME: {
        const char *u = "FluxOS x86_64 0.3";
        ret = copy_to_user(a1, u, strlen(u) + 1) ? 0 : -1;
        break;
    }
    case SYS_FB_INFO:
        if (g_fb_info_hook) {
            uint64_t tmp[4];
            if (g_fb_info_hook(tmp) == 0 && copy_to_user(a1, tmp, sizeof(tmp))) {
                ret = 0;
            }
        }
        break;
    case SYS_AUDIO_NOTE:
        if (g_audio_note_hook) {
            g_audio_note_hook((uint32_t)a1, (uint32_t)a2, (uint8_t)a3, (uint8_t)a4);
            ret = 0;
        }
        break;
    case SYS_AUDIO_BEEP:
        if (g_audio_note_hook) {
            g_audio_note_hook((uint32_t)a1, (uint32_t)a2, 1 /* square */, 80);
            ret = 0;
        }
        break;
    case SYS_GUI_PRINT:
        ret = sys_write(1, a1, (size_t)a2);
        break;
    case SYS_MKDIR: {
        char path[VFS_MAX_PATH];
        if (copy_user_str(path, a1, sizeof(path)) && selinux_check_fs(cur, path, true, false) == 0) {
            ret = vfs_mkdir(path);
        }
        break;
    }
    case SYS_UNLINK: {
        char path[VFS_MAX_PATH];
        if (copy_user_str(path, a1, sizeof(path)) && selinux_check_fs(cur, path, true, false) == 0) {
            ret = vfs_unlink(path);
        }
        break;
    }
    case SYS_READDIR: {
        char path[VFS_MAX_PATH];
        size_t max_e = MIN((size_t)a3, 32U);
        if (a2 && max_e > 0 && copy_user_str(path, a1, sizeof(path))) {
            vfs_dirent_t ents[32];
            int cnt = vfs_readdir(path, ents, max_e);
            if (cnt >= 0) {
                sys_dirent_t out[32];
                for (int i = 0; i < cnt; i++) {
                    memcpy(out[i].name, ents[i].name, sizeof(out[i].name));
                    out[i].ino  = ents[i].ino;
                    out[i].type = (uint32_t)ents[i].type;
                    out[i].size = ents[i].size;
                }
                if (cnt == 0 || copy_to_user(a2, out, (size_t)cnt * sizeof(sys_dirent_t))) {
                    ret = cnt;
                }
            }
        }
        break;
    }
    case SYS_STAT: {
        char path[VFS_MAX_PATH];
        if (a2 && copy_user_str(path, a1, sizeof(path))) {
            vfs_inode_t *vn = vfs_lookup(path);
            if (vn) {
                sys_stat_t st = { .ino = vn->ino, .type = (uint32_t)vn->type, .size = vn->size };
                vfs_inode_put(vn);
                ret = copy_to_user(a2, &st, sizeof(st)) ? 0 : -1;
            }
        }
        break;
    }
    case SYS_SOCKET:
        if (cur && (a1 == AF_INET || a1 == 0)) {
            int fd = alloc_fd(cur, 0);
            if (fd >= 0) {
                int sid = sock_create((int)a2);
                if (sid >= 0) {
                    cur->fds[fd].type    = FD_SOCKET;
                    cur->fds[fd].sock_id = sid;
                    ret = fd;
                }
            }
        }
        break;
    case SYS_BIND: {
        int sid = get_sock_id(cur, (int)a1);
        if (sid >= 0 && selinux_check_net(cur, (uint16_t)a3, true) == 0) {
            ret = sock_bind(sid, (uint32_t)a2, (uint16_t)a3);
        }
        break;
    }
    case SYS_LISTEN: {
        int sid = get_sock_id(cur, (int)a1);
        if (sid >= 0) ret = sock_listen(sid);
        break;
    }
    case SYS_ACCEPT: {
        int sid = get_sock_id(cur, (int)a1);
        if (sid >= 0) {
            uint32_t rip = 0;
            uint16_t rport = 0;
            int cfd = alloc_fd(cur, 0);
            if (cfd >= 0) {
                int cid = sock_accept(sid, &rip, &rport, (uint32_t)a4);
                if (cid >= 0) {
                    cur->fds[cfd].type    = FD_SOCKET;
                    cur->fds[cfd].sock_id = cid;
                    if (a2) copy_to_user(a2, &rip, sizeof(rip));
                    if (a3) copy_to_user(a3, &rport, sizeof(rport));
                    ret = cfd;
                }
            }
        }
        break;
    }
    case SYS_CONNECT: {
        int sid = get_sock_id(cur, (int)a1);
        if (sid >= 0 && selinux_check_net(cur, (uint16_t)a3, false) == 0) {
            ret = sock_connect(sid, (uint32_t)a2, (uint16_t)a3);
        }
        break;
    }
    case SYS_SENDTO: {
        int sid = get_sock_id(cur, (int)a1);
        if (sid >= 0 && a2 && a3 <= 1472 && selinux_check_net(cur, (uint16_t)a5, false) == 0) {
            char kbuf[1472];
            if (copy_from_user(kbuf, a2, (size_t)a3)) {
                ret = sock_sendto(sid, kbuf, (size_t)a3, (uint32_t)a4, (uint16_t)a5);
            }
        }
        break;
    }
    case SYS_RECVFROM: {
        int sid = get_sock_id(cur, (int)a1);
        size_t want = MIN((size_t)a3, 4096U);
        if (sid >= 0 && a2 && want > 0) {
            char *kbuf = (char *)kmalloc(want);
            if (kbuf) {
                uint32_t rip = 0;
                uint16_t rport = 0;
                int64_t r = sock_recvfrom(sid, kbuf, want, &rip, &rport, (uint32_t)a6);
                if (r > 0 && !copy_to_user(a2, kbuf, (size_t)r)) r = -1;
                if (r >= 0) {
                    if (a4) copy_to_user(a4, &rip, sizeof(rip));
                    if (a5) copy_to_user(a5, &rport, sizeof(rport));
                }
                kfree(kbuf);
                ret = r;
            }
        }
        break;
    }
    case SYS_PING: {
        uint32_t rtt = 0;
        uint32_t tm = a3 ? (uint32_t)a3 : 1000U;
        if (icmp_ping((uint32_t)a1, (uint16_t)a2, tm, &rtt)) {
            if (a4) copy_to_user(a4, &rtt, sizeof(rtt));
            ret = 0;
        }
        break;
    }
    case SYS_NETINFO: {
        if (a1) {
            net_info_t ni;
            net_get_info(&ni);
            sys_netinfo_t out = {
                .reserved   = 0,
                .ip         = ni.ip,
                .mask       = ni.mask,
                .gw         = ni.gw,
                .rx_packets = ni.rx_packets,
                .tx_packets = ni.tx_packets
            };
            memcpy(out.mac, ni.mac, 6);
            ret = copy_to_user(a1, &out, sizeof(out)) ? 0 : -1;
        }
        break;
    }
    case SYS_WL_CREATE: {
        char title[32];
        strcpy(title, "wl_client");
        if (a1) copy_user_str(title, a1, sizeof(title));
        uint64_t uva = 0;
        int id = wl_surface_create_for_task(cur, title, (int)a2, (int)a3, &uva);
        if (id > 0 && a4 && copy_to_user(a4, &uva, sizeof(uva))) {
            ret = id;
        }
        break;
    }
    case SYS_WL_COMMIT:
        ret = wl_surface_commit((int)a1);
        break;
    case SYS_SEC_INFO: {
        if (a1) {
            flux_sec_info_t info;
            security_collect_info(cur, &info);
            ret = copy_to_user(a1, &info, sizeof(info)) ? 0 : -1;
        }
        break;
    }
    case SYS_SEC_CTL: {
        switch ((uint32_t)a1) {
        case SEC_OP_SET_ENFORCING:
            selinux_set_enforcing(a2 != 0);
            ret = 0;
            break;
        case SEC_OP_TEST_TAMPER:
            ret = secboot_selftest_tamper() ? 0 : -1;
            break;
        case SEC_OP_LUKS_RW_TEST:
            ret = luks2_selftest_rw() ? 0 : -1;
            break;
        case SEC_OP_FW_BLOCK_PORT:
            ret = fw_add_rule((uint8_t)a2, (uint16_t)a3, false) ? 0 : -1;
            break;
        case SEC_OP_FW_ALLOW_PORT:
            ret = fw_add_rule((uint8_t)a2, (uint16_t)a3, true) ? 0 : -1;
            break;
        case SEC_OP_FLATPAK_CONFINE:
            selinux_set_task_ctx(cur, "flatpak_u:sandbox_r:flatpak_t:s0", (uint32_t)a2);
            ret = 0;
            break;
        case SEC_OP_SET_CTX: {
            char kctx[40];
            if (a2 && copy_user_str(kctx, a2, sizeof(kctx))) {
                selinux_set_task_ctx(cur, kctx, (uint32_t)a3);
                ret = 0;
            }
            break;
        }
        default:
            ret = -1;
            break;
        }
        break;
    }
    default:
        ret = -1;
        break;
    }

    regs->rax = (uint64_t)ret;
    cli();
    return regs;
}
