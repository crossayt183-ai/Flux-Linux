#ifndef FLUX_HEAP_H
#define FLUX_HEAP_H

#include "types.h"

typedef struct {
    uint64_t total_allocs;
    uint64_t total_frees;
    uint64_t active_allocs;
    uint64_t bytes_allocated;
    uint64_t slab_pages;
    uint64_t large_pages;
} heap_stats_t;

void  heap_init(void);
void *kmalloc(size_t size);
void *kcalloc(size_t nmemb, size_t size);
void *krealloc(void *ptr, size_t new_size);
void  kfree(void *ptr);

void  heap_get_stats(heap_stats_t *out);

#endif
