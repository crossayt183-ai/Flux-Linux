#include "net.h"
#include "firewall.h"
#include "timer.h"
#include "spinlock.h"
#include "string.h"

typedef struct PACKED {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t csum;
    uint32_t src_ip;
    uint32_t dst_ip;
} ipv4_hdr_t;

typedef struct PACKED {
    uint8_t  type;
    uint8_t  code;
    uint16_t csum;
    uint16_t id;
    uint16_t seq;
} icmp_hdr_t;

static uint16_t ip_next_id = 1;
static volatile bool     ping_got = false;
static volatile uint16_t ping_expect_seq = 0;
static volatile uint32_t ping_from_ip = 0;
static spinlock_t        ping_lock = SPINLOCK_INIT;

int ipv4_send(uint32_t dst_ip, uint8_t proto, const void *payload, size_t len) {
    if (!payload || len > 1460) return -1;

    net_info_t ni;
    net_get_info(&ni);

    uint8_t pkt[1500];
    ipv4_hdr_t *hdr = (ipv4_hdr_t *)pkt;
    memset(hdr, 0, sizeof(*hdr));
    hdr->ver_ihl = 0x45;
    hdr->tot_len = htons((uint16_t)(sizeof(ipv4_hdr_t) + len));
    hdr->id      = htons(__atomic_fetch_add(&ip_next_id, 1, __ATOMIC_SEQ_CST));
    hdr->ttl     = 64;
    hdr->proto   = proto;
    hdr->src_ip  = htonl(net_is_local_ip(dst_ip) ? dst_ip : ni.ip);
    hdr->dst_ip  = htonl(dst_ip);
    hdr->csum    = htons(net_checksum(hdr, sizeof(*hdr)));
    memcpy(pkt + sizeof(ipv4_hdr_t), payload, len);

    /* Loopback fast path delivers via normal IPv4 RX handlers */
    if (net_is_local_ip(dst_ip)) {
        ipv4_rx(pkt, sizeof(ipv4_hdr_t) + len);
        return 0;
    }

    uint32_t next_hop = ((dst_ip & ni.mask) == (ni.ip & ni.mask)) ? dst_ip : ni.gw;
    uint8_t dmac[6];
    if (!arp_resolve(next_hop, dmac, 1000)) return -1;
    return eth_send(dmac, ETH_TYPE_IPV4, pkt, sizeof(ipv4_hdr_t) + len);
}

void ipv4_rx(const uint8_t *pkt, size_t len) {
    if (len < sizeof(ipv4_hdr_t)) return;
    const ipv4_hdr_t *hdr = (const ipv4_hdr_t *)pkt;
    uint8_t ver = hdr->ver_ihl >> 4;
    size_t ihl  = (size_t)(hdr->ver_ihl & 0x0FU) * 4U;
    if (ver != 4 || ihl < sizeof(ipv4_hdr_t) || ihl > len) return;
    if (net_checksum(hdr, ihl) != 0) return;

    size_t tot = ntohs(hdr->tot_len);
    if (tot < ihl || tot > len) return;

    uint32_t src = ntohl(hdr->src_ip);
    uint32_t dst = ntohl(hdr->dst_ip);
    const uint8_t *body = pkt + ihl;
    size_t body_len = tot - ihl;

    uint16_t sport = 0, dport = 0;
    if ((hdr->proto == IP_PROTO_UDP || hdr->proto == IP_PROTO_TCP) && body_len >= 4) {
        sport = (uint16_t)(((uint16_t)body[0] << 8) | body[1]);
        dport = (uint16_t)(((uint16_t)body[2] << 8) | body[3]);
    }
    if (!fw_check_packet(hdr->proto, sport, dport)) {
        return;
    }

    switch (hdr->proto) {
    case IP_PROTO_ICMP: icmp_rx(src, dst, body, body_len); break;
    case IP_PROTO_UDP:  udp_rx(src, dst, body, body_len); break;
    case IP_PROTO_TCP:  tcp_rx(src, dst, body, body_len); break;
    default: break;
    }
}

void icmp_rx(uint32_t src_ip, uint32_t dst_ip UNUSED, const uint8_t *pkt, size_t len) {
    if (len < sizeof(icmp_hdr_t)) return;
    const icmp_hdr_t *ic = (const icmp_hdr_t *)pkt;

    if (ic->type == 8 && ic->code == 0 && len <= 256) {
        uint8_t rep[256];
        memcpy(rep, pkt, len);
        icmp_hdr_t *rh = (icmp_hdr_t *)rep;
        rh->type = 0;
        rh->csum = 0;
        rh->csum = htons(net_checksum(rep, len));
        ipv4_send(src_ip, IP_PROTO_ICMP, rep, len);
    } else if (ic->type == 0 && ntohs(ic->id) == 0x4658U) {
        spin_lock_irqsave(&ping_lock);
        if (ntohs(ic->seq) == ping_expect_seq) {
            ping_from_ip = src_ip;
            ping_got     = true;
        }
        spin_unlock_irqrestore(&ping_lock);
    }
}

bool icmp_ping(uint32_t dst_ip, uint16_t seq, uint32_t timeout_ms, uint32_t *out_rtt_ms) {
    uint8_t pkt[sizeof(icmp_hdr_t) + 32];
    icmp_hdr_t *ic = (icmp_hdr_t *)pkt;
    ic->type = 8;
    ic->code = 0;
    ic->csum = 0;
    ic->id   = htons(0x4658U);
    ic->seq  = htons(seq);
    for (int i = 0; i < 32; i++) pkt[sizeof(icmp_hdr_t) + i] = (uint8_t)('a' + (i % 26));
    ic->csum = htons(net_checksum(pkt, sizeof(pkt)));

    spin_lock_irqsave(&ping_lock);
    ping_expect_seq = seq;
    ping_got = false;
    spin_unlock_irqrestore(&ping_lock);

    uint64_t t0 = timer_uptime_ms();
    if (ipv4_send(dst_ip, IP_PROTO_ICMP, pkt, sizeof(pkt)) != 0) return false;

    while ((timer_uptime_ms() - t0) < timeout_ms) {
        net_poll();
        if (ping_got) {
            if (out_rtt_ms) {
                uint64_t dt = timer_uptime_ms() - t0;
                *out_rtt_ms = (uint32_t)dt;
            }
            return true;
        }
        timer_sleep_ms(5);
    }
    return false;
}
