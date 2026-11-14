#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "idt.h"
#include "cpu.h"
#include "spinlock.h"
#include "serial.h"
#include "string.h"

static vm_space_t  kernel_space;
static vm_space_t *active_space = NULL;
static uint64_t    mmio_next_va = 0xFFFFFFFFA0000000ULL;
static spinlock_t  mmio_lock = SPINLOCK_INIT;

static inline uint64_t *pt_virt(uint64_t phys) {
    return (uint64_t *)phys_to_virt(phys & PTE_ADDR_MASK);
}

static uint64_t *vmm_walk(vm_space_t *space, uint64_t virt, bool create, uint64_t walk_flags) {
    uint64_t idx[4] = {
        (virt >> 39) & 0x1FF, /* PML4 */
        (virt >> 30) & 0x1FF, /* PDPT */
        (virt >> 21) & 0x1FF, /* PD */
        (virt >> 12) & 0x1FF  /* PT */
    };

    uint64_t *table = pt_virt(space->pml4_phys);
    for (int level = 0; level < 3; level++) {
        uint64_t entry = table[idx[level]];
        if (!(entry & PTE_PRESENT)) {
            if (!create) return NULL;
            uint64_t next_phys = pmm_alloc_page();
            if (!next_phys) return NULL;
            entry = next_phys | PTE_PRESENT | PTE_WRITE | (walk_flags & PTE_USER);
            table[idx[level]] = entry;
        } else {
            if ((walk_flags & PTE_USER) && !(entry & PTE_USER)) {
                table[idx[level]] |= PTE_USER;
            }
            if (entry & PTE_HUGE) {
                return NULL;
            }
        }
        table = pt_virt(entry);
    }
    return &table[idx[3]];
}

static cpu_regs_t *page_fault_isr(cpu_regs_t *regs) {
    uint64_t cr2 = read_cr2();
    bool from_user = (regs->cs & 3) == 3;
    if (vmm_handle_page_fault(cr2, regs->err_code, from_user)) {
        return regs;
    }

    kpanic("Unhandled #PF (%s) @ CR2=0x%lx ERR=0x%lx RIP=0x%lx CS=0x%lx",
           from_user ? "Ring3" : "Ring0", cr2, regs->err_code, regs->rip, regs->cs);
}

void vmm_init(void) {
    memset(&kernel_space, 0, sizeof(kernel_space));
    kernel_space.pml4_phys = read_cr3() & PTE_ADDR_MASK;
    active_space = &kernel_space;

    /* Enable NXE (No-Execute) if supported in EFER */
    uint64_t efer = rdmsr(0xC0000080U);
    wrmsr(0xC0000080U, efer | (1ULL << 11));

    irq_register(14, page_fault_isr);
    kprintf("[VMM] 4-level paging online (CR3=0x%lx), #PF COW & lazy handler active\n",
            kernel_space.pml4_phys);
}

vm_space_t *vmm_kernel_space(void) {
    return &kernel_space;
}

vm_space_t *vmm_current_space(void) {
    return active_space ? active_space : &kernel_space;
}

void vmm_switch_space(vm_space_t *space) {
    if (!space) space = &kernel_space;
    if (active_space == space) return;
    active_space = space;
    write_cr3(space->pml4_phys);
}

bool vmm_map_page(vm_space_t *space, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!space) space = &kernel_space;
    uint64_t va = ALIGN_DOWN(virt, PAGE_SIZE);
    uint64_t pa = ALIGN_DOWN(phys, PAGE_SIZE);
    uint64_t *pte = vmm_walk(space, va, true, flags);
    if (!pte) return false;

    *pte = pa | (flags & ~PTE_ADDR_MASK) | PTE_PRESENT;
    if (space == active_space || va >= KERNEL_VMA_BASE) {
        invlpg(va);
    }
    return true;
}

