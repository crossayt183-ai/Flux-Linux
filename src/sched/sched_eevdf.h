#ifndef FLUX_SCHED_EEVDF_H
#define FLUX_SCHED_EEVDF_H

#include "types.h"

typedef struct {
    uint16_t profile_id;
    uint8_t  sched_policy; /* 0=EEVDF_NORMAL, 1=LAVD_GAMING_RT, 2=SCHED_DEADLINE, 3=IDLE_BATCH */
    int8_t   nice_prio;
    uint16_t slice_us;
    uint16_t latency_target_us;
    const char *workload_class;
    const char *smt_cache_affinity;
    const char *deadline_formula;
} eevdf_workload_rule_t;

typedef struct {
    bool     eevdf_active;
    bool     lavd_steamos_gaming_mode;
    bool     smt_sibling_balancer;
    uint32_t min_slice_us;
    uint64_t vruntime_migrations;
    char     active_governor[64];
} sched_eevdf_status_t;

void   sched_eevdf_init(void);
bool   sched_eevdf_get_status(sched_eevdf_status_t *out);
size_t sched_eevdf_db_size(void);
bool   sched_eevdf_selftest(void);

#endif
