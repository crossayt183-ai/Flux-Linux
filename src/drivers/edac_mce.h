#ifndef FLUX_EDAC_MCE_H
#define FLUX_EDAC_MCE_H

#include "types.h"

typedef struct {
    uint16_t rule_id;
    uint8_t  mca_bank;
    uint8_t  severity; /* 0=Corrected(CE), 1=Recoverable(UCNA), 2=SRAO, 3=Fatal */
    uint32_t mcacod_mask;
    uint32_t mscod_sig;
    uint32_t auto_heal_action;
    const char *unit_name;
    const char *err_desc;
    const char *recovery_strategy;
} edac_mca_rule_t;

typedef struct {
    bool     mca_online;
    uint32_t active_banks;
    uint32_t corrected_errors;
    uint32_t quarantined_pages;
    uint32_t scrub_Bandwidth_mbps;
    char     imc_controller[64];
    char     last_heal_event[80];
} edac_status_t;

void   edac_mce_init(void);
bool   edac_get_status(edac_status_t *out);
bool   edac_scrub_memory_range(uint64_t phys_start, uint64_t length);
size_t edac_rule_db_size(void);
bool   edac_mce_selftest(void);

#endif
