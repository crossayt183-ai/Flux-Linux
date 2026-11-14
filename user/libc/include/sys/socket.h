#ifndef FLUX_USER_SYS_SOCKET_H
#define FLUX_USER_SYS_SOCKET_H

#include "flux/types.h"

#define AF_INET     2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

typedef struct {
    uint8_t  mac[6];
    uint16_t reserved;
    uint32_t ip;
    uint32_t mask;
    uint32_t gw;
    uint64_t rx_packets;
    uint64_t tx_packets;
} netinfo_t;

static inline uint32_t ip4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
}

int     socket(int domain, int type, int proto);
int     bind(int fd, uint32_t ip, uint16_t port);
int     listen(int fd);
int     accept(int fd, uint32_t *out_ip, uint16_t *out_port, uint32_t timeout_ms);
int     connect(int fd, uint32_t ip, uint16_t port);
ssize_t sendto(int fd, const void *buf, size_t len, uint32_t ip, uint16_t port);
ssize_t recvfrom(int fd, void *buf, size_t len, uint32_t *out_ip, uint16_t *out_port, uint32_t timeout_ms);
int     icmp_ping(uint32_t ip, uint16_t seq, uint32_t timeout_ms, uint32_t *out_rtt_ms);
int     netinfo(netinfo_t *out);
bool    parse_ipv4(const char *s, uint32_t *out_ip);

#endif
