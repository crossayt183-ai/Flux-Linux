#include "net.h"
#include "e1000.h"
#include "timer.h"
#include "spinlock.h"
#include "string.h"

#define ARP_CACHE_SIZE 16
#define ARP_OP_REQ     1U
#define ARP_OP_REPLY   2U

typedef struct PACKED {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[6];
    uint32_t spa;
    uint8_t  tha[6];
    uint32_t tpa;
} arp_hdr_t;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static uint32_t    arp_next_evict = 0;
static spinlock_t  arp_lock = SPINLOCK_INIT;

static void arp_put(uint32_t ip, const uint8_t mac[6]) {
    spin_lock_irqsave(&arp_lock);
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, 6);
            spin_unlock_irqrestore(&arp_lock);
            return;
        }
    }
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = true;
            spin_unlock_irqrestore(&arp_lock);
            return;
        }
    }
    uint32_t s = arp_next_evict++ % ARP_CACHE_SIZE;
    arp_cache[s].ip = ip;
    memcpy(arp_cache[s].mac, mac, 6);
    arp_cache[s].valid = true;
    spin_unlock_irqrestore(&arp_lock);
}

static bool arp_lookup(uint32_t ip, uint8_t out_mac[6]) {
    spin_lock_irqsave(&arp_lock);
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(out_mac, arp_cache[i].mac, 6);
            spin_unlock_irqrestore(&arp_lock);
            return true;
        }
    }
    spin_unlock_irqrestore(&arp_lock);
    return false;
}

void arp_rx(const uint8_t *pkt, size_t len) {
    if (len < sizeof(arp_hdr_t)) return;
    const arp_hdr_t *a = (const arp_hdr_t *)pkt;
    if (ntohs(a->htype) != 1 || ntohs(a->ptype) != ETH_TYPE_IPV4) return;

    uint32_t spa = ntohl(a->spa);
    uint32_t tpa = ntohl(a->tpa);
    arp_put(spa, a->sha);

    net_info_t ni;
    net_get_info(&ni);
    if (ntohs(a->oper) == ARP_OP_REQ && tpa == ni.ip) {
        arp_hdr_t rep;
        rep.htype = htons(1);
        rep.ptype = htons(ETH_TYPE_IPV4);
        rep.hlen  = 6;
        rep.plen  = 4;
        rep.oper  = htons(ARP_OP_REPLY);
        memcpy(rep.sha, ni.mac, 6);
        rep.spa = htonl(ni.ip);
        memcpy(rep.tha, a->sha, 6);
        rep.tpa = a->spa;
        eth_send(a->sha, ETH_TYPE_ARP, &rep, sizeof(rep));
    }
}

bool arp_resolve(uint32_t ip, uint8_t out_mac[6], uint32_t timeout_ms) {
    if (arp_lookup(ip, out_mac)) return true;
    if (!e1000_is_ready()) return false;

    net_info_t ni;
    net_get_info(&ni);

    arp_hdr_t req;
    req.htype = htons(1);
    req.ptype = htons(ETH_TYPE_IPV4);
    req.hlen  = 6;
    req.plen  = 4;
    req.oper  = htons(ARP_OP_REQ);
    memcpy(req.sha, ni.mac, 6);
    req.spa = htonl(ni.ip);
    memset(req.tha, 0, 6);
    req.tpa = htonl(ip);

    static const uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint64_t start = timer_uptime_ms();
    uint64_t last_tx = 0;

    while ((timer_uptime_ms() - start) < timeout_ms) {
        uint64_t now = timer_uptime_ms();
        if (last_tx == 0 || (now - last_tx) >= 200) {
            eth_send(bcast, ETH_TYPE_ARP, &req, sizeof(req));
            last_tx = now ? now : 1;
        }
        net_poll();
        if (arp_lookup(ip, out_mac)) return true;
        timer_sleep_ms(5);
    }
    return false;
}

size_t arp_list(arp_entry_t *out, size_t max_entries) {
    spin_lock_irqsave(&arp_lock);
    size_t n = 0;
    for (int i = 0; i < ARP_CACHE_SIZE && n < max_entries; i++) {
        if (arp_cache[i].valid) out[n++] = arp_cache[i];
    }
    spin_unlock_irqrestore(&arp_lock);
    return n;
}
