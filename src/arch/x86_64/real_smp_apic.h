#ifndef FLUX_REAL_SMP_APIC_H
#define FLUX_REAL_SMP_APIC_H

#include "types.h"

#define SMP_MAX_CPUS 32

typedef struct {
    bool     acpi_madt_found;
    uint8_t  acpi_revision;
    uint32_t lapic_phys_addr;
    uint32_t ioapic_phys_addr;
    uint32_t bsp_lapic_id;
    uint32_t lapic_version;
    uint32_t madt_cpu_count;
    uint32_t online_cpu_count;
    uint8_t  apic_ids[SMP_MAX_CPUS];
} real_smp_info_t;

void     real_smp_apic_init(void);
void     real_smp_get_info(real_smp_info_t *out);
uint32_t real_smp_online_cpus(void);
bool     real_smp_apic_selftest(void);

#endif
