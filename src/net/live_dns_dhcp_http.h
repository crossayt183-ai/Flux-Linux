#ifndef FLUX_LIVE_DNS_DHCP_HTTP_H
#define FLUX_LIVE_DNS_DHCP_HTTP_H

#include "types.h"

typedef struct {
    bool     dhcp_codec_verified;
    bool     dns_codec_verified;
    bool     nat_dns_reachable;
    uint32_t leased_ip;
    uint32_t dns_server_ip;
    uint32_t last_resolved_ip;
} live_net_client_status_t;

void   live_dns_dhcp_http_init(void);
size_t dns_encode_query(uint8_t *out_pkt, size_t cap, uint16_t txid, const char *hostname);
bool   dns_decode_a_record(const uint8_t *pkt, size_t len, uint16_t expected_txid, uint32_t *out_ipv4);
bool   dns_resolve_ipv4(const char *hostname, uint32_t dns_ip, uint32_t timeout_ms, uint32_t *out_ipv4);
size_t dhcp_build_discover(uint8_t *out_pkt, size_t cap, uint32_t xid, const uint8_t mac[6]);
bool   dhcp_parse_offer_or_ack(const uint8_t *pkt, size_t len, uint32_t xid,
                               uint32_t *out_yiaddr, uint32_t *out_mask,
                               uint32_t *out_gw, uint32_t *out_dns, uint8_t *out_msg_type);
int64_t http_get_ipv4(uint32_t server_ip, uint16_t port, const char *host, const char *path,
                      char *resp_out, size_t resp_cap, uint32_t timeout_ms);
bool   live_dns_dhcp_http_selftest(void);

#endif
