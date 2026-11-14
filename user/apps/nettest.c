#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "sys/socket.h"

int main(void) {
    netinfo_t ni;
    if (netinfo(&ni) != 0) {
        printf("[ELF/nettest] FAIL: netinfo\n");
        return 1;
    }

    uint32_t ping_rtt = 0;
    if (icmp_ping(ni.gw, 1, 1500, &ping_rtt) != 0) {
        printf("[ELF/nettest] FAIL: ICMP ping gw\n");
        return 1;
    }

    int ud = socket(AF_INET, SOCK_DGRAM, 0);
    if (ud < 0 || bind(ud, 0, 7001) != 0) {
        printf("[ELF/nettest] FAIL: UDP bind\n");
        return 1;
    }
    const char *umsg = "flux-udp";
    if (sendto(ud, umsg, 8, ip4(127, 0, 0, 1), 7001) != 8) {
        printf("[ELF/nettest] FAIL: UDP sendto\n");
        close(ud);
        return 1;
    }
    char ubuf[32];
    uint32_t rip = 0;
    uint16_t rport = 0;
    ssize_t ulen = recvfrom(ud, ubuf, sizeof(ubuf), &rip, &rport, 1000);
    close(ud);
    if (ulen != 8 || memcmp(ubuf, umsg, 8) != 0) {
        printf("[ELF/nettest] FAIL: UDP recv (%ld)\n", ulen);
        return 1;
    }

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0 || bind(srv, 0, 8080) != 0 || listen(srv) != 0) {
        printf("[ELF/nettest] FAIL: TCP listen\n");
        return 1;
    }

    int64_t pid = fork();
    if (pid < 0) {
        printf("[ELF/nettest] FAIL: fork\n");
        close(srv);
        return 1;
    }

    if (pid == 0) {
        close(srv);
        int cli = socket(AF_INET, SOCK_STREAM, 0);
        if (cli < 0 || connect(cli, ip4(127, 0, 0, 1), 8080) != 0) {
            printf("[ELF/nettest] FAIL: TCP connect\n");
            exit(1);
        }
        const char *req = "SYN-PING";
        write(cli, req, 8);
        char rep[32];
        rep[0] = '\0';
        ssize_t n = read(cli, rep, sizeof(rep) - 1);
        if (n > 0) rep[n] = '\0';
        close(cli);
        if (n != 8 || strcmp(rep, "ACK-PONG") != 0) {
            printf("[ELF/nettest-child] n=%ld rep='%s'\n", n, rep);
            exit(1);
        }
        exit(0);
    }

    uint32_t cip = 0;
    uint16_t cport = 0;
    int cfd = accept(srv, &cip, &cport, 4000);
    if (cfd < 0) {
        printf("[ELF/nettest] FAIL: TCP accept\n");
        close(srv);
        return 1;
    }
    char tbuf[32];
    ssize_t tn = read(cfd, tbuf, sizeof(tbuf) - 1);
    if (tn > 0) tbuf[tn] = '\0';
    write(cfd, "ACK-PONG", 8);
    close(cfd);
    close(srv);

    int32_t st = -1;
    waitpid(pid, &st);
    if (tn != 8 || strcmp(tbuf, "SYN-PING") != 0 || st != 0) {
        printf("[ELF/nettest] FAIL: TCP data/status tn=%ld st=%d\n", tn, st);
        return 1;
    }

    printf("[ELF/nettest] OK (gw_rtt=%ums, udp=8B, tcp_3way+data=8B)\n", ping_rtt);
    return 0;
}
