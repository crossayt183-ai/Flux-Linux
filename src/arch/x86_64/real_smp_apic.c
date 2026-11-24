#include "real_smp_apic.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "gdt.h"
#include "idt.h"
#include "cpu.h"
#include "io.h"
#include "timer.h"
#include "string.h"
#include "serial.h"

/* Local APIC MMIO register offsets */
#define LAPIC_REG_ID      0x0020U
#define LAPIC_REG_VER     0x0030U
#define LAPIC_REG_TPR     0x0080U
#define LAPIC_REG_EOI     0x00B0U
#define LAPIC_REG_SVR     0x00F0U
#define LAPIC_REG_ICR_LO  0x0300U
#define LAPIC_REG_ICR_HI  0x0310U

#define TRAMPOLINE_PHYS   0x00008000ULL

typedef struct PACKED {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t  ext_checksum;
    uint8_t  reserved[3];
} acpi_rsdp_t;

typedef struct PACKED {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_hdr_t;

typedef struct PACKED {
    acpi_sdt_hdr_t hdr;
    uint32_t       lapic_addr;
    uint32_t       flags;
} acpi_madt_t;

/* Shared naturally-aligned mailbox @ 0x8F00 for real-mode AP -> 64-bit Long Mode handoff */
typedef struct ALIGNED(8) {
    uint32_t cr3_phys32;
    uint32_t pad0;
    uint64_t entry_rip;
    uint64_t stack_top;
    uint32_t ap_ack;
    uint32_t ap_lapic_id;
} ap_mailbox_t;

static real_smp_info_t    g_smp;
static volatile uint8_t  *g_lapic_mmio = NULL;
static volatile uint32_t  g_ap_online_total = 1;

static inline uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t *)(g_lapic_mmio + reg);
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(g_lapic_mmio + reg) = val;
    (void)*(volatile uint32_t *)(g_lapic_mmio + LAPIC_REG_ID);
}

static bool acpi_checksum_ok(const void *ptr, size_t len) {
    const uint8_t *p = (const uint8_t *)ptr;
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) sum = (uint8_t)(sum + p[i]);
    return sum == 0;
}

static const acpi_rsdp_t *scan_rsdp_range(uint64_t phys_start, uint64_t phys_end) {
    for (uint64_t pa = phys_start; pa < phys_end; pa += 16) {
        const acpi_rsdp_t *r = (const acpi_rsdp_t *)phys_to_virt(pa);
        if (memcmp(r->signature, "RSD PTR ", 8) == 0 && acpi_checksum_ok(r, 20)) {
            return r;
        }
    }
    return NULL;
}

static const acpi_rsdp_t *find_acpi_rsdp(void) {
    uint16_t ebda_seg = *(const uint16_t *)phys_to_virt(0x040EULL);
    uint64_t ebda_pa  = ((uint64_t)ebda_seg << 4) & 0xFFFF0ULL;
    if (ebda_pa >= 0x80000ULL && ebda_pa < 0xA0000ULL) {
        const acpi_rsdp_t *r = scan_rsdp_range(ebda_pa, ebda_pa + 1024);
        if (r) return r;
    }
    return scan_rsdp_range(0x000E0000ULL, 0x00100000ULL);
}

static void parse_madt_entries(const acpi_madt_t *madt) {
    g_smp.lapic_phys_addr = madt->lapic_addr ? madt->lapic_addr : 0xFEE00000U;
    const uint8_t *cur = (const uint8_t *)madt + sizeof(acpi_madt_t);
    const uint8_t *end = (const uint8_t *)madt + madt->hdr.length;

    while (cur + 2 <= end) {
        uint8_t type = cur[0];
        uint8_t len  = cur[1];
        if (len < 2 || cur + len > end) break;

        if (type == 0 && len >= 8) {
            /* Processor Local APIC */
            uint8_t  apic_id = cur[3];
            uint32_t flags   = *(const uint32_t *)(cur + 4);
            if ((flags & 0x3U) != 0 && g_smp.madt_cpu_count < SMP_MAX_CPUS) {
                g_smp.apic_ids[g_smp.madt_cpu_count++] = apic_id;
            }
        } else if (type == 1 && len >= 12) {
            /* I/O APIC */
            g_smp.ioapic_phys_addr = *(const uint32_t *)(cur + 4);
        }
        cur += len;
    }
}

