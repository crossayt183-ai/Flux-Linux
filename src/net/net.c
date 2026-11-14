#include "net.h"
#include "firewall.h"
#include "e1000.h"
#include "string.h"
#include "serial.h"

static net_info_t net_cfg;

void net_init(void) {
    memset(&net_cfg, 0, sizeof(net_cfg));
    net_cfg.ip   = ip4(10, 0, 2, 15);
    net_cfg.mask = ip4(255, 255, 255, 0);
    net_cfg.gw   = ip4(10, 0, 2, 2);

    if (e1000_init()) {
        e1000_get_mac(net_cfg.mac);
    }
    socket_init();
    fw_init();
    kprintf("[NET] IPv4 stack configured: 10.0.2.15/24 gw=10.0.2.2\n");
}

void net_get_info(net_info_t *out) {
    if (out) *out = net_cfg;
}

bool net_is_local_ip(uint32_t ip) {
    return ip == net_cfg.ip || (ip >> 24) == 127U;
}

uint16_t net_checksum(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | (uint32_t)p[1];
        p += 2;
        len -= 2;
    }
    if (len == 1) {
        sum += ((uint32_t)p[0] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFU);
}

uint16_t net_pseudo_checksum(uint32_t src_ip, uint32_t dst_ip, uint8_t proto,
                             const void *payload, uint16_t len) {
    uint32_t sum = 0;
    sum += (src_ip >> 16) & 0xFFFFU;
    sum += src_ip & 0xFFFFU;
    sum += (dst_ip >> 16) & 0xFFFFU;
    sum += dst_ip & 0xFFFFU;
    sum += proto;
    sum += len;

    const uint8_t *p = (const uint8_t *)payload;
    size_t rem = len;
    while (rem > 1) {
        sum += ((uint32_t)p[0] << 8) | (uint32_t)p[1];
        p += 2;
        rem -= 2;
    }
    if (rem == 1) {
        sum += ((uint32_t)p[0] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    uint16_t res = (uint16_t)(~sum & 0xFFFFU);
    return res == 0 ? 0xFFFFU : res;
}

static void on_eth_frame(const uint8_t *frame, size_t len) {
    if (len < 14) return;
    net_cfg.rx_packets++;
    uint16_t etype = (uint16_t)(((uint16_t)frame[12] << 8) | frame[13]);
    if (etype == ETH_TYPE_ARP) {
        arp_rx(frame + 14, len - 14);
    } else if (etype == ETH_TYPE_IPV4) {
        ipv4_rx(frame + 14, len - 14);
    }
}

void net_poll(void) {
    e1000_poll_rx(on_eth_frame);
}

int eth_send(const uint8_t dst_mac[6], uint16_t ethertype, const void *payload, size_t len) {
    if (!e1000_is_ready() || len > 1500) return -1;
    uint8_t frame[1520];
    memcpy(frame + 0, dst_mac, 6);
    memcpy(frame + 6, net_cfg.mac, 6);
    frame[12] = (uint8_t)(ethertype >> 8);
    frame[13] = (uint8_t)(ethertype & 0xFFU);
    memcpy(frame + 14, payload, len);

    size_t total = 14 + len;
    if (total < 60) {
        memset(frame + total, 0, 60 - total);
        total = 60;
    }
    int rc = e1000_send(frame, total);
    if (rc == 0) net_cfg.tx_packets++;
    return rc;
}
