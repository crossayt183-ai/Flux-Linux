#include "sock_internal.h"
#include "heap.h"
#include "timer.h"
#include "task.h"
#include "string.h"

#define TCP_FIN 0x01U
#define TCP_SYN 0x02U
#define TCP_RST 0x04U
#define TCP_PSH 0x08U
#define TCP_ACK 0x10U

#define TCP_MSS 1400U

typedef struct PACKED {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_off;
    uint8_t  flags;
    uint16_t window;
    uint16_t csum;
    uint16_t urg_ptr;
} tcp_hdr_t;

static uint32_t next_isn = 0x10420000U;

static int send_segment(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                        uint32_t seq, uint32_t ack, uint8_t flags,
                        const void *payload, size_t len) {
    if (len > TCP_MSS) return -1;
    net_info_t ni;
    net_get_info(&ni);
    uint32_t src_ip = net_is_local_ip(dst_ip) ? dst_ip : ni.ip;

    uint8_t pkt[sizeof(tcp_hdr_t) + TCP_MSS];
    tcp_hdr_t *th = (tcp_hdr_t *)pkt;
    th->src_port = htons(src_port);
    th->dst_port = htons(dst_port);
    th->seq      = htonl(seq);
    th->ack      = htonl(ack);
    th->data_off = (uint8_t)(5U << 4);
    th->flags    = flags;
    th->window   = htons(TCP_RX_CAP);
    th->csum     = 0;
    th->urg_ptr  = 0;
    if (len > 0 && payload) memcpy(pkt + sizeof(tcp_hdr_t), payload, len);

    uint16_t tot = (uint16_t)(sizeof(tcp_hdr_t) + len);
    th->csum = htons(net_pseudo_checksum(src_ip, dst_ip, IP_PROTO_TCP, pkt, tot));
    return ipv4_send(dst_ip, IP_PROTO_TCP, pkt, tot);
}

void tcp_send_fin_ack(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                      uint32_t seq, uint32_t ack) {
    send_segment(dst_ip, src_port, dst_port, seq, ack, TCP_FIN | TCP_ACK, NULL, 0);
}

static int find_conn_locked(uint32_t src_ip, uint16_t sport, uint16_t dport) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        net_socket_t *s = &g_sockets[i];
        if (!s->in_use || s->type != SOCK_STREAM || s->tcp_state == TCP_LISTEN) continue;
        if (s->local_port == dport && s->remote_port == sport && s->remote_ip == src_ip) {
            return i;
        }
    }
    return -1;
}

static int find_listen_locked(uint16_t dport) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        net_socket_t *s = &g_sockets[i];
        if (s->in_use && s->type == SOCK_STREAM &&
            s->tcp_state == TCP_LISTEN && s->local_port == dport) {
            return i;
        }
    }
    return -1;
}

static void rx_buf_push(net_socket_t *s, const uint8_t *data, size_t len) {
    if (!s->tcp_rx_buf || len == 0) return;
    size_t tail = (s->tcp_rx_head + s->tcp_rx_len) % TCP_RX_CAP;
    for (size_t i = 0; i < len && s->tcp_rx_len < TCP_RX_CAP; i++) {
        s->tcp_rx_buf[tail] = data[i];
        tail = (tail + 1U) % TCP_RX_CAP;
        s->tcp_rx_len++;
    }
}

