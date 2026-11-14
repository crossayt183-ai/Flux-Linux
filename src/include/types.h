#ifndef FLUX_TYPES_H
#define FLUX_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE  4096ULL
#define PAGE_MASK  (~(PAGE_SIZE - 1ULL))

#define ALIGN_UP(x, a)   (((uint64_t)(x) + ((uint64_t)(a) - 1ULL)) & ~((uint64_t)(a) - 1ULL))
#define ALIGN_DOWN(x, a) ((uint64_t)(x) & ~((uint64_t)(a) - 1ULL))

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

#define PACKED   __attribute__((packed))
#define ALIGNED(n) __attribute__((aligned(n)))
#define NORETURN __attribute__((noreturn))
#define UNUSED   __attribute__((unused))

#endif
