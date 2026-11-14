#include "sock_internal.h"
#include "timer.h"
#include "task.h"
#include "string.h"

typedef struct PACKED {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t csum;
} udp_hdr_t;

void udp_rx(uint32_t src_ip, uint32_t dst_ip, const uint8_t *pkt, size_t len) {
    if (!pkt || len < sizeof(udp_hdr_t)) return;
    const udp_hdr_t *uh = (const udp_hdr_t *)pkt;
    size_t ulen = ntohs(uh->length);
    if (ulen < sizeof(udp_hdr_t) || ulen > len) return;

    if (uh->csum != 0) {
        if (net_pseudo_checksum(src_ip, dst_ip, IP_PROTO_UDP, pkt, (uint16_t)ulen) != 0xFFFFU) {
            return;
        }
    }

    uint16_t sport = ntohs(uh->src_port);
    uint16_t dport = ntohs(uh->dst_port);
    size_t dlen = ulen - sizeof(udp_hdr_t);
    if (dlen > UDP_MAX_DATA) dlen = UDP_MAX_DATA;

    spin_lock_irqsave(&g_sock_lock);
    for (int i = 0; i < MAX_SOCKETS; i++) {
        net_socket_t *s = &g_sockets[i];
        if (!s->in_use || s->type != SOCK_DGRAM || s->local_port != dport || !s->udp_q) {
            continue;
        }
        if (s->udp_count < UDP_RX_QUEUE) {
            udp_dgram_t *dg = &s->udp_q[s->udp_tail];
            dg->src_ip   = src_ip;
            dg->src_port = sport;
            dg->len      = (uint16_t)dlen;
            if (dlen > 0) memcpy(dg->data, pkt + sizeof(udp_hdr_t), dlen);
            s->udp_tail = (uint8_t)((s->udp_tail + 1U) % UDP_RX_QUEUE);
            s->udp_count++;
        }
        break;
    }
    spin_unlock_irqrestore(&g_sock_lock);
}

int64_t udp_sock_send(int sock_id, const void *buf, size_t len, uint32_t ip, uint16_t port) {
    if (len > UDP_MAX_DATA) return -1;

    spin_lock_irqsave(&g_sock_lock);
    net_socket_t *s = &g_sockets[sock_id];
    if (!s->in_use || s->type != SOCK_DGRAM) {
        spin_unlock_irqrestore(&g_sock_lock);
        return -1;
    }
    if (s->local_port == 0) s->local_port = sock_alloc_ephemeral_port();
    uint16_t lport = s->local_port;
    uint32_t dst_ip   = ip   ? ip   : s->remote_ip;
    uint16_t dst_port = port ? port : s->remote_port;
    spin_unlock_irqrestore(&g_sock_lock);

    if (dst_ip == 0 || dst_port == 0) return -1;

    net_info_t ni;
    net_get_info(&ni);
    uint32_t src_ip = net_is_local_ip(dst_ip) ? dst_ip : ni.ip;

    uint8_t pkt[sizeof(udp_hdr_t) + UDP_MAX_DATA];
    udp_hdr_t *uh = (udp_hdr_t *)pkt;
    uint16_t tot = (uint16_t)(sizeof(udp_hdr_t) + len);
    uh->src_port = htons(lport);
    uh->dst_port = htons(dst_port);
    uh->length   = htons(tot);
    uh->csum     = 0;
    if (len > 0) memcpy(pkt + sizeof(udp_hdr_t), buf, len);
    uh->csum = htons(net_pseudo_checksum(src_ip, dst_ip, IP_PROTO_UDP, pkt, tot));

    if (ipv4_send(dst_ip, IP_PROTO_UDP, pkt, tot) != 0) return -1;
    return (int64_t)len;
}

int64_t udp_sock_recv(int sock_id, void *buf, size_t len, uint32_t *out_ip,
                      uint16_t *out_port, uint32_t timeout_ms) {
    if (timeout_ms == 0) timeout_ms = 5000;
    uint64_t t0 = timer_uptime_ms();

    for (;;) {
        net_poll();

        spin_lock_irqsave(&g_sock_lock);
        net_socket_t *s = &g_sockets[sock_id];
        if (!s->in_use || s->type != SOCK_DGRAM || !s->udp_q) {
            spin_unlock_irqrestore(&g_sock_lock);
            return -1;
        }
        if (s->udp_count > 0) {
            udp_dgram_t *dg = &s->udp_q[s->udp_head];
            size_t n = dg->len < len ? dg->len : len;
            if (n > 0) memcpy(buf, dg->data, n);
            if (out_ip)   *out_ip   = dg->src_ip;
            if (out_port) *out_port = dg->src_port;
            s->udp_head = (uint8_t)((s->udp_head + 1U) % UDP_RX_QUEUE);
            s->udp_count--;
            spin_unlock_irqrestore(&g_sock_lock);
            return (int64_t)n;
        }
        spin_unlock_irqrestore(&g_sock_lock);

        if ((timer_uptime_ms() - t0) >= timeout_ms) return -1;
        task_sleep_ms(2);
    }
}
