#include "gdt.h"
#include "string.h"

typedef struct PACKED {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} gdt_entry_t;

typedef struct PACKED {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} gdt_tss_entry_t;

typedef struct PACKED {
    gdt_entry_t     null;       /* 0x00 */
    gdt_entry_t     kcode;      /* 0x08 */
    gdt_entry_t     kdata;      /* 0x10 */
    gdt_entry_t     udata;      /* 0x18 (DPL=3) */
    gdt_entry_t     ucode;      /* 0x20 (DPL=3, 64-bit) */
    gdt_tss_entry_t tss;        /* 0x28 (16-byte system descriptor) */
} gdt_table_t;

typedef struct PACKED {
    uint16_t limit;
    uint64_t base;
} gdt_ptr_t;

static gdt_table_t gdt;
static tss64_t     tss;
static uint8_t     ist1_stack[8192] ALIGNED(16);

extern void gdt_flush(gdt_ptr_t *gdtr);
extern void tss_flush(uint16_t selector);

static void set_entry(gdt_entry_t *e, uint8_t access, uint8_t gran) {
    e->limit_low   = 0xFFFF;
    e->base_low    = 0;
    e->base_mid    = 0;
    e->access      = access;
    e->granularity = gran;
    e->base_high   = 0;
}

void gdt_init(void) {
    memset(&gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    set_entry(&gdt.kcode, 0x9A, 0x20); /* Present, DPL=0, Exec/Read, Long Mode */
    set_entry(&gdt.kdata, 0x92, 0x00); /* Present, DPL=0, Read/Write */
    set_entry(&gdt.udata, 0xF2, 0x00); /* Present, DPL=3, Read/Write */
    set_entry(&gdt.ucode, 0xFA, 0x20); /* Present, DPL=3, Exec/Read, Long Mode */

    uint64_t tss_base = (uint64_t)(uintptr_t)&tss;
    uint32_t tss_lim  = (uint32_t)(sizeof(tss64_t) - 1);

    tss.ist1       = (uint64_t)(uintptr_t)(ist1_stack + sizeof(ist1_stack));
    tss.iomap_base = sizeof(tss64_t);

    gdt.tss.limit_low   = (uint16_t)(tss_lim & 0xFFFF);
    gdt.tss.base_low    = (uint16_t)(tss_base & 0xFFFF);
    gdt.tss.base_mid    = (uint8_t)((tss_base >> 16) & 0xFF);
    gdt.tss.access      = 0x89; /* Present, 64-bit TSS Available */
    gdt.tss.granularity = (uint8_t)((tss_lim >> 16) & 0x0F);
    gdt.tss.base_high   = (uint8_t)((tss_base >> 24) & 0xFF);
    gdt.tss.base_upper  = (uint32_t)(tss_base >> 32);
    gdt.tss.reserved    = 0;

    gdt_ptr_t gdtr = {
        .limit = (uint16_t)(sizeof(gdt) - 1),
        .base  = (uint64_t)(uintptr_t)&gdt
    };
    gdt_flush(&gdtr);
    tss_flush(GDT_TSS_SEL);
}

void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
