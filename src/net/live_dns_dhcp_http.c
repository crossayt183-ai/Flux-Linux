#include "live_dns_dhcp_http.h"
#include "net.h"
#include "e1000.h"
#include "string.h"
#include "serial.h"

#define DHCP_MAGIC_COOKIE 0x63825363U

static live_net_client_status_t g_lnc;

/* RFC 1035 Section 4.1: Encode standard recursive A-record DNS query */
size_t dns_encode_query(uint8_t *out_pkt, size_t cap, uint16_t txid, const char *hostname) {
    if (!out_pkt || !hostname || cap < 64) return 0;
    memset(out_pkt, 0, 12);

    out_pkt[0] = (uint8_t)(txid >> 8);
    out_pkt[1] = (uint8_t)(txid & 0xFFU);
    out_pkt[2] = 0x01U; /* RD = 1 (Recursion Desired) */
    out_pkt[3] = 0x00U;
    out_pkt[4] = 0x00U; out_pkt[5] = 0x01U; /* QDCOUNT = 1 */

    size_t pos = 12;
    const char *p = hostname;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        size_t lbl_len = (size_t)(dot - p);
        if (lbl_len == 0 || lbl_len > 63 || pos + 1 + lbl_len + 5 > cap) return 0;
        out_pkt[pos++] = (uint8_t)lbl_len;
        memcpy(out_pkt + pos, p, lbl_len);
        pos += lbl_len;
        p = (*dot == '.') ? (dot + 1) : dot;
    }
    out_pkt[pos++] = 0x00U; /* Root label terminator */
    out_pkt[pos++] = 0x00U; out_pkt[pos++] = 0x01U; /* QTYPE = A (1) */
    out_pkt[pos++] = 0x00U; out_pkt[pos++] = 0x01U; /* QCLASS = IN (1) */
    return pos;
}

static size_t dns_skip_name(const uint8_t *pkt, size_t len, size_t pos) {
    size_t steps = 0;
    while (pos < len && steps < 128) {
        uint8_t c = pkt[pos];
        if (c == 0) return pos + 1;
        if ((c & 0xC0U) == 0xC0U) {
            if (pos + 1 >= len) return len + 1;
            return pos + 2; /* RFC 1035 Section 4.1.4 compressed pointer */
        }
        pos += 1U + (size_t)c;
        steps++;
    }
    return len + 1;
}

/* RFC 1035 Section 4.1.3: Decode DNS response with compression pointers & CNAME skipping */
bool dns_decode_a_record(const uint8_t *pkt, size_t len, uint16_t expected_txid, uint32_t *out_ipv4) {
    if (!pkt || len < 12 || !out_ipv4) return false;
    uint16_t rx_txid = ((uint16_t)pkt[0] << 8) | pkt[1];
    if (rx_txid != expected_txid) return false;
    if ((pkt[2] & 0x80U) == 0) return false;        /* Must be QR=1 response */
    if ((pkt[3] & 0x0FU) != 0) return false;        /* RCODE must be 0 (NOERROR) */

    uint16_t qdcount = ((uint16_t)pkt[4] << 8) | pkt[5];
    uint16_t ancount = ((uint16_t)pkt[6] << 8) | pkt[7];
    if (ancount == 0) return false;

    size_t pos = 12;
    for (uint16_t q = 0; q < qdcount; q++) {
        pos = dns_skip_name(pkt, len, pos);
        if (pos + 4 > len) return false;
        pos += 4;
    }

    for (uint16_t a = 0; a < ancount; a++) {
        pos = dns_skip_name(pkt, len, pos);
        if (pos + 10 > len) return false;
        uint16_t rtype  = ((uint16_t)pkt[pos + 0] << 8) | pkt[pos + 1];
        uint16_t rclass = ((uint16_t)pkt[pos + 2] << 8) | pkt[pos + 3];
        uint16_t rdlen  = ((uint16_t)pkt[pos + 8] << 8) | pkt[pos + 9];
        pos += 10;
        if (pos + rdlen > len) return false;

        if (rtype == 1U && rclass == 1U && rdlen == 4U) {
            *out_ipv4 = ip4(pkt[pos + 0], pkt[pos + 1], pkt[pos + 2], pkt[pos + 3]);
            return true;
        }
        pos += rdlen;
    }
    return false;
}

