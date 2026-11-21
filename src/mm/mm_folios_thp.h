#ifndef FLUX_MM_FOLIOS_THP_H
#define FLUX_MM_FOLIOS_THP_H

#include "types.h"

typedef struct {
    uint16_t policy_id;
    uint8_t  folio_order; /* 0=4KB, 9=2MB THP, 18=1GB Giant Page */
    uint8_t  mglru_gen;   /* Generation 0 (coldest) .. 3 (hottest) */
    uint16_t watermark_pct;
    uint16_t ksm_merge_prio;
    const char *zone_pool_name;
    const char *reclaim_heuristic;
    const char *compaction_action;
} mm_folio_policy_t;

typedef struct {
    bool     thp_2mb_1gb_online;
    bool     mglru_4gen_active;
    bool     ksm_dedup_enabled;
    uint32_t hugepages_allocated;
    uint32_t ksm_pages_merged;
    uint32_t page_cache_hit_pct;
} mm_folios_status_t;

void   mm_folios_thp_init(void);
bool   mm_folios_get_status(mm_folios_status_t *out);
size_t mm_folios_policy_db_size(void);
bool   mm_folios_thp_selftest(void);

#endif
