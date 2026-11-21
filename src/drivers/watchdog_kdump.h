#ifndef FLUX_WATCHDOG_KDUMP_H
#define FLUX_WATCHDOG_KDUMP_H

#include "types.h"

typedef struct {
    uint16_t probe_id;
    uint16_t timeout_ms;
    uint32_t subsystem_tag;
    uint32_t recovery_flags;
    const char *subsystem_name;
    const char *health_invariant;
    const char *auto_unstick_fn;
} wdt_health_probe_t;

typedef struct {
    bool     itco_wdt_active;
    bool     nmi_lockup_guard;
    bool     pstore_blackbox_ready;
    uint32_t timeout_sec;
    uint64_t heartbeats_total;
    uint32_t irq_storms_throttled;
    uint32_t deadlocks_prevented;
    char     wdt_chipset[64];
} wdt_kdump_status_t;

void   watchdog_kdump_init(void);
void   watchdog_heartbeat(void);
bool   watchdog_get_status(wdt_kdump_status_t *out);
size_t watchdog_probe_db_size(void);
bool   watchdog_kdump_selftest(void);

#endif
