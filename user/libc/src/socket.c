#include "sys/socket.h"
#include "flux/syscall.h"

int socket(int domain, int type, int proto) {
    (void)proto;
    return (int)__syscall2(SYS_SOCKET, (uint64_t)domain, (uint64_t)type);
}

int bind(int fd, uint32_t ip, uint16_t port) {
    return (int)__syscall3(SYS_BIND, (uint64_t)fd, (uint64_t)ip, (uint64_t)port);
}

int listen(int fd) {
    return (int)__syscall1(SYS_LISTEN, (uint64_t)fd);
}

int accept(int fd, uint32_t *out_ip, uint16_t *out_port, uint32_t timeout_ms) {
    return (int)__syscall4(SYS_ACCEPT, (uint64_t)fd, (uint64_t)(uintptr_t)out_ip,
                           (uint64_t)(uintptr_t)out_port, (uint64_t)timeout_ms);
}

int connect(int fd, uint32_t ip, uint16_t port) {
    return (int)__syscall3(SYS_CONNECT, (uint64_t)fd, (uint64_t)ip, (uint64_t)port);
}

ssize_t sendto(int fd, const void *buf, size_t len, uint32_t ip, uint16_t port) {
    return (ssize_t)__syscall5(SYS_SENDTO, (uint64_t)fd, (uint64_t)(uintptr_t)buf,
                               (uint64_t)len, (uint64_t)ip, (uint64_t)port);
}

ssize_t recvfrom(int fd, void *buf, size_t len, uint32_t *out_ip,
                 uint16_t *out_port, uint32_t timeout_ms) {
    return (ssize_t)__syscall6(SYS_RECVFROM, (uint64_t)fd, (uint64_t)(uintptr_t)buf,
                               (uint64_t)len, (uint64_t)(uintptr_t)out_ip,
                               (uint64_t)(uintptr_t)out_port, (uint64_t)timeout_ms);
}

int icmp_ping(uint32_t ip, uint16_t seq, uint32_t timeout_ms, uint32_t *out_rtt_ms) {
    return (int)__syscall4(SYS_PING, (uint64_t)ip, (uint64_t)seq,
                           (uint64_t)timeout_ms, (uint64_t)(uintptr_t)out_rtt_ms);
}

int netinfo(netinfo_t *out) {
    return (int)__syscall1(SYS_NETINFO, (uint64_t)(uintptr_t)out);
}

bool parse_ipv4(const char *s, uint32_t *out_ip) {
    if (!s || !*s || !out_ip) return false;
    uint32_t parts[4] = { 0, 0, 0, 0 };
    int idx = 0;
    uint32_t val = 0;
    bool has_digit = false;

    for (const char *p = s; ; p++) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            val = val * 10U + (uint32_t)(c - '0');
            if (val > 255U) return false;
            has_digit = true;
        } else if (c == '.' || c == '\0') {
            if (!has_digit || idx >= 4) return false;
            parts[idx++] = val;
            val = 0;
            has_digit = false;
            if (c == '\0') break;
        } else {
            return false;
        }
    }
    if (idx != 4) return false;
    *out_ip = ip4((uint8_t)parts[0], (uint8_t)parts[1], (uint8_t)parts[2], (uint8_t)parts[3]);
    return true;
}