/* Secondary CPU 64-bit C entry reached after 16->32->64-bit trampoline @ 0x8000 */
static void ap_kernel_entry(void) {
    volatile ap_mailbox_t *mb = (volatile ap_mailbox_t *)phys_to_virt(0x8F00ULL);
    /* Enable NXE + SSE on this physical core */
    uint64_t efer = rdmsr(0xC0000080U);
    wrmsr(0xC0000080U, efer | (1ULL << 11));
    uint64_t cr0 = read_cr0();
    cr0 &= ~(1ULL << 2);
    cr0 |= (1ULL << 1) | (1ULL << 16);
    write_cr0(cr0);
    uint64_t cr4 = read_cr4();
    cr4 |= (1ULL << 9) | (1ULL << 10);
    write_cr4(cr4);

    if (g_lapic_mmio) {
        lapic_write(LAPIC_REG_SVR, lapic_read(LAPIC_REG_SVR) | 0x1FFU);
        lapic_write(LAPIC_REG_TPR, 0);
    }

    __atomic_fetch_add(&g_ap_online_total, 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&mb->ap_ack, 1U, __ATOMIC_SEQ_CST);

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* Hand-assembled position-fixed 16->32->64-bit AP startup trampoline @ 0x8000:
 *   0x8000:16-bit Real Mode -> loads temp GDT @ 0x8080 -> CR0.PE -> ljmp 0x08:0x8024
 *   0x8024:32-bit Prot Mode -> CR4.PAE -> CR3=[0x8F00] -> EFER.LME|NXE -> CR0.PG -> ljmp 0x18:0x8054
 *   0x8054:64-bit Long Mode -> loads RSP=[0x8F10], RAX=[0x8F08] -> call RAX (ap_kernel_entry)
 */
static const uint8_t g_ap_trampoline_bin[] = {
    /* 0x8000 (16-bit): cli; cld; xor ax, ax; mov ds, ax; mov es, ax; mov ss, ax */
    0xFA, 0xFC, 0x31, 0xC0, 0x8E, 0xD8, 0x8E, 0xC0, 0x8E, 0xD0,
    /* 0x800A: lgdt [0x8078] */
    0x0F, 0x01, 0x16, 0x78, 0x80,
    /* 0x800F: mov eax, cr0; or al, 1; mov cr0, eax */
    0x0F, 0x20, 0xC0, 0x0C, 0x01, 0x0F, 0x22, 0xC0,
    /* 0x8017: ljmp 0x08:0x8020 (pad 4 NOPs to 0x8020) */
    0xEA, 0x20, 0x80, 0x08, 0x00, 0x90, 0x90, 0x90, 0x90,
    /* 0x8020 (32-bit): mov ax, 0x10; mov ds, ax; mov es, ax; mov ss, ax */
    0x66, 0xB8, 0x10, 0x00, 0x8E, 0xD8, 0x8E, 0xC0, 0x8E, 0xD0,
    /* 0x802A: mov eax, cr4; or eax, 0x20 (PAE); mov cr4, eax */
    0x0F, 0x20, 0xE0, 0x83, 0xC8, 0x20, 0x0F, 0x22, 0xE0,
    /* 0x8033: mov eax, [0x8F00] (cr3_phys32); mov cr3, eax */
    0xA1, 0x00, 0x8F, 0x00, 0x00, 0x0F, 0x22, 0xD8,
    /* 0x803B: mov ecx, 0xC0000080; rdmsr; or eax, 0x900 (LME|NXE); wrmsr */
    0xB9, 0x80, 0x00, 0x00, 0xC0, 0x0F, 0x32, 0x0D, 0x00, 0x09, 0x00, 0x00, 0x0F, 0x30,
    /* 0x8049: mov eax, cr0; or eax, 0x80000001 (PG|PE); mov cr0, eax */
    0x0F, 0x20, 0xC0, 0x0D, 0x01, 0x00, 0x00, 0x80, 0x0F, 0x22, 0xC0,
    /* 0x8054: ljmp 0x18:0x8060 (pad NOPs to 0x8060) */
    0xEA, 0x60, 0x80, 0x00, 0x00, 0x18, 0x00, 0x90, 0x90, 0x90, 0x90, 0x90,
    /* 0x8060 (64-bit): mov ax, 0x10; mov ds, ax; mov es, ax; mov ss, ax */
    0x66, 0xB8, 0x10, 0x00, 0x8E, 0xD8, 0x8E, 0xC0, 0x8E, 0xD0,
    /* 0x806A: mov rsp, [0x8F10]; mov rax, [0x8F08]; call rax */
    0x48, 0x8B, 0x24, 0x25, 0x10, 0x8F, 0x00, 0x00,
    0x48, 0x8B, 0x04, 0x25, 0x08, 0x8F, 0x00, 0x00,
    0xFF, 0xD0,
    /* 0x807C->pad? Wait: let's place GDTR at 0x8078? No! 0x806A + 18 = 0x807C! */
};

/* Build self-contained trampoline + GDT @ 0x8000..0x8100 with exact offsets */
static void install_trampoline_at_8000(void) {
    uint8_t *dst = (uint8_t *)phys_to_virt(TRAMPOLINE_PHYS);
    memset(dst, 0x90, 512);

    /* Patch GDTR offset in lgdt [0x8090] instead of 0x8078 so code 0x8000..0x807C never overlaps! */
    memcpy(dst, g_ap_trampoline_bin, sizeof(g_ap_trampoline_bin));
    dst[13] = 0x90; /* lgdt [0x8090] low byte */
    dst[14] = 0x80; /* lgdt [0x8090] high byte */

    /* GDT @ 0x80A0: [0]=null, [1]=0x08 32-bit code, [2]=0x10 data, [3]=0x18 64-bit code */
    uint64_t *gdt64 = (uint64_t *)(dst + 0xA0);
    gdt64[0] = 0x0000000000000000ULL;
    gdt64[1] = 0x00CF9A000000FFFFULL; /* 0x08: 32-bit flat code */
    gdt64[2] = 0x00CF92000000FFFFULL; /* 0x10: flat data */
    gdt64[3] = 0x00AF9A000000FFFFULL; /* 0x18: 64-bit long-mode code */

    /* 6-byte GDTR @ 0x8090 */
    *(uint16_t *)(dst + 0x90) = 31U;
    *(uint32_t *)(dst + 0x92) = 0x000080A0U;
}

static void boot_secondary_aps(void) {
    if (g_smp.madt_cpu_count <= 1 || !g_lapic_mmio) return;

    vm_space_t *ksp = vmm_kernel_space();
    if (ksp->pml4_phys >= 0x100000000ULL) return; /* 32-bit CR3 required in 32-bit step */

    /* Save low physical page 0x8000 and PML4[0] identity mapping */
    uint8_t *saved_page = (uint8_t *)kmalloc(PAGE_SIZE);
    if (!saved_page) return;
    memcpy(saved_page, phys_to_virt(TRAMPOLINE_PHYS), PAGE_SIZE);

    uint64_t *pml4 = (uint64_t *)phys_to_virt(ksp->pml4_phys);
    uint64_t saved_pml4_0 = pml4[0];
    bool mapped_id = vmm_map_page(ksp, TRAMPOLINE_PHYS, TRAMPOLINE_PHYS, PTE_PRESENT | PTE_WRITE);

    if (mapped_id) {
        install_trampoline_at_8000();
        volatile ap_mailbox_t *mb = (volatile ap_mailbox_t *)phys_to_virt(0x8F00ULL);

        for (uint32_t i = 0; i < g_smp.madt_cpu_count; i++) {
            uint8_t apic_id = g_smp.apic_ids[i];
            if (apic_id == (uint8_t)g_smp.bsp_lapic_id) continue;

            void *ap_stack = kmalloc(16384);
            if (!ap_stack) break;

            mb->cr3_phys32  = (uint32_t)ksp->pml4_phys;
            mb->entry_rip   = (uint64_t)(uintptr_t)ap_kernel_entry;
            mb->stack_top   = (uint64_t)(uintptr_t)ap_stack + 16384ULL;
            mb->ap_ack      = 0;
            mb->ap_lapic_id = apic_id;
            __asm__ volatile ("mfence" ::: "memory");

            /* Send INIT IPI (0x00004500) */
            lapic_write(LAPIC_REG_ICR_HI, ((uint32_t)apic_id) << 24);
            lapic_write(LAPIC_REG_ICR_LO, 0x00004500U);
            for (volatile int d = 0; d < 200000; d++) __asm__ volatile ("pause");

            /* Send Startup IPI (SIPI) #1 -> vector 0x08 (0x8000) */
            lapic_write(LAPIC_REG_ICR_HI, ((uint32_t)apic_id) << 24);
            lapic_write(LAPIC_REG_ICR_LO, 0x00004608U);
            for (volatile int d = 0; d < 100000; d++) {
                if (__atomic_load_n(&mb->ap_ack, __ATOMIC_SEQ_CST)) break;
                __asm__ volatile ("pause");
            }

            if (!__atomic_load_n(&mb->ap_ack, __ATOMIC_SEQ_CST)) {
                /* Send Startup IPI (SIPI) #2 per Intel MP spec */
                lapic_write(LAPIC_REG_ICR_HI, ((uint32_t)apic_id) << 24);
                lapic_write(LAPIC_REG_ICR_LO, 0x00004608U);
                for (volatile int d = 0; d < 500000; d++) {
                    if (__atomic_load_n(&mb->ap_ack, __ATOMIC_SEQ_CST)) break;
                    __asm__ volatile ("pause");
                }
            }
        }
    }

    /* Restore original low memory & PML4[0] */
    memcpy(phys_to_virt(TRAMPOLINE_PHYS), saved_page, PAGE_SIZE);
    pml4[0] = saved_pml4_0;
    invlpg(TRAMPOLINE_PHYS);
    kfree(saved_page);
}

void real_smp_apic_init(void) {
    memset(&g_smp, 0, sizeof(g_smp));
    g_smp.lapic_phys_addr  = 0xFEE00000U;
    g_smp.online_cpu_count = 1;
    g_ap_online_total      = 1;

    const acpi_rsdp_t *rsdp = find_acpi_rsdp();
    if (rsdp) {
        g_smp.acpi_revision = rsdp->revision;
        if (rsdp->revision >= 2 && rsdp->xsdt_addr != 0) {
            const acpi_sdt_hdr_t *xsdt = (const acpi_sdt_hdr_t *)vmm_map_mmio(rsdp->xsdt_addr, 4096);
            if (xsdt && memcmp(xsdt->signature, "XSDT", 4) == 0 && xsdt->length <= 4096) {
                size_t entries = (xsdt->length - sizeof(acpi_sdt_hdr_t)) / 8;
                const uint64_t *ptrs = (const uint64_t *)((const uint8_t *)xsdt + sizeof(acpi_sdt_hdr_t));
                for (size_t i = 0; i < entries; i++) {
                    const acpi_sdt_hdr_t *t = (const acpi_sdt_hdr_t *)vmm_map_mmio(ptrs[i], 4096);
                    if (t && memcmp(t->signature, "APIC", 4) == 0) {
                        g_smp.acpi_madt_found = true;
                        parse_madt_entries((const acpi_madt_t *)t);
                        break;
                    }
                }
            }
        }
        if (!g_smp.acpi_madt_found && rsdp->rsdt_addr != 0) {
            const acpi_sdt_hdr_t *rsdt = (const acpi_sdt_hdr_t *)vmm_map_mmio(rsdp->rsdt_addr, 4096);
            if (rsdt && memcmp(rsdt->signature, "RSDT", 4) == 0 && rsdt->length <= 4096) {
                size_t entries = (rsdt->length - sizeof(acpi_sdt_hdr_t)) / 4;
                const uint32_t *ptrs = (const uint32_t *)((const uint8_t *)rsdt + sizeof(acpi_sdt_hdr_t));
                for (size_t i = 0; i < entries; i++) {
                    const acpi_sdt_hdr_t *t = (const acpi_sdt_hdr_t *)vmm_map_mmio(ptrs[i], 4096);
                    if (t && memcmp(t->signature, "APIC", 4) == 0) {
                        g_smp.acpi_madt_found = true;
                        parse_madt_entries((const acpi_madt_t *)t);
                        break;
                    }
                }
            }
        }
    }

    if (g_smp.madt_cpu_count == 0) {
        g_smp.madt_cpu_count = 1;
        g_smp.apic_ids[0]    = 0;
    }

    g_lapic_mmio = (volatile uint8_t *)vmm_map_mmio(g_smp.lapic_phys_addr, 4096);
    if (g_lapic_mmio) {
        g_smp.bsp_lapic_id  = lapic_read(LAPIC_REG_ID) >> 24;
        g_smp.lapic_version = lapic_read(LAPIC_REG_VER) & 0xFFU;
        boot_secondary_aps();
    }

    g_smp.online_cpu_count = __atomic_load_n(&g_ap_online_total, __ATOMIC_SEQ_CST);
    kprintf("[SMP/HW-APIC] ACPI MADT=%s (rev=%u) LAPIC@0x%x ver=0x%x BSP=%u | Online CPUs: %u/%u\n",
            g_smp.acpi_madt_found ? "YES" : "FALLBACK",
            g_smp.acpi_revision, g_smp.lapic_phys_addr, g_smp.lapic_version,
            g_smp.bsp_lapic_id, g_smp.online_cpu_count, g_smp.madt_cpu_count);
}

void real_smp_get_info(real_smp_info_t *out) {
    if (out) *out = g_smp;
}

uint32_t real_smp_online_cpus(void) {
    return g_smp.online_cpu_count;
}

bool real_smp_apic_selftest(void) {
    return g_lapic_mmio != NULL && g_smp.lapic_version != 0 && g_smp.online_cpu_count >= 1;
}