void tcp_rx(uint32_t src_ip, uint32_t dst_ip, const uint8_t *pkt, size_t len) {
    if (!pkt || len < sizeof(tcp_hdr_t)) return;
    const tcp_hdr_t *th = (const tcp_hdr_t *)pkt;
    size_t hdr_len = (size_t)(th->data_off >> 4) * 4U;
    if (hdr_len < sizeof(tcp_hdr_t) || hdr_len > len) return;
    if (net_pseudo_checksum(src_ip, dst_ip, IP_PROTO_TCP, pkt, (uint16_t)len) != 0xFFFFU) {
        return;
    }

    uint16_t sport = ntohs(th->src_port);
    uint16_t dport = ntohs(th->dst_port);
    uint32_t seq   = ntohl(th->seq);
    uint32_t ack   = ntohl(th->ack);
    uint8_t  flags = th->flags;
    const uint8_t *payload = pkt + hdr_len;
    size_t data_len = len - hdr_len;

    bool send_reply = false;
    uint8_t  reply_flags = 0;
    uint32_t reply_seq = 0, reply_ack = 0;

    spin_lock_irqsave(&g_sock_lock);
    int idx = find_conn_locked(src_ip, sport, dport);
    if (idx < 0) {
        int lid = find_listen_locked(dport);
        if (lid >= 0 && (flags & TCP_SYN) && !(flags & (TCP_ACK | TCP_RST))) {
            net_socket_t *ls = &g_sockets[lid];
            if (ls->accept_count < TCP_ACCEPT_Q) {
                uint8_t *rbuf = (uint8_t *)kmalloc(TCP_RX_CAP);
                int cid = rbuf ? sock_alloc_slot_locked(SOCK_STREAM) : -1;
                if (cid >= 0) {
                    uint32_t isn = __atomic_fetch_add(&next_isn, 64000U, __ATOMIC_SEQ_CST);
                    net_socket_t *cs = &g_sockets[cid];
                    cs->local_ip    = dst_ip;
                    cs->local_port  = dport;
                    cs->remote_ip   = src_ip;
                    cs->remote_port = sport;
                    cs->tcp_state   = TCP_SYN_RECV;
                    cs->parent_id   = lid;
                    cs->tcp_rx_buf  = rbuf;
                    cs->rcv_nxt     = seq + 1U;
                    cs->snd_una     = isn;
                    cs->snd_nxt     = isn + 1U;

                    send_reply  = true;
                    reply_flags = TCP_SYN | TCP_ACK;
                    reply_seq   = isn;
                    reply_ack   = cs->rcv_nxt;
                } else if (rbuf) {
                    kfree(rbuf);
                }
            }
        }
        spin_unlock_irqrestore(&g_sock_lock);
        if (send_reply) {
            send_segment(src_ip, dport, sport, reply_seq, reply_ack, reply_flags, NULL, 0);
        }
        return;
    }

    net_socket_t *s = &g_sockets[idx];
    if (flags & TCP_RST) {
        s->peer_closed = true;
        s->tcp_state   = TCP_CLOSED;
        spin_unlock_irqrestore(&g_sock_lock);
        return;
    }

    if (s->tcp_state == TCP_SYN_SENT) {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) && ack == s->snd_nxt) {
            s->snd_una   = ack;
            s->rcv_nxt   = seq + 1U;
            s->tcp_state = TCP_ESTABLISHED;
            send_reply   = true;
            reply_flags  = TCP_ACK;
            reply_seq    = s->snd_nxt;
            reply_ack    = s->rcv_nxt;
        }
    } else if (s->tcp_state == TCP_SYN_RECV) {
        if ((flags & TCP_ACK) && ack == s->snd_nxt) {
            s->snd_una   = ack;
            s->tcp_state = TCP_ESTABLISHED;
            if (s->parent_id >= 0 && s->parent_id < MAX_SOCKETS) {
                net_socket_t *ls = &g_sockets[s->parent_id];
                if (ls->in_use && ls->tcp_state == TCP_LISTEN && ls->accept_count < TCP_ACCEPT_Q) {
                    ls->accept_q[ls->accept_tail] = idx;
                    ls->accept_tail = (uint8_t)((ls->accept_tail + 1U) % TCP_ACCEPT_Q);
                    ls->accept_count++;
                }
            }
        }
    } else if (s->tcp_state == TCP_ESTABLISHED || s->tcp_state == TCP_CLOSE_WAIT) {
        if ((flags & TCP_ACK) && (int32_t)(ack - s->snd_una) > 0 && (int32_t)(s->snd_nxt - ack) >= 0) {
            s->snd_una = ack;
        }
        if (data_len > 0 && seq == s->rcv_nxt) {
            size_t avail = TCP_RX_CAP - s->tcp_rx_len;
            size_t take  = data_len < avail ? data_len : avail;
            if (take > 0) {
                rx_buf_push(s, payload, take);
                s->rcv_nxt += (uint32_t)take;
                seq        += (uint32_t)take;
                send_reply  = true;
                reply_flags = TCP_ACK;
                reply_seq   = s->snd_nxt;
                reply_ack   = s->rcv_nxt;
            }
        }
        if ((flags & TCP_FIN) && !s->peer_closed && seq == s->rcv_nxt) {
            s->rcv_nxt++;
            s->peer_closed = true;
            s->tcp_state   = TCP_CLOSE_WAIT;
            send_reply     = true;
            reply_flags    = TCP_ACK;
            reply_seq      = s->snd_nxt;
            reply_ack      = s->rcv_nxt;
        }
    }

    spin_unlock_irqrestore(&g_sock_lock);
    if (send_reply) {
        send_segment(src_ip, dport, sport, reply_seq, reply_ack, reply_flags, NULL, 0);
    }
}