bool dns_resolve_ipv4(const char *hostname, uint32_t dns_ip, uint32_t timeout_ms, uint32_t *out_ipv4) {
    if (!hostname || !out_ipv4 || !e1000_is_ready()) return false;
    uint8_t qbuf[256];
    uint16_t txid = 0x4658U;
    size_t qlen = dns_encode_query(qbuf, sizeof(qbuf), txid, hostname);
    if (qlen == 0) return false;

    int sid = sock_create(SOCK_DGRAM);
    if (sid < 0) return false;
    sock_bind(sid, 0, 53053U);

    bool ok = false;
    if (sock_sendto(sid, qbuf, qlen, dns_ip, 53U) == (int64_t)qlen) {
        uint8_t rbuf[512];
        uint32_t rip = 0;
        uint16_t rport = 0;
        int64_t r = sock_recvfrom(sid, rbuf, sizeof(rbuf), &rip, &rport, timeout_ms);
        if (r >= 12 && dns_decode_a_record(rbuf, (size_t)r, txid, out_ipv4)) {
            ok = true;
        }
    }
    sock_close(sid);
    return ok;
}

/* RFC 2131: Build BOOTP/DHCP DISCOVER datagram */
size_t dhcp_build_discover(uint8_t *out_pkt, size_t cap, uint32_t xid, const uint8_t mac[6]) {
    if (!out_pkt || !mac || cap < 300) return 0;
    memset(out_pkt, 0, 300);

    out_pkt[0]  = 1U;    /* BOOTREQUEST */
    out_pkt[1]  = 1U;    /* Ethernet 10Mb */
    out_pkt[2]  = 6U;    /* Hardware addr len */
    out_pkt[4]  = (uint8_t)(xid >> 24);
    out_pkt[5]  = (uint8_t)(xid >> 16);
    out_pkt[6]  = (uint8_t)(xid >> 8);
    out_pkt[7]  = (uint8_t)(xid & 0xFFU);
    out_pkt[10] = 0x80U; /* Broadcast flag */
    memcpy(out_pkt + 28, mac, 6);

    /* DHCP Magic Cookie 99.130.83.99 @ offset 236 */
    out_pkt[236] = 99U; out_pkt[237] = 130U; out_pkt[238] = 83U; out_pkt[239] = 99U;

    size_t o = 240;
    /* Option 53: DHCP Message Type = 1 (DHCPDISCOVER) */
    out_pkt[o++] = 53U; out_pkt[o++] = 1U; out_pkt[o++] = 1U;
    /* Option 55: Parameter Request List (Subnet Mask, Router, DNS, Domain) */
    out_pkt[o++] = 55U; out_pkt[o++] = 4U; out_pkt[o++] = 1U; out_pkt[o++] = 3U; out_pkt[o++] = 6U; out_pkt[o++] = 15U;
    /* Option 12: Hostname "fluxos" */
    out_pkt[o++] = 12U; out_pkt[o++] = 6U; memcpy(out_pkt + o, "fluxos", 6); o += 6;
    /* Option 255: END */
    out_pkt[o++] = 255U;
    return 300;
}

