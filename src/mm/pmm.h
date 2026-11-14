#ifndef FLUX_PMM_H
#define FLUX_PMM_H

#include "types.h"

struct limine_memmap_response;

typedef struct {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t total_bytes;
    uint64_t free_bytes;
} pmm_stats_t;

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);
uint64_t pmm_hhdm(void);

static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(uintptr_t)(phys + pmm_hhdm());
}

static inline uint64_t virt_to_phys_hhdm(const void *virt) {
    return (uint64_t)(uintptr_t)virt - pmm_hhdm();
}

uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_pages(size_t count);
void     pmm_free_page(uint64_t phys);
void     pmm_free_pages(uint64_t phys, size_t count);

void     pmm_ref_inc(uint64_t phys);
uint16_t pmm_ref_get(uint64_t phys);

void pmm_get_stats(pmm_stats_t *out);

#endif
