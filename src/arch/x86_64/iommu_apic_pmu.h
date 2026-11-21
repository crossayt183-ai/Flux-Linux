#ifndef FLUX_IOMMU_APIC_PMU_H
#define FLUX_IOMMU_APIC_PMU_H

#include "types.h"

typedef struct {
    uint16_t hw_rule_id;
    uint8_t  subsys_class; /* 1=VT-d-DMAR-IOMMU, 2=AMD-Vi-IOMMU, 3=x2APIC-MSIX-Router, 4=Arch-PMU-PEBS */
    uint8_t  apic_vector;
    uint32_t msr_or_mmio_reg;
    const char *unit_name;
    const char *remapping_policy;
    const char *isolation_guarantee;
} iommu_apic_pmu_rule_t;

typedef struct {
    bool     vtd_dmar_iova_strict;
    bool     x2apic_msix_remapping;
    bool     pmu_pebs_lbr_active;
    uint32_t iommu_domains_allocated;
    uint32_t msix_vectors_routed;
    uint32_t total_arch_hw_rules;
} iommu_apic_pmu_status_t;

void   iommu_apic_pmu_init(void);
bool   iommu_apic_pmu_get_status(iommu_apic_pmu_status_t *out);
size_t iommu_apic_pmu_db_size(void);
bool   iommu_apic_pmu_selftest(void);

#endif