bool dhcp_parse_offer_or_ack(const uint8_t *pkt, size_t len, uint32_t xid,
                             uint32_t *out_yiaddr, uint32_t *out_mask,
                             uint32_t *out_gw, uint32_t *out_dns, uint8_t *out_msg_type) {
    if (!pkt || len < 244 || pkt[0] != 2U) return false; /* BOOTREPLY = 2 */
    uint32_t rx_xid = ((uint32_t)pkt[4] << 24) | ((uint32_t)pkt[5] << 16) |
                      ((uint32_t)pkt[6] << 8)  | (uint32_t)pkt[7];
    if (rx_xid != xid) return false;
    if (pkt[236] != 99U || pkt[237] != 130U || pkt[238] != 83U || pkt[239] != 99U) return false;

    if (out_yiaddr) *out_yiaddr = ip4(pkt[16], pkt[17], pkt[18], pkt[19]);
    uint8_t mtype = 0;
    size_t pos = 240;
    while (pos < len) {
        uint8_t opt = pkt[pos++];
        if (opt == 0U) continue;
        if (opt == 255U) break;
        if (pos >= len) break;
        uint8_t olen = pkt[pos++];
        if (pos + olen > len) break;

        if (opt == 53U && olen == 1U) {
            mtype = pkt[pos];
        } else if (opt == 1U && olen >= 4U && out_mask) {
            *out_mask = ip4(pkt[pos], pkt[pos + 1], pkt[pos + 2], pkt[pos + 3]);
        } else if (opt == 3U && olen >= 4U && out_gw) {
            *out_gw = ip4(pkt[pos], pkt[pos + 1], pkt[pos + 2], pkt[pos + 3]);
        } else if (opt == 6U && olen >= 4U && out_dns) {
            *out_dns = ip4(pkt[pos], pkt[pos + 1], pkt[pos + 2], pkt[pos + 3]);
        }
        pos += olen;
    }
    if (out_msg_type) *out_msg_type = mtype;
    return mtype == 2U || mtype == 5U; /* OFFER=2 or ACK=5 */
}

/* Real HTTP/1.1 TCP client over FluxOS BSD socket stream */
int64_t http_get_ipv4(uint32_t server_ip, uint16_t port, const char *host, const char *path,
                      char *resp_out, size_t resp_cap, uint32_t timeout_ms) {
    if (!resp_out || resp_cap < 32 || !e1000_is_ready()) return -1;
    int sid = sock_create(SOCK_STREAM);
    if (sid < 0) return -1;

    if (sock_connect(sid, server_ip, port) != 0) {
        sock_close(sid);
        return -1;
    }

    char req[256];
    size_t rpos = 0;
    const char *parts[6] = {
        "GET ",
        (path && *path) ? path : "/",
        " HTTP/1.1\r\nHost: ",
        (host && *host) ? host : "fluxos.local",
        "\r\nUser-Agent: FluxOS/0.4-x86_64\r\nConnection: close\r\n\r\n",
        NULL
    };
    for (int i = 0; parts[i]; i++) {
        size_t pl = strlen(parts[i]);
        if (rpos + pl < sizeof(req)) {
            memcpy(req + rpos, parts[i], pl);
            rpos += pl;
        }
    }
    req[rpos] = '\0';

    size_t rlen = rpos;
    if (sock_sendto(sid, req, rlen, server_ip, port) < 0) {
        sock_close(sid);
        return -1;
    }

    uint32_t rip = 0;
    uint16_t rport = 0;
    int64_t got = sock_recvfrom(sid, resp_out, resp_cap - 1, &rip, &rport, timeout_ms);
    if (got >= 0) {
        resp_out[got] = '\0';
    }
    sock_close(sid);
    return got;
}

void live_dns_dhcp_http_init(void) {
    memset(&g_lnc, 0, sizeof(g_lnc));
    g_lnc.leased_ip     = ip4(10, 0, 2, 15);
    g_lnc.dns_server_ip = ip4(10, 0, 2, 3); /* QEMU SLIRP & VirtualBox NAT built-in DNS forwarder */
    kprintf("[NET/DNS+DHCP+HTTP] RFC 2131 DHCP + RFC 1035 DNS (10.0.2.3:53) + HTTP/1.1 TCP client ready\n");
}