void *vmm_map_mmio(uint64_t phys_addr, size_t size) {
    if (size == 0) return NULL;
    uint64_t phys_base = ALIGN_DOWN(phys_addr, PAGE_SIZE);
    uint64_t offset    = phys_addr - phys_base;
    size_t   pages     = ALIGN_UP(offset + size, PAGE_SIZE) / PAGE_SIZE;

    spin_lock_irqsave(&mmio_lock);
    uint64_t virt_base = mmio_next_va;
    mmio_next_va += pages * PAGE_SIZE;
    spin_unlock_irqrestore(&mmio_lock);

    for (size_t i = 0; i < pages; i++) {
        if (!vmm_map_page(&kernel_space, virt_base + i * PAGE_SIZE,
                          phys_base + i * PAGE_SIZE,
                          PTE_PRESENT | PTE_WRITE | PTE_CD | PTE_WT | PTE_NX)) {
            return NULL;
        }
    }
    return (void *)(uintptr_t)(virt_base + offset);
}

bool vmm_unmap_page(vm_space_t *space, uint64_t virt, bool free_phys) {
    if (!space) space = &kernel_space;
    uint64_t va = ALIGN_DOWN(virt, PAGE_SIZE);
    uint64_t *pte = vmm_walk(space, va, false, 0);
    if (!pte || !(*pte & PTE_PRESENT)) return false;

    uint64_t phys = *pte & PTE_ADDR_MASK;
    *pte = 0;
    if (space == active_space) {
        invlpg(va);
    }
    if (free_phys) {
        pmm_free_page(phys);
    }
    return true;
}

bool vmm_get_pte(vm_space_t *space, uint64_t virt, uint64_t *out_pte) {
    if (!space) space = &kernel_space;
    uint64_t *pte = vmm_walk(space, ALIGN_DOWN(virt, PAGE_SIZE), false, 0);
    if (!pte || !(*pte & PTE_PRESENT)) return false;
    if (out_pte) *out_pte = *pte;
    return true;
}

uint64_t vmm_virt_to_phys(vm_space_t *space, uint64_t virt) {
    uint64_t pte = 0;
    if (!vmm_get_pte(space, virt, &pte)) return 0;
    return (pte & PTE_ADDR_MASK) | (virt & (PAGE_SIZE - 1));
}

vm_space_t *vmm_create_space(void) {
    vm_space_t *sp = (vm_space_t *)kcalloc(1, sizeof(vm_space_t));
    if (!sp) return NULL;

    sp->pml4_phys = pmm_alloc_page();
    if (!sp->pml4_phys) {
        kfree(sp);
        return NULL;
    }

    uint64_t *dst_pml4 = pt_virt(sp->pml4_phys);
    uint64_t *src_pml4 = pt_virt(kernel_space.pml4_phys);
    /* Share kernel upper half (entries 256..511), keep user lower half empty */
    for (int i = 256; i < 512; i++) {
        dst_pml4[i] = src_pml4[i];
    }
    return sp;
}

static bool clone_pt(uint64_t src_pt_phys, uint64_t dst_pt_phys, uint64_t va_base, bool is_active) {
    uint64_t *src = pt_virt(src_pt_phys);
    uint64_t *dst = pt_virt(dst_pt_phys);
    for (uint64_t i = 0; i < 512; i++) {
        uint64_t pte = src[i];
        if (!(pte & PTE_PRESENT)) continue;
        uint64_t phys = pte & PTE_ADDR_MASK;
        if (pte & PTE_WRITE) {
            pte = (pte & ~PTE_WRITE) | PTE_COW;
            src[i] = pte;
            if (is_active) invlpg(va_base + (i << 12));
        }
        dst[i] = pte;
        pmm_ref_inc(phys);
    }
    return true;
}

static bool clone_pd(uint64_t src_pd_phys, uint64_t dst_pd_phys, uint64_t va_base, bool is_active) {
    uint64_t *src = pt_virt(src_pd_phys);
    uint64_t *dst = pt_virt(dst_pd_phys);
    for (uint64_t i = 0; i < 512; i++) {
        uint64_t pde = src[i];
        if (!(pde & PTE_PRESENT)) continue;
        uint64_t new_pt = pmm_alloc_page();
        if (!new_pt) return false;
        dst[i] = new_pt | (pde & ~PTE_ADDR_MASK);
        if (!clone_pt(pde & PTE_ADDR_MASK, new_pt, va_base + (i << 21), is_active)) return false;
    }
    return true;
}

