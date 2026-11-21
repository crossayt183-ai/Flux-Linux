#ifndef FLUX_NET_RESILIENCE_H
#define FLUX_NET_RESILIENCE_H

#include "types.h"

typedef struct {
    uint16_t policy_id;
    uint8_t  proto;
    uint8_t  action; /* 1=Sanitize, 2=SYN-Cookie, 3=BBR-Pace, 4=NIC-Ring-Heal */
    uint16_t min_len;
    uint16_t max_burst_pps;
    const char *guard_name;
    const char *mitigation_fn;
} net_guard_rule_t;

typedef struct {
    bool     bbr_v3_enabled;
    bool     syn_cookies_active;
    bool     nic_watchdog_ok;
    uint32_t pacing_rate_mbps;
    uint32_t malformed_pkts_scrubbed;
    uint32_t tx_hangs_recovered;
} net_resilience_status_t;

void   net_resilience_init(void);
bool   net_resilience_check(net_resilience_status_t *out);
size_t net_guard_rule_db_size(void);
bool   net_resilience_selftest(void);

#endif
