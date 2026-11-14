#include "sock_internal.h"
#include "heap.h"
#include "string.h"

net_socket_t g_sockets[MAX_SOCKETS];
spinlock_t   g_sock_lock = SPINLOCK_INIT;
static uint16_t next_ephemeral = 49152;

void socket_init(void) {
    spin_lock_irqsave(&g_sock_lock);
    memset(g_sockets, 0, sizeof(g_sockets));
    next_ephemeral = 49152;
    spin_unlock_irqrestore(&g_sock_lock);
}

static bool port_in_use_locked(uint16_t port, int type) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_sockets[i].in_use && g_sockets[i].type == type &&
            g_sockets[i].local_port == port) {
            return true;
        }
    }
    return false;
}

uint16_t sock_alloc_ephemeral_port(void) {
    for (int tries = 0; tries < 16000; tries++) {
        uint16_t p = next_ephemeral++;
        if (next_ephemeral < 49152) next_ephemeral = 49152;
        bool used = false;
        for (int i = 0; i < MAX_SOCKETS; i++) {
            if (g_sockets[i].in_use && g_sockets[i].local_port == p) {
                used = true;
                break;
            }
        }
        if (!used) return p;
    }
    return 50000;
}

int sock_alloc_slot_locked(int type) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!g_sockets[i].in_use) {
            memset(&g_sockets[i], 0, sizeof(g_sockets[i]));
            g_sockets[i].in_use    = true;
            g_sockets[i].type      = type;
            g_sockets[i].refcount  = 1;
            g_sockets[i].parent_id = -1;
            return i;
        }
    }
    return -1;
}

int sock_create(int type) {
    if (type != SOCK_DGRAM && type != SOCK_STREAM) return -1;

    void *buf = NULL;
    if (type == SOCK_DGRAM) {
        buf = kcalloc(UDP_RX_QUEUE, sizeof(udp_dgram_t));
    } else {
        buf = kmalloc(TCP_RX_CAP);
    }
    if (!buf) return -1;

    spin_lock_irqsave(&g_sock_lock);
    int id = sock_alloc_slot_locked(type);
    if (id < 0) {
        spin_unlock_irqrestore(&g_sock_lock);
        kfree(buf);
        return -1;
    }
    if (type == SOCK_DGRAM) {
        g_sockets[id].udp_q = (udp_dgram_t *)buf;
    } else {
        g_sockets[id].tcp_rx_buf = (uint8_t *)buf;
    }
    spin_unlock_irqrestore(&g_sock_lock);
    return id;
}

void sock_ref(int sock_id) {
    if (sock_id < 0 || sock_id >= MAX_SOCKETS) return;
    spin_lock_irqsave(&g_sock_lock);
    if (g_sockets[sock_id].in_use) {
        g_sockets[sock_id].refcount++;
    }
    spin_unlock_irqrestore(&g_sock_lock);
}

void sock_close(int sock_id) {
    if (sock_id < 0 || sock_id >= MAX_SOCKETS) return;

    bool send_fin = false;
    uint32_t rip = 0, seq = 0, ack = 0;
    uint16_t rport = 0, lport = 0;
    void *free_ptr = NULL;
    int close_children[TCP_ACCEPT_Q];
    int num_children = 0;

    spin_lock_irqsave(&g_sock_lock);
    net_socket_t *s = &g_sockets[sock_id];
    if (!s->in_use) {
        spin_unlock_irqrestore(&g_sock_lock);
        return;
    }
    if (--s->refcount > 0) {
        spin_unlock_irqrestore(&g_sock_lock);
        return;
    }

    if (s->type == SOCK_STREAM) {
        tcp_prepare_close_locked(sock_id, &send_fin, &rip, &rport, &lport,
                                 &seq, &ack, close_children, &num_children);
        free_ptr = s->tcp_rx_buf;
    } else {
        free_ptr = s->udp_q;
    }
    memset(s, 0, sizeof(*s));
    spin_unlock_irqrestore(&g_sock_lock);

    if (free_ptr) kfree(free_ptr);
    for (int i = 0; i < num_children; i++) {
        sock_close(close_children[i]);
    }
    if (send_fin) {
        tcp_send_fin_ack(rip, lport, rport, seq, ack);
    }
}

int sock_bind(int sock_id, uint32_t ip, uint16_t port) {
    if (sock_id < 0 || sock_id >= MAX_SOCKETS || port == 0) return -1;
    spin_lock_irqsave(&g_sock_lock);
    net_socket_t *s = &g_sockets[sock_id];
    if (!s->in_use || port_in_use_locked(port, s->type)) {
        spin_unlock_irqrestore(&g_sock_lock);
        return -1;
    }
    s->local_ip   = ip;
    s->local_port = port;
    spin_unlock_irqrestore(&g_sock_lock);
    return 0;
}

int sock_listen(int sock_id) {
    return tcp_sock_listen(sock_id);
}

int sock_accept(int sock_id, uint32_t *out_ip, uint16_t *out_port, uint32_t timeout_ms) {
    return tcp_sock_accept(sock_id, out_ip, out_port, timeout_ms);
}

int sock_connect(int sock_id, uint32_t ip, uint16_t port) {
    if (sock_id < 0 || sock_id >= MAX_SOCKETS || port == 0) return -1;
    spin_lock_irqsave(&g_sock_lock);
    if (!g_sockets[sock_id].in_use) {
        spin_unlock_irqrestore(&g_sock_lock);
        return -1;
    }
    int type = g_sockets[sock_id].type;
    if (type == SOCK_DGRAM) {
        g_sockets[sock_id].remote_ip   = ip;
        g_sockets[sock_id].remote_port = port;
        if (g_sockets[sock_id].local_port == 0) {
            g_sockets[sock_id].local_port = sock_alloc_ephemeral_port();
        }
        spin_unlock_irqrestore(&g_sock_lock);
        return 0;
    }
    spin_unlock_irqrestore(&g_sock_lock);
    return tcp_sock_connect(sock_id, ip, port);
}

int64_t sock_sendto(int sock_id, const void *buf, size_t len, uint32_t ip, uint16_t port) {
    if (sock_id < 0 || sock_id >= MAX_SOCKETS || !buf) return -1;
    spin_lock_irqsave(&g_sock_lock);
    if (!g_sockets[sock_id].in_use) {
        spin_unlock_irqrestore(&g_sock_lock);
        return -1;
    }
    int type = g_sockets[sock_id].type;
    spin_unlock_irqrestore(&g_sock_lock);

    if (type == SOCK_DGRAM) return udp_sock_send(sock_id, buf, len, ip, port);
    return tcp_sock_send(sock_id, buf, len);
}

int64_t sock_recvfrom(int sock_id, void *buf, size_t len, uint32_t *out_ip,
                      uint16_t *out_port, uint32_t timeout_ms) {
    if (sock_id < 0 || sock_id >= MAX_SOCKETS || !buf) return -1;
    spin_lock_irqsave(&g_sock_lock);
    if (!g_sockets[sock_id].in_use) {
        spin_unlock_irqrestore(&g_sock_lock);
        return -1;
    }
    int type = g_sockets[sock_id].type;
    if (type == SOCK_STREAM) {
        if (out_ip)   *out_ip   = g_sockets[sock_id].remote_ip;
        if (out_port) *out_port = g_sockets[sock_id].remote_port;
    }
    spin_unlock_irqrestore(&g_sock_lock);

    if (type == SOCK_DGRAM) return udp_sock_recv(sock_id, buf, len, out_ip, out_port, timeout_ms);
    return tcp_sock_recv(sock_id, buf, len, timeout_ms);
}