bool live_dns_dhcp_http_selftest(void) {
    /* 1. Verify RFC 1035 DNS query encoding + compressed response pointer (0xC00C) decoding */
    uint8_t qpkt[128];
    size_t qlen = dns_encode_query(qpkt, sizeof(qpkt), 0xBEEFU, "kernel.fluxos.org");
    if (qlen != 35) return false;

    uint8_t fake_resp[64];
    memcpy(fake_resp, qpkt, qlen);
    fake_resp[2] = 0x81U; fake_resp[3] = 0x80U; /* QR=1, RD=1, RA=1, NOERROR */
    fake_resp[6] = 0x00U; fake_resp[7] = 0x01U; /* ANCOUNT = 1 */
    size_t p = qlen;
    fake_resp[p++] = 0xC0U; fake_resp[p++] = 0x0CU; /* Compressed pointer -> offset 12 */
    fake_resp[p++] = 0x00U; fake_resp[p++] = 0x01U; /* TYPE A */
    fake_resp[p++] = 0x00U; fake_resp[p++] = 0x01U; /* CLASS IN */
    fake_resp[p++] = 0x00U; fake_resp[p++] = 0x00U; fake_resp[p++] = 0x00U; fake_resp[p++] = 0x3CU; /* TTL 60s */
    fake_resp[p++] = 0x00U; fake_resp[p++] = 0x04U; /* RDLENGTH 4 */
    fake_resp[p++] = 93U;   fake_resp[p++] = 184U;  fake_resp[p++] = 216U;  fake_resp[p++] = 34U;

    uint32_t resolved = 0;
    if (!dns_decode_a_record(fake_resp, p, 0xBEEFU, &resolved) || resolved != ip4(93, 184, 216, 34)) {
        return false;
    }
    g_lnc.dns_codec_verified = true;

    /* 2. Verify RFC 2131 DHCP DISCOVER + ACK options parser */
    static const uint8_t test_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    uint8_t dpkt[320];
    if (dhcp_build_discover(dpkt, sizeof(dpkt), 0x11223344U, test_mac) != 300) return false;

    dpkt[0]  = 2U; /* Convert into synthetic BOOTREPLY DHCPACK */
    dpkt[16] = 10U; dpkt[17] = 0U; dpkt[18] = 2U; dpkt[19] = 15U; /* yiaddr 10.0.2.15 */
    size_t o = 240;
    dpkt[o++] = 53U; dpkt[o++] = 1U; dpkt[o++] = 5U; /* DHCPACK */
    dpkt[o++] = 1U;  dpkt[o++] = 4U; dpkt[o++] = 255U; dpkt[o++] = 255U; dpkt[o++] = 255U; dpkt[o++] = 0U;
    dpkt[o++] = 3U;  dpkt[o++] = 4U; dpkt[o++] = 10U;  dpkt[o++] = 0U;   dpkt[o++] = 2U;   dpkt[o++] = 2U;
    dpkt[o++] = 6U;  dpkt[o++] = 4U; dpkt[o++] = 10U;  dpkt[o++] = 0U;   dpkt[o++] = 2U;   dpkt[o++] = 3U;
    dpkt[o++] = 255U;

    uint32_t yi = 0, mk = 0, gw = 0, dn = 0;
    uint8_t mt = 0;
    if (!dhcp_parse_offer_or_ack(dpkt, 300, 0x11223344U, &yi, &mk, &gw, &dn, &mt) ||
        mt != 5U || yi != ip4(10, 0, 2, 15) || gw != ip4(10, 0, 2, 2) || dn != ip4(10, 0, 2, 3)) {
        return false;
    }
    g_lnc.dhcp_codec_verified = true;

    /* 3. Probe live NAT DNS forwarder 10.0.2.3:53 if NIC is present (non-blocking 150ms probe) */
    if (e1000_is_ready()) {
        uint32_t live_ip = 0;
        if (dns_resolve_ipv4("example.com", ip4(10, 0, 2, 3), 150U, &live_ip) && live_ip != 0) {
            g_lnc.nat_dns_reachable = true;
            g_lnc.last_resolved_ip  = live_ip;
            kprintf("[NET/DNS] Live NAT DNS resolved example.com -> %u.%u.%u.%u\n",
                    (live_ip >> 24) & 0xFFU, (live_ip >> 16) & 0xFFU,
                    (live_ip >> 8) & 0xFFU, live_ip & 0xFFU);
        }
    }
    return true;
}
