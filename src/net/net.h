#ifndef FLUX_NET_H
#define FLUX_NET_H

#include "types.h"

#define ETH_TYPE_IPV4 0x0800U
#define ETH_TYPE_ARP  0x0806U

#define IP_PROTO_ICMP 1U
#define IP_PROTO_TCP  6U
#define IP_PROTO_UDP  17U

static inline uint16_t htons(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static inline uint16_t ntohs(uint16_t v) { return htons(v); }
static inline uint32_t htonl(uint32_t v) {
    return ((v & 0x000000FFU) << 24) | ((v & 0x0000FF00U) << 8) |
           ((v & 0x00FF0000U) >> 8)  | ((v & 0xFF000000U) >> 24);
}
static inline uint32_t ntohl(uint32_t v) { return htonl(v); }

static inline uint32_t ip4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
}

typedef struct {
    uint8_t  mac[6];
    uint32_t ip;
    uint32_t mask;
    uint32_t gw;
    uint64_t rx_packets;
    uint64_t tx_packets;
} net_info_t;

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    bool     valid;
} arp_entry_t;

void net_init(void);
void net_poll(void);
void net_get_info(net_info_t *out);
bool net_is_local_ip(uint32_t ip);
uint16_t net_checksum(const void *data, size_t len);
uint16_t net_pseudo_checksum(uint32_t src_ip, uint32_t dst_ip, uint8_t proto,
                             const void *payload, uint16_t len);

/* Ethernet & ARP */
int  eth_send(const uint8_t dst_mac[6], uint16_t ethertype, const void *payload, size_t len);
void arp_rx(const uint8_t *pkt, size_t len);
bool arp_resolve(uint32_t ip, uint8_t out_mac[6], uint32_t timeout_ms);
size_t arp_list(arp_entry_t *out, size_t max_entries);

/* IPv4 & ICMP */
int  ipv4_send(uint32_t dst_ip, uint8_t proto, const void *payload, size_t len);
void ipv4_rx(const uint8_t *pkt, size_t len);
void icmp_rx(uint32_t src_ip, uint32_t dst_ip, const uint8_t *pkt, size_t len);
bool icmp_ping(uint32_t dst_ip, uint16_t seq, uint32_t timeout_ms, uint32_t *out_rtt_ms);

/* UDP & TCP RX handlers */
void udp_rx(uint32_t src_ip, uint32_t dst_ip, const uint8_t *pkt, size_t len);
void tcp_rx(uint32_t src_ip, uint32_t dst_ip, const uint8_t *pkt, size_t len);

/* Socket & BSD interface */
#define AF_INET     2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

void    socket_init(void);
int     sock_create(int type);
void    sock_ref(int sock_id);
void    sock_close(int sock_id);
int     sock_bind(int sock_id, uint32_t ip, uint16_t port);
int     sock_listen(int sock_id);
int     sock_accept(int sock_id, uint32_t *out_ip, uint16_t *out_port, uint32_t timeout_ms);
int     sock_connect(int sock_id, uint32_t ip, uint16_t port);
int64_t sock_sendto(int sock_id, const void *buf, size_t len, uint32_t ip, uint16_t port);
int64_t sock_recvfrom(int sock_id, void *buf, size_t len, uint32_t *out_ip, uint16_t *out_port, uint32_t timeout_ms);

#endif
