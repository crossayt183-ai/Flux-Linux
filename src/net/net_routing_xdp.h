#ifndef FLUX_NET_ROUTING_XDP_H
#define FLUX_NET_ROUTING_XDP_H

#include "types.h"

typedef struct {
    uint16_t route_id;
    uint8_t  layer_kind; /* 1=FIB-ECMP-Route, 2=eBPF-XDP-FastPath, 3=WiFi7-MLO, 4=BT5.4-LE-Audio, 5=WireGuard */
    uint8_t  prefix_len;
    uint32_t metric_cost;
    const char *subnet_or_band;
    const char *next_hop_engine;
    const char *qos_discipline;
} net_routing_xdp_rule_t;

typedef struct {
    bool     xdp_zero_copy_online;
    bool     fib_trie_multipath;
    bool     wifi7_mlo_320mhz;
    bool     bt54_le_isochronous;
    bool     wireguard_chacha20;
    uint32_t xdp_mpps_capacity;
} net_routing_xdp_status_t;

void   net_routing_xdp_init(void);
bool   net_routing_xdp_get_status(net_routing_xdp_status_t *out);
size_t net_routing_xdp_db_size(void);
bool   net_routing_xdp_selftest(void);

#endif
