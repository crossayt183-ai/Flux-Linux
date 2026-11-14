#ifndef FLUX_VMM_H
#define FLUX_VMM_H

#include "types.h"

/* x86_64 Page Table Entry flags */
#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITE    (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_WT       (1ULL << 3)
#define PTE_CD       (1ULL << 4)
#define PTE_ACCESSED (1ULL << 5)
#define PTE_DIRTY    (1ULL << 6)
#define PTE_HUGE     (1ULL << 7)
#define PTE_GLOBAL   (1ULL << 8)
/* Custom OS bits [9..11] */
#define PTE_COW      (1ULL << 9)
#define PTE_LAZY     (1ULL << 10)
#define PTE_NX       (1ULL << 63)

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define USER_SPACE_MAX 0x00007FFFFFFFFFFFULL
#define KERNEL_VMA_BASE 0xFFFF800000000000ULL

typedef enum {
    VMA_NONE = 0,
    VMA_ANON,
    VMA_STACK,
    VMA_CODE,
    VMA_DATA,
    VMA_MMIO
} vma_type_t;

#define VMA_READ  (1U << 0)
#define VMA_WRITE (1U << 1)
#define VMA_EXEC  (1U << 2)
#define VMA_USER  (1U << 3)

typedef struct vma_region {
    uint64_t start;
    uint64_t end;
    uint32_t flags;
    vma_type_t type;
    struct vma_region *next;
} vma_region_t;

typedef struct {
    uint64_t      pml4_phys;
    vma_region_t *vmas;
    uint64_t      brk_start;
    uint64_t      brk_curr;
    uint64_t      cow_faults;
    uint64_t      lazy_faults;
    uint64_t      pie_base;
    uint32_t      relro_pages;
} vm_space_t;

void vmm_init(void);
vm_space_t *vmm_kernel_space(void);
vm_space_t *vmm_current_space(void);

vm_space_t *vmm_create_space(void);
vm_space_t *vmm_clone_cow(vm_space_t *src);
void        vmm_destroy_space(vm_space_t *space);
void        vmm_switch_space(vm_space_t *space);

bool     vmm_map_page(vm_space_t *space, uint64_t virt, uint64_t phys, uint64_t flags);
bool     vmm_unmap_page(vm_space_t *space, uint64_t virt, bool free_phys);
void    *vmm_map_mmio(uint64_t phys_addr, size_t size);
uint64_t vmm_virt_to_phys(vm_space_t *space, uint64_t virt);
bool     vmm_get_pte(vm_space_t *space, uint64_t virt, uint64_t *out_pte);

/* VMA registration for lazy demand paging & user memory protection */
bool vmm_add_vma(vm_space_t *space, uint64_t start, uint64_t end, uint32_t flags, vma_type_t type);
bool vmm_remove_vma(vm_space_t *space, uint64_t start, uint64_t end);
vma_region_t *vmm_find_vma(vm_space_t *space, uint64_t addr);
bool vmm_user_range_valid(vm_space_t *space, uint64_t addr, size_t len, bool write);

/* Invoked by #PF ISR (vector 14) */
bool vmm_handle_page_fault(uint64_t fault_addr, uint64_t err_code, bool from_user);

#endif