int tcp_sock_listen(int sock_id) {
    if (sock_id < 0 || sock_id >= MAX_SOCKETS) return -1;
    spin_lock_irqsave(&g_sock_lock);
    net_socket_t *s = &g_sockets[sock_id];
    if (!s->in_use || s->type != SOCK_STREAM || s->local_port == 0) {
        spin_unlock_irqrestore(&g_sock_lock);
        return -1;
    }
    s->tcp_state = TCP_LISTEN;
    spin_unlock_irqrestore(&g_sock_lock);
    return 0;
}

int tcp_sock_accept(int sock_id, uint32_t *out_ip, uint16_t *out_port, uint32_t timeout_ms) {
    if (timeout_ms == 0) timeout_ms = 5000;
    uint64_t t0 = timer_uptime_ms();

    for (;;) {
        net_poll();

        spin_lock_irqsave(&g_sock_lock);
        net_socket_t *s = &g_sockets[sock_id];
        if (!s->in_use || s->type != SOCK_STREAM || s->tcp_state != TCP_LISTEN) {
            spin_unlock_irqrestore(&g_sock_lock);
            return -1;
        }
        if (s->accept_count > 0) {
            int cid = s->accept_q[s->accept_head];
            s->accept_head = (uint8_t)((s->accept_head + 1U) % TCP_ACCEPT_Q);
            s->accept_count--;
            if (out_ip)   *out_ip   = g_sockets[cid].remote_ip;
            if (out_port) *out_port = g_sockets[cid].remote_port;
            spin_unlock_irqrestore(&g_sock_lock);
            return cid;
        }
        spin_unlock_irqrestore(&g_sock_lock);

        if ((timer_uptime_ms() - t0) >= timeout_ms) return -1;
        task_sleep_ms(2);
    }
}

int tcp_sock_connect(int sock_id, uint32_t ip, uint16_t port) {
    if (ip == 0 || port == 0) return -1;

    uint32_t isn = __atomic_fetch_add(&next_isn, 64000U, __ATOMIC_SEQ_CST);
    spin_lock_irqsave(&g_sock_lock);
    net_socket_t *s = &g_sockets[sock_id];
    if (!s->in_use || s->type != SOCK_STREAM) {
        spin_unlock_irqrestore(&g_sock_lock);
        return -1;
    }
    if (s->local_port == 0) s->local_port = sock_alloc_ephemeral_port();
    uint16_t lport  = s->local_port;
    s->remote_ip    = ip;
    s->remote_port  = port;
    s->tcp_state    = TCP_SYN_SENT;
    s->snd_una      = isn;
    s->snd_nxt      = isn + 1U;
    s->peer_closed  = false;
    spin_unlock_irqrestore(&g_sock_lock);

    if (send_segment(ip, lport, port, isn, 0, TCP_SYN, NULL, 0) != 0) return -1;

    uint64_t t0 = timer_uptime_ms();
    uint64_t last_tx = t0;
    for (;;) {
        net_poll();

        spin_lock_irqsave(&g_sock_lock);
        bool ok   = g_sockets[sock_id].in_use && g_sockets[sock_id].tcp_state == TCP_ESTABLISHED;
        bool dead = !g_sockets[sock_id].in_use || g_sockets[sock_id].tcp_state == TCP_CLOSED;
        spin_unlock_irqrestore(&g_sock_lock);

        if (ok)   return 0;
        if (dead) return -1;
        uint64_t now = timer_uptime_ms();
        if ((now - t0) >= 4000U) return -1;
        if ((now - last_tx) >= 500U) {
            send_segment(ip, lport, port, isn, 0, TCP_SYN, NULL, 0);
            last_tx = now;
        }
        task_sleep_ms(2);
    }
}

