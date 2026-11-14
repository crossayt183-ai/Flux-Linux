#ifndef FLUX_SOCK_INTERNAL_H
#define FLUX_SOCK_INTERNAL_H

#include "net.h"
#include "spinlock.h"

#define MAX_SOCKETS  32
#define UDP_RX_QUEUE 8
#define UDP_MAX_DATA 1472
#define TCP_RX_CAP   4096
#define TCP_ACCEPT_Q 4

typedef struct {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t len;
    uint8_t  data[UDP_MAX_DATA];
} udp_dgram_t;

typedef enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECV,
    TCP_ESTABLISHED,
    TCP_CLOSE_WAIT
} tcp_state_t;

typedef struct {
    bool        in_use;
    int         type;       /* SOCK_STREAM or SOCK_DGRAM */
    int         refcount;
    uint32_t    local_ip;
    uint16_t    local_port;
    uint32_t    remote_ip;
    uint16_t    remote_port;

    /* UDP state */
    udp_dgram_t *udp_q;
    uint8_t     udp_head;
    uint8_t     udp_tail;
    uint8_t     udp_count;

    /* TCP state */
    tcp_state_t tcp_state;
    uint32_t    snd_una;
    uint32_t    snd_nxt;
    uint32_t    rcv_nxt;
    bool        peer_closed;
    int         parent_id;
    int         accept_q[TCP_ACCEPT_Q];
    uint8_t     accept_head;
    uint8_t     accept_tail;
    uint8_t     accept_count;
    uint8_t    *tcp_rx_buf;
    size_t      tcp_rx_head;
    size_t      tcp_rx_len;
} net_socket_t;

extern net_socket_t g_sockets[MAX_SOCKETS];
extern spinlock_t   g_sock_lock;

uint16_t sock_alloc_ephemeral_port(void);
int      sock_alloc_slot_locked(int type);

int      tcp_sock_listen(int sock_id);
int      tcp_sock_accept(int sock_id, uint32_t *out_ip, uint16_t *out_port, uint32_t timeout_ms);
int      tcp_sock_connect(int sock_id, uint32_t ip, uint16_t port);
int64_t  tcp_sock_send(int sock_id, const void *buf, size_t len);
int64_t  tcp_sock_recv(int sock_id, void *buf, size_t len, uint32_t timeout_ms);
void     tcp_prepare_close_locked(int sock_id, bool *send_fin,
                                  uint32_t *rip, uint16_t *rport, uint16_t *lport,
                                  uint32_t *seq, uint32_t *ack,
                                  int close_children[TCP_ACCEPT_Q], int *num_children);
void     tcp_send_fin_ack(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                          uint32_t seq, uint32_t ack);

int64_t  udp_sock_send(int sock_id, const void *buf, size_t len, uint32_t ip, uint16_t port);
int64_t  udp_sock_recv(int sock_id, void *buf, size_t len, uint32_t *out_ip,
                       uint16_t *out_port, uint32_t timeout_ms);

#endif
