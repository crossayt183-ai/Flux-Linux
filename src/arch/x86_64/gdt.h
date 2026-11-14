#ifndef FLUX_GDT_H
#define FLUX_GDT_H

#include "types.h"

/* Selectors aligned with x86_64 SYSCALL / SYSRET ordering */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18
#define GDT_USER_CODE   0x20
#define GDT_TSS_SEL     0x28

typedef struct PACKED {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} tss64_t;

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);

#endif
