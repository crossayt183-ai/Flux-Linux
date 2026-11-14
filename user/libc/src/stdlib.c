#include "stdlib.h"
#include "unistd.h"
#include "string.h"

typedef struct block_hdr {
    size_t            size;
    bool              free;
    struct block_hdr *next;
} block_hdr_t;

static block_hdr_t *heap_head = NULL;
static block_hdr_t *heap_tail = NULL;

static size_t align16(size_t n) {
    return (n + 15ULL) & ~15ULL;
}

void *malloc(size_t size) {
    if (size == 0) size = 16;
    size = align16(size);

    for (block_hdr_t *b = heap_head; b; b = b->next) {
        if (b->free && b->size >= size) {
            if (b->size >= size + sizeof(block_hdr_t) + 16) {
                block_hdr_t *rem = (block_hdr_t *)((uint8_t *)(b + 1) + size);
                rem->size = b->size - size - sizeof(block_hdr_t);
                rem->free = true;
                rem->next = b->next;
                b->size = size;
                b->next = rem;
                if (heap_tail == b) heap_tail = rem;
            }
            b->free = false;
            return (void *)(b + 1);
        }
    }

    size_t total = sizeof(block_hdr_t) + size;
    void *mem = sbrk((int64_t)total);
    if (mem == (void *)-1) return NULL;

    block_hdr_t *nb = (block_hdr_t *)mem;
    nb->size = size;
    nb->free = false;
    nb->next = NULL;

    if (!heap_head) heap_head = nb;
    if (heap_tail) heap_tail->next = nb;
    heap_tail = nb;
    return (void *)(nb + 1);
}

void free(void *ptr) {
    if (!ptr) return;
    block_hdr_t *b = ((block_hdr_t *)ptr) - 1;
    b->free = true;

    for (block_hdr_t *cur = heap_head; cur && cur->next; ) {
        if (cur->free && cur->next->free &&
            (uint8_t *)(cur + 1) + cur->size == (uint8_t *)cur->next) {
            cur->size += sizeof(block_hdr_t) + cur->next->size;
            if (heap_tail == cur->next) heap_tail = cur;
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }
    block_hdr_t *b = ((block_hdr_t *)ptr) - 1;
    if (b->size >= size) return ptr;

    void *np = malloc(size);
    if (!np) return NULL;
    memcpy(np, ptr, b->size);
    free(ptr);
    return np;
}

int atoi(const char *s) {
    int sign = 1, v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return sign * v;
}
