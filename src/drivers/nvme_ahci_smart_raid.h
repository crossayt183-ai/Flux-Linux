#ifndef FLUX_NVME_AHCI_SMART_RAID_H
#define FLUX_NVME_AHCI_SMART_RAID_H

#include "types.h"

typedef struct {
    uint32_t rule_id;
    uint16_t hw_class;
    uint16_t param_val;
    const char *profile_key;
    const char *fastpath_engine;
    const char *telemetry_bound;
} nvme_ahci_smart_raid_rule_t;

void   nvme_ahci_smart_raid_init(void);
size_t nvme_ahci_smart_raid_db_size(void);
bool   nvme_ahci_smart_raid_selftest(void);

#endif
