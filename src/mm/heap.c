#include "heap.h"
#include "pmm.h"
#include "cpu.h"
#include "string.h"
#include "serial.h"

#define SLAB_MAGIC 0x46534C42U /* "FSLB" */
#define LARGE_MAGIC 0x464C5247U /* "FLRG" */
#define NUM_CACHES 8

static const size_t cache_sizes[NUM_CACHES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048
};

typedef struct slab_hdr {
    uint32_t         magic;
    uint16_t         cache_idx;
    uint16_t         free_count;
    uint16_t         capacity;
    uint16_t         reserved;
    void            *freelist;
    struct slab_hdr *prev;
    struct slab_hdr *next;
} slab_hdr_t;

#define SLAB_HDR_SIZE 64U /* >= sizeof(slab_hdr_t), 64-byte aligned */

typedef struct {
    uint32_t magic;
    uint32_t pages;
    size_t   req_size;
    uint64_t pad[2];
} large_hdr_t;

#define LARGE_HDR_SIZE 32U /* == sizeof(large_hdr_t), 16-byte aligned */

static slab_hdr_t  *caches[NUM_CACHES];
static heap_stats_t stats;

static int find_cache_index(size_t size) {
    for (int i = 0; i < NUM_CACHES; i++) {
        if (size <= cache_sizes[i]) return i;
    }
    return -1;
}

static slab_hdr_t *slab_create(int idx) {
    uint64_t phys = pmm_alloc_page();
    if (!phys) return NULL;

    uint8_t *base = (uint8_t *)phys_to_virt(phys);
    slab_hdr_t *slab = (slab_hdr_t *)base;
    size_t obj_size = cache_sizes[idx];
    uint16_t cap = (uint16_t)((PAGE_SIZE - SLAB_HDR_SIZE) / obj_size);

    slab->magic      = SLAB_MAGIC;
    slab->cache_idx  = (uint16_t)idx;
    slab->capacity   = cap;
    slab->free_count = cap;
    slab->prev       = NULL;
    slab->next       = caches[idx];

    void *head = NULL;
    for (int i = (int)cap - 1; i >= 0; i--) {
        void *obj = base + SLAB_HDR_SIZE + (size_t)i * obj_size;
        *(void **)obj = head;
        head = obj;
    }
    slab->freelist = head;

    if (caches[idx]) caches[idx]->prev = slab;
    caches[idx] = slab;
    stats.slab_pages++;
    return slab;
}

static void slab_destroy(slab_hdr_t *slab) {
    int idx = slab->cache_idx;
    if (slab->prev) slab->prev->next = slab->next;
    else caches[idx] = slab->next;
    if (slab->next) slab->next->prev = slab->prev;

    stats.slab_pages--;
    pmm_free_page(virt_to_phys_hhdm(slab));
}

void heap_init(void) {
    memset(caches, 0, sizeof(caches));
    memset(&stats, 0, sizeof(stats));
    kprintf("[HEAP] Slab allocator ready (caches: 16..2048 B + multi-page)\n");
}

void *kmalloc(size_t size) {
    if (size == 0) size = 1;
    uint64_t rf = irq_save_disable();

    int idx = find_cache_index(size);
    if (idx >= 0) {
        slab_hdr_t *slab = caches[idx];
        while (slab && slab->free_count == 0) {
            slab = slab->next;
        }
        if (!slab) {
            slab = slab_create(idx);
            if (!slab) {
                irq_restore(rf);
                return NULL;
            }
        }
        void *obj = slab->freelist;
        slab->freelist = *(void **)obj;
        slab->free_count--;

        stats.total_allocs++;
        stats.active_allocs++;
        stats.bytes_allocated += cache_sizes[idx];
        irq_restore(rf);
        return obj;
    }

    size_t pages = ALIGN_UP(size + LARGE_HDR_SIZE, PAGE_SIZE) / PAGE_SIZE;
    uint64_t phys = pmm_alloc_pages(pages);
    if (!phys) {
        irq_restore(rf);
        return NULL;
    }
    uint8_t *base = (uint8_t *)phys_to_virt(phys);
    large_hdr_t *lh = (large_hdr_t *)base;
    lh->magic    = LARGE_MAGIC;
    lh->pages    = (uint32_t)pages;
    lh->req_size = pages * PAGE_SIZE - LARGE_HDR_SIZE;

    stats.total_allocs++;
    stats.active_allocs++;
    stats.large_pages += pages;
    stats.bytes_allocated += pages * PAGE_SIZE;
    irq_restore(rf);
    return base + LARGE_HDR_SIZE;
}

void *kcalloc(size_t nmemb, size_t size) {
    if (nmemb != 0 && size > ((size_t)-1) / nmemb) return NULL;
    size_t total = nmemb * size;
    void *p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void kfree(void *ptr) {
    if (!ptr) return;
    uint64_t rf = irq_save_disable();

    uintptr_t page_base = ALIGN_DOWN((uintptr_t)ptr, PAGE_SIZE);
    uint32_t magic = *(const uint32_t *)page_base;

    if (magic == SLAB_MAGIC) {
        slab_hdr_t *slab = (slab_hdr_t *)page_base;
        *(void **)ptr = slab->freelist;
        slab->freelist = ptr;
        slab->free_count++;

        stats.total_frees++;
        stats.active_allocs--;
        stats.bytes_allocated -= cache_sizes[slab->cache_idx];

        /* Keep at least 1 warm slab per cache, release extra empty slabs */
        if (slab->free_count == slab->capacity && (slab->prev || slab->next)) {
            slab_destroy(slab);
        }
        irq_restore(rf);
        return;
    }

    if (magic == LARGE_MAGIC) {
        large_hdr_t *lh = (large_hdr_t *)page_base;
        uint32_t pages = lh->pages;
        lh->magic = 0;

        stats.total_frees++;
        stats.active_allocs--;
        stats.large_pages -= pages;
        stats.bytes_allocated -= (uint64_t)pages * PAGE_SIZE;

        pmm_free_pages(virt_to_phys_hhdm((void *)page_base), pages);
        irq_restore(rf);
        return;
    }

    irq_restore(rf);
    kpanic("kfree: corrupted header magic=0x%x @ ptr=%p", magic, ptr);
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    uintptr_t page_base = ALIGN_DOWN((uintptr_t)ptr, PAGE_SIZE);
    uint32_t magic = *(const uint32_t *)page_base;
    size_t old_size = 0;

    if (magic == SLAB_MAGIC) {
        slab_hdr_t *slab = (slab_hdr_t *)page_base;
        old_size = cache_sizes[slab->cache_idx];
        if (new_size <= old_size) return ptr;
    } else if (magic == LARGE_MAGIC) {
        large_hdr_t *lh = (large_hdr_t *)page_base;
        old_size = lh->req_size;
        if (new_size <= old_size) return ptr;
    } else {
        kpanic("krealloc: invalid ptr=%p", ptr);
    }

    void *np = kmalloc(new_size);
    if (!np) return NULL;
    memcpy(np, ptr, MIN(old_size, new_size));
    kfree(ptr);
    return np;
}

void heap_get_stats(heap_stats_t *out) {
    if (!out) return;
    uint64_t rf = irq_save_disable();
    *out = stats;
    irq_restore(rf);
}