static bool clone_pdpt(uint64_t src_pdpt_phys, uint64_t dst_pdpt_phys, uint64_t va_base, bool is_active) {
    uint64_t *src = pt_virt(src_pdpt_phys);
    uint64_t *dst = pt_virt(dst_pdpt_phys);
    for (uint64_t i = 0; i < 512; i++) {
        uint64_t pdpte = src[i];
        if (!(pdpte & PTE_PRESENT)) continue;
        uint64_t new_pd = pmm_alloc_page();
        if (!new_pd) return false;
        dst[i] = new_pd | (pdpte & ~PTE_ADDR_MASK);
        if (!clone_pd(pdpte & PTE_ADDR_MASK, new_pd, va_base + (i << 30), is_active)) return false;
    }
    return true;
}

vm_space_t *vmm_clone_cow(vm_space_t *src) {
    if (!src) return NULL;
    vm_space_t *dst = vmm_create_space();
    if (!dst) return NULL;

    uint64_t rf = irq_save_disable();
    bool is_active = (src == active_space);
    uint64_t *src_pml4 = pt_virt(src->pml4_phys);
    uint64_t *dst_pml4 = pt_virt(dst->pml4_phys);

    for (uint64_t i = 0; i < 256; i++) {
        uint64_t pml4e = src_pml4[i];
        if (!(pml4e & PTE_PRESENT)) continue;
        uint64_t new_pdpt = pmm_alloc_page();
        if (!new_pdpt) {
            irq_restore(rf);
            vmm_destroy_space(dst);
            return NULL;
        }
        dst_pml4[i] = new_pdpt | (pml4e & ~PTE_ADDR_MASK);
        if (!clone_pdpt(pml4e & PTE_ADDR_MASK, new_pdpt, i << 39, is_active)) {
            irq_restore(rf);
            vmm_destroy_space(dst);
            return NULL;
        }
    }

    /* Clone VMA list */
    vma_region_t **tail = &dst->vmas;
    for (vma_region_t *v = src->vmas; v; v = v->next) {
        vma_region_t *nv = (vma_region_t *)kmalloc(sizeof(vma_region_t));
        if (!nv) {
            irq_restore(rf);
            vmm_destroy_space(dst);
            return NULL;
        }
        *nv = *v;
        nv->next = NULL;
        *tail = nv;
        tail = &nv->next;
    }
    dst->brk_start   = src->brk_start;
    dst->brk_curr    = src->brk_curr;
    dst->pie_base    = src->pie_base;
    dst->relro_pages = src->relro_pages;
    irq_restore(rf);
    return dst;
}

static void free_pt_tree(uint64_t table_phys, int level) {
    uint64_t *tbl = pt_virt(table_phys);
    int max_idx = (level == 4) ? 256 : 512;
    for (int i = 0; i < max_idx; i++) {
        uint64_t e = tbl[i];
        if (!(e & PTE_PRESENT)) continue;
        if (level == 1) {
            pmm_free_page(e & PTE_ADDR_MASK);
        } else {
            free_pt_tree(e & PTE_ADDR_MASK, level - 1);
        }
        tbl[i] = 0;
    }
    pmm_free_page(table_phys);
}

void vmm_destroy_space(vm_space_t *space) {
    if (!space || space == &kernel_space) return;
    if (active_space == space) {
        vmm_switch_space(&kernel_space);
    }

    vma_region_t *v = space->vmas;
    while (v) {
        vma_region_t *nx = v->next;
        kfree(v);
        v = nx;
    }
    if (space->pml4_phys) {
        free_pt_tree(space->pml4_phys, 4);
    }
    kfree(space);
}

bool vmm_add_vma(vm_space_t *space, uint64_t start, uint64_t end, uint32_t flags, vma_type_t type) {
    if (!space || start >= end) return false;
    vma_region_t *v = (vma_region_t *)kmalloc(sizeof(vma_region_t));
    if (!v) return false;
    v->start = ALIGN_DOWN(start, PAGE_SIZE);
    v->end   = ALIGN_UP(end, PAGE_SIZE);
    v->flags = flags;
    v->type  = type;
    v->next  = space->vmas;
    space->vmas = v;
    return true;
}

