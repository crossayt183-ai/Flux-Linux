#include "pmm.h"
#include "limine.h"
#include "serial.h"
#include "string.h"
#include "cpu.h"

static uint8_t  *bitmap = NULL;
static uint16_t *refcounts = NULL;
static uint64_t  highest_addr = 0;
static uint64_t  total_pages = 0;
static uint64_t  free_pages = 0;
static uint64_t  last_hint = 0;
static uint64_t  g_hhdm = 0;

static inline void bmp_set(uint64_t pfn) {
    bitmap[pfn >> 3] |= (uint8_t)(1U << (pfn & 7));
}

static inline void bmp_clear(uint64_t pfn) {
    bitmap[pfn >> 3] &= (uint8_t)~(1U << (pfn & 7));
}

static inline bool bmp_test(uint64_t pfn) {
    return (bitmap[pfn >> 3] & (1U << (pfn & 7))) != 0;
}

uint64_t pmm_hhdm(void) {
    return g_hhdm;
}

void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    g_hhdm = hhdm_offset;
    highest_addr = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE ||
            e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
            e->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) {
            uint64_t end = e->base + e->length;
            if (end > highest_addr) highest_addr = end;
        }
    }

    total_pages = ALIGN_UP(highest_addr, PAGE_SIZE) / PAGE_SIZE;
    uint64_t bmp_bytes = ALIGN_UP((total_pages + 7) / 8, PAGE_SIZE);
    uint64_t ref_bytes = ALIGN_UP(total_pages * sizeof(uint16_t), PAGE_SIZE);
    uint64_t meta_bytes = bmp_bytes + ref_bytes;

    uint64_t meta_phys = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->length >= meta_bytes) {
            uint64_t aligned_base = ALIGN_UP(e->base, PAGE_SIZE);
            uint64_t avail = (e->base + e->length) - aligned_base;
            if (avail >= meta_bytes && aligned_base >= 0x100000ULL) {
                meta_phys = aligned_base;
                break;
            }
        }
    }
    if (!meta_phys) {
        kpanic("PMM: No memory region large enough for %lu KiB metadata", meta_bytes / 1024);
    }

    bitmap    = (uint8_t *)phys_to_virt(meta_phys);
    refcounts = (uint16_t *)phys_to_virt(meta_phys + bmp_bytes);

    /* Mark everything allocated initially, then release USABLE pages */
    memset(bitmap, 0xFF, bmp_bytes);
    memset(refcounts, 0, ref_bytes);
    free_pages = 0;

    uint64_t usable_bytes = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;

        uint64_t start = ALIGN_UP(e->base, PAGE_SIZE);
        uint64_t end   = ALIGN_DOWN(e->base + e->length, PAGE_SIZE);
        if (end <= start) continue;

        usable_bytes += (end - start);
        for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
            uint64_t pfn = addr / PAGE_SIZE;
            if (pfn == 0) continue; /* keep NULL physical page 0 reserved */
            bmp_clear(pfn);
            free_pages++;
        }
    }

    /* Reserve PMM metadata itself */
    uint64_t meta_start_pfn = meta_phys / PAGE_SIZE;
    uint64_t meta_page_cnt  = meta_bytes / PAGE_SIZE;
    for (uint64_t p = meta_start_pfn; p < meta_start_pfn + meta_page_cnt; p++) {
        if (!bmp_test(p)) {
            bmp_set(p);
            refcounts[p] = 1;
            free_pages--;
        }
    }

    last_hint = 1;
    kprintf("[PMM] Usable RAM: %lu MiB (%lu free pages), meta @ 0x%lx (%lu KiB)\n",
            usable_bytes / (1024 * 1024), free_pages, meta_phys, meta_bytes / 1024);
}

uint64_t pmm_alloc_page(void) {
    uint64_t rf = irq_save_disable();
    for (uint64_t pass = 0; pass < 2; pass++) {
        uint64_t start = (pass == 0) ? last_hint : 1;
        uint64_t end   = (pass == 0) ? total_pages : last_hint;
        for (uint64_t pfn = start; pfn < end; pfn++) {
            if (!bmp_test(pfn)) {
                bmp_set(pfn);
                refcounts[pfn] = 1;
                free_pages--;
                last_hint = pfn + 1;
                if (last_hint >= total_pages) last_hint = 1;
                irq_restore(rf);
                uint64_t phys = pfn * PAGE_SIZE;
                memset(phys_to_virt(phys), 0, PAGE_SIZE);
                return phys;
            }
        }
    }
    irq_restore(rf);
    return 0;
}

uint64_t pmm_alloc_pages(size_t count) {
    if (count == 0) return 0;
    if (count == 1) return pmm_alloc_page();

    uint64_t rf = irq_save_disable();
    size_t run = 0;
    uint64_t run_start = 1;
    for (uint64_t pfn = 1; pfn < total_pages; pfn++) {
        if (!bmp_test(pfn)) {
            if (run == 0) run_start = pfn;
            run++;
            if (run == count) {
                for (size_t k = 0; k < count; k++) {
                    bmp_set(run_start + k);
                    refcounts[run_start + k] = 1;
                }
                free_pages -= count;
                irq_restore(rf);
                uint64_t phys = run_start * PAGE_SIZE;
                memset(phys_to_virt(phys), 0, count * PAGE_SIZE);
                return phys;
            }
        } else {
            run = 0;
        }
    }
    irq_restore(rf);
    return 0;
}

void pmm_ref_inc(uint64_t phys) {
    uint64_t pfn = phys / PAGE_SIZE;
    if (pfn == 0 || pfn >= total_pages) return;
    uint64_t rf = irq_save_disable();
    if (refcounts[pfn] < 0xFFFF) {
        refcounts[pfn]++;
    }
    irq_restore(rf);
}

uint16_t pmm_ref_get(uint64_t phys) {
    uint64_t pfn = phys / PAGE_SIZE;
    if (pfn == 0 || pfn >= total_pages) return 0;
    return refcounts[pfn];
}

void pmm_free_page(uint64_t phys) {
    if (phys == 0) return;
    uint64_t pfn = phys / PAGE_SIZE;
    if (pfn >= total_pages) return;

    uint64_t rf = irq_save_disable();
    if (bmp_test(pfn)) {
        if (refcounts[pfn] > 1) {
            refcounts[pfn]--;
        } else {
            refcounts[pfn] = 0;
            bmp_clear(pfn);
            free_pages++;
            if (pfn < last_hint) last_hint = pfn;
        }
    }
    irq_restore(rf);
}

void pmm_free_pages(uint64_t phys, size_t count) {
    for (size_t i = 0; i < count; i++) {
        pmm_free_page(phys + (uint64_t)i * PAGE_SIZE);
    }
}

void pmm_get_stats(pmm_stats_t *out) {
    if (!out) return;
    out->total_pages = total_pages;
    out->free_pages  = free_pages;
    out->used_pages  = total_pages - free_pages;
    out->total_bytes = total_pages * PAGE_SIZE;
    out->free_bytes  = free_pages * PAGE_SIZE;
}
