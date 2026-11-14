#include "elf.h"
#include "pmm.h"
#include "vmm.h"
#include "timer.h"
#include "string.h"
#include "serial.h"

#define EI_NIDENT 16
#define ELF_MAGIC 0x464C457FU /* "\x7FELF" little-endian */
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EM_X86_64 62
#define PT_LOAD 1
#define PF_X 1U
#define PF_W 2U
#define PF_R 4U

#define USER_STACK_TOP  0x0000007000000000ULL
#define USER_STACK_SIZE (64ULL * 1024ULL)
#define USER_ELF_LIMIT  0x0000001000000000ULL

typedef struct PACKED {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr_t;

typedef struct PACKED {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

bool elf_is_valid(const uint8_t *buf, size_t len) {
    if (!buf || len < sizeof(elf64_ehdr_t)) return false;
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)buf;
    if (*(const uint32_t *)eh->e_ident != ELF_MAGIC) return false;
    if (eh->e_ident[4] != ELFCLASS64 || eh->e_ident[5] != ELFDATA2LSB) return false;
    if ((eh->e_type != ET_EXEC && eh->e_type != ET_DYN) || eh->e_machine != EM_X86_64) return false;
    if (eh->e_phnum == 0 || eh->e_phentsize < sizeof(elf64_phdr_t)) return false;
    if (eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > len) return false;
    return true;
}

static bool map_or_merge_vma(vm_space_t *vm, uint64_t start, uint64_t end, uint32_t vflags) {
    for (vma_region_t *v = vm->vmas; v; v = v->next) {
        if (start < v->end && end > v->start) {
            if (start < v->start) v->start = start;
            if (end > v->end)     v->end = end;
            v->flags |= vflags;
            return true;
        }
    }
    vma_type_t vt = (vflags & VMA_EXEC) ? VMA_CODE : VMA_DATA;
    return vmm_add_vma(vm, start, end, vflags, vt);
}

static bool setup_user_stack(vm_space_t *vm, int argc, const char *const *argv, uint64_t *out_rsp) {
    uint64_t stack_bot = USER_STACK_TOP - USER_STACK_SIZE;
    if (!vmm_add_vma(vm, stack_bot, USER_STACK_TOP, VMA_READ | VMA_WRITE | VMA_USER, VMA_STACK)) {
        return false;
    }

    uint64_t top_page_va = USER_STACK_TOP - PAGE_SIZE;
    uint64_t top_phys = pmm_alloc_page();
    if (!top_phys) return false;
    if (!vmm_map_page(vm, top_page_va, top_phys, PTE_PRESENT | PTE_USER | PTE_WRITE | PTE_NX)) {
        pmm_free_page(top_phys);
        return false;
    }

    uint8_t *kpage = (uint8_t *)phys_to_virt(top_phys);
    memset(kpage, 0, PAGE_SIZE);

    if (argc < 0) argc = 0;
    if (argc > 16) argc = 16;

    uint64_t u_ptrs[16];
    uint64_t sp = USER_STACK_TOP;

    /* Copy argument strings down from top of stack page */
    for (int i = argc - 1; i >= 0; i--) {
        const char *s = (argv && argv[i]) ? argv[i] : "";
        size_t slen = strlen(s) + 1;
        if (slen > 128) slen = 128;
        if (sp - slen < top_page_va + 256) break;
        sp -= slen;
        memcpy(kpage + (sp - top_page_va), s, slen - 1);
        kpage[(sp - top_page_va) + slen - 1] = '\0';
        u_ptrs[i] = sp;
    }

    /* Push System V AMD64 initial frame: argc, argv[0..argc-1], NULL, envp NULL */
    sp = ALIGN_DOWN(sp, 8);
    uint64_t qwords = 1ULL + (uint64_t)argc + 1ULL + 1ULL;
    if (((sp - qwords * 8ULL) & 0xFULL) != 0) {
        sp -= 8; /* Keep RSP 16-byte aligned at _start */
    }
    uint64_t frame_rsp = sp - qwords * 8ULL;
    if (frame_rsp < top_page_va) return false;

    uint64_t *tbl = (uint64_t *)(kpage + (frame_rsp - top_page_va));
    tbl[0] = (uint64_t)argc;
    for (int i = 0; i < argc; i++) tbl[1 + i] = u_ptrs[i];
    tbl[1 + argc] = 0;
    tbl[2 + argc] = 0;

    *out_rsp = frame_rsp;
    return true;
}

static bool vm_read_bytes(vm_space_t *vm, uint64_t uva, void *out, size_t len) {
    uint8_t *dst = (uint8_t *)out;
    size_t done = 0;
    while (done < len) {
        uint64_t cur = uva + done;
        uint64_t pte = 0;
        if (!vmm_get_pte(vm, ALIGN_DOWN(cur, PAGE_SIZE), &pte) || !(pte & PTE_PRESENT)) {
            return false;
        }
        size_t page_off = (size_t)(cur & (PAGE_SIZE - 1));
        size_t chunk = MIN(len - done, PAGE_SIZE - page_off);
        memcpy(dst + done, (const uint8_t *)phys_to_virt(pte & PTE_ADDR_MASK) + page_off, chunk);
        done += chunk;
    }
    return true;
}

static bool vm_write_u64(vm_space_t *vm, uint64_t uva, uint64_t val) {
    const uint8_t *src = (const uint8_t *)&val;
    for (size_t i = 0; i < sizeof(uint64_t); i++) {
        uint64_t cur = uva + i;
        uint64_t pte = 0;
        if (!vmm_get_pte(vm, ALIGN_DOWN(cur, PAGE_SIZE), &pte) || !(pte & PTE_PRESENT)) {
            return false;
        }
        size_t page_off = (size_t)(cur & (PAGE_SIZE - 1));
        ((uint8_t *)phys_to_virt(pte & PTE_ADDR_MASK))[page_off] = src[i];
    }
    return true;
}

static bool apply_pie_relocs(vm_space_t *vm, const uint8_t *buf, size_t len,
                             const elf64_ehdr_t *eh, uint64_t load_bias) {
    uint64_t rela_va = 0, rela_sz = 0, rela_ent = sizeof(elf64_rela_t);
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const elf64_phdr_t *ph = (const elf64_phdr_t *)(buf + eh->e_phoff + (size_t)i * eh->e_phentsize);
        if (ph->p_type != PT_DYNAMIC || ph->p_filesz == 0) continue;
        if (ph->p_offset + ph->p_filesz > len) return false;
        for (uint64_t off = 0; off + sizeof(elf64_dyn_t) <= ph->p_filesz; off += sizeof(elf64_dyn_t)) {
            const elf64_dyn_t *dyn = (const elf64_dyn_t *)(buf + ph->p_offset + off);
            if (dyn->d_tag == DT_NULL) break;
            if (dyn->d_tag == DT_RELA)    rela_va  = dyn->d_val;
            if (dyn->d_tag == DT_RELASZ)  rela_sz  = dyn->d_val;
            if (dyn->d_tag == DT_RELAENT && dyn->d_val >= sizeof(elf64_rela_t)) rela_ent = dyn->d_val;
        }
    }
    if (rela_sz > 0) {
        for (uint64_t off = 0; off + sizeof(elf64_rela_t) <= rela_sz; off += rela_ent) {
            elf64_rela_t r;
            if (!vm_read_bytes(vm, load_bias + rela_va + off, &r, sizeof(r))) return false;
            if ((uint32_t)(r.r_info & 0xFFFFFFFFULL) == R_X86_64_RELATIVE) {
                if (!vm_write_u64(vm, load_bias + r.r_offset, load_bias + (uint64_t)r.r_addend)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static void apply_gnu_relro(vm_space_t *vm, const uint8_t *buf, const elf64_ehdr_t *eh, uint64_t load_bias) {
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const elf64_phdr_t *ph = (const elf64_phdr_t *)(buf + eh->e_phoff + (size_t)i * eh->e_phentsize);
        if (ph->p_type != PT_GNU_RELRO || ph->p_memsz == 0) continue;
        uint64_t r_start = ALIGN_DOWN(load_bias + ph->p_vaddr, PAGE_SIZE);
        uint64_t r_end   = ALIGN_UP(load_bias + ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
        for (uint64_t va = r_start; va < r_end; va += PAGE_SIZE) {
            uint64_t pte = 0;
            if (vmm_get_pte(vm, va, &pte) && (pte & PTE_PRESENT)) {
                uint64_t ro_flags = (pte & ~(PTE_ADDR_MASK | PTE_WRITE)) | PTE_PRESENT | PTE_USER;
                vmm_map_page(vm, va, pte & PTE_ADDR_MASK, ro_flags);
                vm->relro_pages++;
            }
        }
        for (vma_region_t *v = vm->vmas; v; v = v->next) {
            if (v->start >= r_start && v->end <= r_end) {
                v->flags &= ~VMA_WRITE;
            }
        }
    }
}

bool elf_load_into_vm(const uint8_t *buf, size_t len,
                      int argc, const char *const *argv,
                      vm_space_t **out_vm, uint64_t *out_entry, uint64_t *out_rsp) {
    if (!elf_is_valid(buf, len) || !out_vm || !out_entry || !out_rsp) return false;
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)buf;

    vm_space_t *vm = vmm_create_space();
    if (!vm) return false;

    uint64_t load_bias = 0;
    if (eh->e_type == ET_DYN) {
        static uint64_t aslr_seq = 1;
        uint64_t slide = ((aslr_seq++ * 0x5bd1e995ULL) + timer_ticks()) & 0xFFULL;
        load_bias = 0x0000000040000000ULL + (slide << 12);
    }
    vm->pie_base    = load_bias;
    vm->relro_pages = 0;

    uint64_t max_end = 0x400000ULL;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const elf64_phdr_t *ph = (const elf64_phdr_t *)(buf + eh->e_phoff + (size_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
        if (ph->p_filesz > ph->p_memsz || ph->p_offset + ph->p_filesz > len) {
            vmm_destroy_space(vm);
            return false;
        }
        uint64_t seg_vaddr = ph->p_vaddr + load_bias;
        if (seg_vaddr < 0x10000ULL || seg_vaddr + ph->p_memsz > USER_ELF_LIMIT) {
            vmm_destroy_space(vm);
            return false;
        }

        uint64_t seg_start = ALIGN_DOWN(seg_vaddr, PAGE_SIZE);
        uint64_t seg_end   = ALIGN_UP(seg_vaddr + ph->p_memsz, PAGE_SIZE);
        if (seg_end > max_end) max_end = seg_end;

        uint32_t vflags = VMA_USER | VMA_READ;
        if (ph->p_flags & PF_W) vflags |= VMA_WRITE;
        if (ph->p_flags & PF_X) vflags |= VMA_EXEC;

        if (!map_or_merge_vma(vm, seg_start, seg_end, vflags)) {
            vmm_destroy_space(vm);
            return false;
        }

        uint64_t pte_flags = PTE_PRESENT | PTE_USER;
        if (ph->p_flags & PF_W)    pte_flags |= PTE_WRITE;
        if (!(ph->p_flags & PF_X)) pte_flags |= PTE_NX;

        for (uint64_t va = seg_start; va < seg_end; va += PAGE_SIZE) {
            uint64_t existing_pte = 0;
            uint64_t phys = 0;
            uint64_t final_flags = pte_flags;

            if (vmm_get_pte(vm, va, &existing_pte)) {
                phys = existing_pte & PTE_ADDR_MASK;
                /* Preserve W/X if adjacent segments share a boundary page */
                uint64_t merged = (existing_pte & ~PTE_ADDR_MASK) | pte_flags;
                if (((existing_pte & PTE_NX) == 0) || ((pte_flags & PTE_NX) == 0)) {
                    merged &= ~PTE_NX;
                }
                final_flags = merged;
            } else {
                phys = pmm_alloc_page();
                if (!phys) { vmm_destroy_space(vm); return false; }
                memset(phys_to_virt(phys), 0, PAGE_SIZE);
            }

            if (!vmm_map_page(vm, va, phys, final_flags)) {
                vmm_destroy_space(vm);
                return false;
            }

            uint64_t file_start = seg_vaddr;
            uint64_t file_end   = seg_vaddr + ph->p_filesz;
            uint64_t ov_start   = MAX(va, file_start);
            uint64_t ov_end     = MIN(va + PAGE_SIZE, file_end);
            if (ov_start < ov_end) {
                size_t copy_len = (size_t)(ov_end - ov_start);
                size_t file_off = (size_t)(ph->p_offset + (ov_start - seg_vaddr));
                size_t page_off = (size_t)(ov_start - va);
                memcpy((uint8_t *)phys_to_virt(phys) + page_off, buf + file_off, copy_len);
            }
        }
    }

    if (eh->e_type == ET_DYN && load_bias != 0) {
        if (!apply_pie_relocs(vm, buf, len, eh, load_bias)) {
            vmm_destroy_space(vm);
            return false;
        }
    }
    apply_gnu_relro(vm, buf, eh, load_bias);

    uint64_t entry_va = eh->e_entry + load_bias;
    if (entry_va < 0x10000ULL || entry_va >= USER_ELF_LIMIT) {
        vmm_destroy_space(vm);
        return false;
    }

    uint64_t init_rsp = 0;
    if (!setup_user_stack(vm, argc, argv, &init_rsp)) {
        vmm_destroy_space(vm);
        return false;
    }

    vm->brk_start = ALIGN_UP(max_end, PAGE_SIZE) + PAGE_SIZE;
    vm->brk_curr  = vm->brk_start;

    *out_vm    = vm;
    *out_entry = entry_va;
    *out_rsp   = init_rsp;
    return true;
}