bool vmm_remove_vma(vm_space_t *space, uint64_t start, uint64_t end) {
    if (!space || start >= end) return false;
    uint64_t a = ALIGN_DOWN(start, PAGE_SIZE);
    uint64_t b = ALIGN_UP(end, PAGE_SIZE);
    vma_region_t **pp = &space->vmas;
    bool removed = false;
    while (*pp) {
        vma_region_t *v = *pp;
        if (v->start >= a && v->end <= b) {
            for (uint64_t va = v->start; va < v->end; va += PAGE_SIZE) {
                vmm_unmap_page(space, va, true);
            }
            *pp = v->next;
            kfree(v);
            removed = true;
            continue;
        }
        pp = &v->next;
    }
    return removed;
}

vma_region_t *vmm_find_vma(vm_space_t *space, uint64_t addr) {
    if (!space) return NULL;
    for (vma_region_t *v = space->vmas; v; v = v->next) {
        if (addr >= v->start && addr < v->end) return v;
    }
    return NULL;
}

bool vmm_user_range_valid(vm_space_t *space, uint64_t addr, size_t len, bool write) {
    if (!space || len == 0) return false;
    if (addr > USER_SPACE_MAX || len > USER_SPACE_MAX - addr) return false;
    uint64_t cur = ALIGN_DOWN(addr, PAGE_SIZE);
    uint64_t end = ALIGN_UP(addr + len, PAGE_SIZE);
    while (cur < end) {
        vma_region_t *v = vmm_find_vma(space, cur);
        if (!v || !(v->flags & VMA_USER)) return false;
        if (write && !(v->flags & VMA_WRITE)) return false;
        cur = v->end;
    }
    return true;
}

bool vmm_handle_page_fault(uint64_t fault_addr, uint64_t err_code, bool from_user) {
    bool present = (err_code & 0x1) != 0;
    bool write   = (err_code & 0x2) != 0;
    bool exec    = (err_code & 0x10) != 0;

    if (from_user && fault_addr >= KERNEL_VMA_BASE) {
        return false;
    }

    vm_space_t *sp = vmm_current_space();
    uint64_t page_va = ALIGN_DOWN(fault_addr, PAGE_SIZE);

    /* Case 1: Copy-on-Write on present writable-by-COW page */
    if (present && write) {
        uint64_t *pte_ptr = vmm_walk(sp, page_va, false, 0);
        if (pte_ptr && (*pte_ptr & PTE_PRESENT) && (*pte_ptr & PTE_COW)) {
            uint64_t old_phys = *pte_ptr & PTE_ADDR_MASK;
            uint64_t new_flags = (*pte_ptr & ~(PTE_ADDR_MASK | PTE_COW)) | PTE_WRITE;

            if (pmm_ref_get(old_phys) <= 1) {
                *pte_ptr = old_phys | new_flags;
            } else {
                uint64_t new_phys = pmm_alloc_page();
                if (!new_phys) return false;
                memcpy(phys_to_virt(new_phys), phys_to_virt(old_phys), PAGE_SIZE);
                *pte_ptr = new_phys | new_flags;
                pmm_free_page(old_phys);
            }
            invlpg(page_va);
            sp->cow_faults++;
            return true;
        }
        return false;
    }

    /* Case 2: Lazy demand-zero allocation inside valid VMA */
    if (!present) {
        vma_region_t *vma = vmm_find_vma(sp, fault_addr);
        if (!vma) return false;
        if (from_user && !(vma->flags & VMA_USER)) return false;
        if (write && !(vma->flags & VMA_WRITE)) return false;
        if (exec && !(vma->flags & VMA_EXEC)) return false;

        uint64_t phys = pmm_alloc_page();
        if (!phys) return false;

        uint64_t pte_flags = PTE_PRESENT;
        if (vma->flags & VMA_USER)  pte_flags |= PTE_USER;
        if (vma->flags & VMA_WRITE) pte_flags |= PTE_WRITE;
        if (!(vma->flags & VMA_EXEC)) pte_flags |= PTE_NX;

        if (!vmm_map_page(sp, page_va, phys, pte_flags)) {
            pmm_free_page(phys);
            return false;
        }
        sp->lazy_faults++;
        return true;
    }

    return false;
}
