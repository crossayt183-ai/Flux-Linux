#ifndef FLUX_CPU_H
#define FLUX_CPU_H

#include "types.h"

static inline uint64_t read_cr0(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(val));
    return val;
}

static inline void write_cr0(uint64_t val) {
    __asm__ volatile ("mov %0, %%cr0" : : "r"(val) : "memory");
}

static inline uint64_t read_cr2(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(val));
    return val;
}

static inline uint64_t read_cr3(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void write_cr3(uint64_t val) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(val) : "memory");
}

static inline uint64_t read_cr4(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(val));
    return val;
}

static inline void write_cr4(uint64_t val) {
    __asm__ volatile ("mov %0, %%cr4" : : "r"(val) : "memory");
}

static inline void invlpg(uint64_t vaddr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(vaddr) : "memory");
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t lo = (uint32_t)(val & 0xFFFFFFFFULL);
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static inline void cli(void) {
    __asm__ volatile ("cli" ::: "memory");
}

static inline void sti(void) {
    __asm__ volatile ("sti" ::: "memory");
}

static inline void hlt(void) {
    __asm__ volatile ("hlt");
}

static inline uint64_t read_rflags(void) {
    uint64_t rf;
    __asm__ volatile ("pushfq; pop %0" : "=r"(rf));
    return rf;
}

static inline uint64_t irq_save_disable(void) {
    uint64_t rf = read_rflags();
    cli();
    return rf;
}

static inline void irq_restore(uint64_t rf) {
    if (rf & (1ULL << 9)) {
        sti();
    }
}

#endif