int64_t tcp_sock_send(int sock_id, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;

    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > TCP_MSS) chunk = TCP_MSS;

        spin_lock_irqsave(&g_sock_lock);
        net_socket_t *s = &g_sockets[sock_id];
        if (!s->in_use || s->type != SOCK_STREAM ||
            (s->tcp_state != TCP_ESTABLISHED && s->tcp_state != TCP_CLOSE_WAIT)) {
            spin_unlock_irqrestore(&g_sock_lock);
            return sent > 0 ? (int64_t)sent : -1;
        }
        uint32_t rip   = s->remote_ip;
        uint16_t rport = s->remote_port;
        uint16_t lport = s->local_port;
        uint32_t seq   = s->snd_nxt;
        uint32_t ack   = s->rcv_nxt;
        s->snd_nxt    += (uint32_t)chunk;
        spin_unlock_irqrestore(&g_sock_lock);

        if (send_segment(rip, lport, rport, seq, ack, TCP_PSH | TCP_ACK, p + sent, chunk) != 0) {
            return sent > 0 ? (int64_t)sent : -1;
        }
        sent += chunk;
    }
    return (int64_t)sent;
}

int64_t tcp_sock_recv(int sock_id, void *buf, size_t len, uint32_t timeout_ms) {
    if (timeout_ms == 0) timeout_ms = 5000;
    uint64_t t0 = timer_uptime_ms();

    for (;;) {
        net_poll();

        spin_lock_irqsave(&g_sock_lock);
        net_socket_t *s = &g_sockets[sock_id];
        if (!s->in_use || s->type != SOCK_STREAM || !s->tcp_rx_buf) {
            spin_unlock_irqrestore(&g_sock_lock);
            return -1;
        }
        if (s->tcp_rx_len > 0) {
            size_t take = len < s->tcp_rx_len ? len : s->tcp_rx_len;
            uint8_t *dst = (uint8_t *)buf;
            for (size_t i = 0; i < take; i++) {
                dst[i] = s->tcp_rx_buf[s->tcp_rx_head];
                s->tcp_rx_head = (s->tcp_rx_head + 1U) % TCP_RX_CAP;
                s->tcp_rx_len--;
            }
            spin_unlock_irqrestore(&g_sock_lock);
            return (int64_t)take;
        }
        if (s->peer_closed) {
            spin_unlock_irqrestore(&g_sock_lock);
            return 0;
        }
        spin_unlock_irqrestore(&g_sock_lock);

        if ((timer_uptime_ms() - t0) >= timeout_ms) return -1;
        task_sleep_ms(2);
    }
}

void tcp_prepare_close_locked(int sock_id, bool *send_fin,
                              uint32_t *rip, uint16_t *rport, uint16_t *lport,
                              uint32_t *seq, uint32_t *ack,
                              int close_children[TCP_ACCEPT_Q], int *num_children) {
    net_socket_t *s = &g_sockets[sock_id];
    *send_fin = (s->tcp_state == TCP_ESTABLISHED || s->tcp_state == TCP_CLOSE_WAIT);
    *rip      = s->remote_ip;
    *rport    = s->remote_port;
    *lport    = s->local_port;
    *seq      = s->snd_nxt;
    *ack      = s->rcv_nxt;
    *num_children = 0;

    while (s->accept_count > 0 && *num_children < TCP_ACCEPT_Q) {
        close_children[(*num_children)++] = s->accept_q[s->accept_head];
        s->accept_head = (uint8_t)((s->accept_head + 1U) % TCP_ACCEPT_Q);
        s->accept_count--;
    }
}
