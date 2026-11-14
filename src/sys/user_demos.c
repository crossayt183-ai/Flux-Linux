#include "syscall.h"
#include "security.h"
#include "elf.h"
#include "vfs.h"
#include "task.h"
#include "pmm.h"
#include "heap.h"
#include "vmm.h"
#include "gdt.h"
#include "serial.h"
#include "string.h"

#define USER_CODE_BASE  0x0000000000400000ULL
#define USER_BRK_BASE   0x0000000000800000ULL
#define USER_STACK_TOP  0x0000007000000000ULL
#define USER_STACK_SIZE (64ULL * 1024ULL)

extern const uint8_t user_selftest_start[];
extern const uint8_t user_selftest_end[];
extern const uint8_t user_worker_start[];
extern const uint8_t user_worker_end[];
extern const uint8_t user_beeper_start[];
extern const uint8_t user_beeper_end[];

typedef struct {
    const char    *path;
    const char    *proc_name;
    const uint8_t *start;
    const uint8_t *end;
} user_prog_entry_t;

static const user_prog_entry_t *find_builtin_program(const char *path) {
    static user_prog_entry_t table[3];
    table[0] = (user_prog_entry_t){ "/bin/selftest", "u-selftest", user_selftest_start, user_selftest_end };
    table[1] = (user_prog_entry_t){ "/bin/worker",   "u-worker",   user_worker_start,   user_worker_end };
    table[2] = (user_prog_entry_t){ "/bin/beeper",   "u-beeper",   user_beeper_start,   user_beeper_end };

    for (size_t i = 0; i < ARRAY_LEN(table); i++) {
        if (strcmp(path, table[i].path) == 0) {
            return &table[i];
        }
    }
    return NULL;
}

static vm_space_t *build_raw_user_vm(const user_prog_entry_t *prog) {
    size_t code_size = (size_t)(prog->end - prog->start);
    if (code_size == 0) return NULL;

    vm_space_t *vm = vmm_create_space();
    if (!vm) return NULL;

    size_t code_pages = ALIGN_UP(code_size, PAGE_SIZE) / PAGE_SIZE;
    for (size_t p = 0; p < code_pages; p++) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) {
            vmm_destroy_space(vm);
            return NULL;
        }
        size_t off = p * PAGE_SIZE;
        size_t chunk = MIN(code_size - off, PAGE_SIZE);
        memcpy(phys_to_virt(phys), prog->start + off, chunk);
        if (!vmm_map_page(vm, USER_CODE_BASE + off, phys, PTE_USER)) {
            pmm_free_page(phys);
            vmm_destroy_space(vm);
            return NULL;
        }
    }

    if (!vmm_add_vma(vm, USER_CODE_BASE, USER_CODE_BASE + code_pages * PAGE_SIZE,
                     VMA_READ | VMA_EXEC | VMA_USER, VMA_CODE)) {
        vmm_destroy_space(vm);
        return NULL;
    }

    uint64_t stack_bot = USER_STACK_TOP - USER_STACK_SIZE;
    if (!vmm_add_vma(vm, stack_bot, USER_STACK_TOP, VMA_READ | VMA_WRITE | VMA_USER, VMA_STACK)) {
        vmm_destroy_space(vm);
        return NULL;
    }

    vm->brk_start = USER_BRK_BASE;
    vm->brk_curr  = USER_BRK_BASE;
    return vm;
}

static const char *base_proc_name(const char *path) {
    const char *last = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' && p[1]) last = p + 1;
    }
    return last;
}

static bool load_program_image(const char *path, int argc, const char *const *argv,
                               vm_space_t **out_vm, uint64_t *out_entry, uint64_t *out_rsp,
                               char *out_name, size_t name_cap) {
    const char *def_argv[1] = { path };
    if (argc <= 0 || !argv) {
        argc = 1;
        argv = def_argv;
    }

    /* 1. Try external ELF64 binary on VFS/ext2 disk first */
    if (vfs_is_mounted()) {
        uint8_t *elf_buf = NULL;
        size_t elf_sz = 0;
        if (vfs_read_all(path, &elf_buf, &elf_sz)) {
            if (secboot_verify_elf(path, elf_buf, elf_sz) < 0) {
                kfree(elf_buf);
                return false;
            }
            bool ok = elf_is_valid(elf_buf, elf_sz) &&
                      elf_load_into_vm(elf_buf, elf_sz, argc, argv, out_vm, out_entry, out_rsp);
            kfree(elf_buf);
            if (ok) {
                strncpy(out_name, base_proc_name(path), name_cap - 1);
                out_name[name_cap - 1] = '\0';
                return true;
            }
        }
    }

    /* 2. Fallback to built-in raw Ring 3 test binaries */
    const user_prog_entry_t *prog = find_builtin_program(path);
    if (!prog) return false;
    vm_space_t *vm = build_raw_user_vm(prog);
    if (!vm) return false;

    *out_vm    = vm;
    *out_entry = USER_CODE_BASE;
    *out_rsp   = USER_STACK_TOP - 16;
    strncpy(out_name, prog->proc_name, name_cap - 1);
    out_name[name_cap - 1] = '\0';
    return true;
}

bool user_exec_program_argv(const char *path, int argc, const char *const *argv, int priority, uint64_t *out_pid) {
    vm_space_t *vm = NULL;
    uint64_t entry = 0, rsp = 0;
    char pname[32];
    if (!load_program_image(path, argc, argv, &vm, &entry, &rsp, pname, sizeof(pname))) {
        return false;
    }

    task_t *t = task_create_user(pname, entry, rsp, vm, (uint8_t)priority);
    if (!t) {
        vmm_destroy_space(vm);
        return false;
    }
    if (out_pid) *out_pid = t->pid;
    return true;
}

bool user_exec_program(const char *path, int priority, uint64_t *out_pid) {
    const char *argv[2] = { path, NULL };
    return user_exec_program_argv(path, 1, argv, priority, out_pid);
}

int64_t user_replace_current_argv(const char *path, int argc, const char *const *argv, cpu_regs_t *regs) {
    task_t *cur = task_current();
    if (!cur) return -1;

    vm_space_t *new_vm = NULL;
    uint64_t entry = 0, rsp = 0;
    char pname[32];
    if (!load_program_image(path, argc, argv, &new_vm, &entry, &rsp, pname, sizeof(pname))) {
        return -1;
    }

    vm_space_t *old_vm = cur->vm;
    cur->vm = new_vm;
    strncpy(cur->name, pname, sizeof(cur->name) - 1);
    cur->name[sizeof(cur->name) - 1] = '\0';

    vmm_switch_space(new_vm);
    if (old_vm && old_vm != vmm_kernel_space()) {
        vmm_destroy_space(old_vm);
    }

    regs->rip    = entry;
    regs->cs     = GDT_USER_CODE | 3;
    regs->rflags = 0x202ULL;
    regs->rsp    = rsp;
    regs->ss     = GDT_USER_DATA | 3;
    regs->rax = regs->rbx = regs->rcx = regs->rdx = 0;
    regs->rsi = regs->rdi = regs->rbp = 0;
    regs->r8 = regs->r9 = regs->r10 = regs->r11 = 0;
    regs->r12 = regs->r13 = regs->r14 = regs->r15 = 0;
    return 0;
}

int64_t user_replace_current(const char *path, cpu_regs_t *regs) {
    const char *argv[2] = { path, NULL };
    return user_replace_current_argv(path, 1, argv, regs);
}
