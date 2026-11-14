#include "stdio.h"
#include "unistd.h"
#include "sys/socket.h"

int main(int argc, char **argv) {
    uint32_t dst = ip4(10, 0, 2, 2);
    const char *target = "10.0.2.2";
    if (argc >= 2) {
        if (!parse_ipv4(argv[1], &dst)) {
            printf("ping: invalid IPv4 address '%s'\n", argv[1]);
            return 1;
        }
        target = argv[1];
    }

    int ok = 0;
    for (uint16_t seq = 1; seq <= 3; seq++) {
        uint32_t rtt = 0;
        if (icmp_ping(dst, seq, 1000, &rtt) == 0) {
            printf("64 bytes from %s: icmp_seq=%u time=%u ms\n", target, seq, rtt);
            ok++;
        } else {
            printf("Request timeout for icmp_seq %u\n", seq);
        }
        if (seq < 3) sleep_ms(80);
    }
    printf("--- %s ping: 3 transmitted, %d received ---\n", target, ok);
    return ok > 0 ? 0 : 1;
}
