#include "firewall.h"
#include "net.h"
#include "spinlock.h"
#include "serial.h"
#include "string.h"

static fw_rule_t  fw_rules[FW_MAX_RULES];
static uint32_t   fw_zone         = FW_ZONE_PUBLIC;
static uint32_t   fw_allowed_pkts = 0;
static uint32_t   fw_dropped_pkts = 0;
static spinlock_t fw_lock         = SPINLOCK_INIT;

void fw_init(void) {
    spin_lock_irqsave(&fw_lock);
    memset(fw_rules, 0, sizeof(fw_rules));
    fw_zone         = FW_ZONE_PUBLIC;
    fw_allowed_pkts = 0;
    fw_dropped_pkts = 0;
    spin_unlock_irqrestore(&fw_lock);

    /* Default Firewalld 'public' zone policy */
    fw_add_rule(IP_PROTO_ICMP, 0, true);
    fw_add_rule(IP_PROTO_TCP, 80, true);
    fw_add_rule(IP_PROTO_TCP, 443, true);
    fw_add_rule(IP_PROTO_TCP, 8080, true);
    fw_add_rule(IP_PROTO_UDP, 53, true);
    fw_add_rule(IP_PROTO_UDP, 7001, true);

    kprintf("[FIREWALLD] Stateful packet filter active (zone=%s, 6 default rules)\n",
            fw_zone_name(fw_zone));
}

void fw_set_zone(uint32_t zone) {
    if (zone > FW_ZONE_TRUSTED) return;
    spin_lock_irqsave(&fw_lock);
    fw_zone = zone;
    spin_unlock_irqrestore(&fw_lock);
}

uint32_t fw_get_zone(void) { return fw_zone; }

const char *fw_zone_name(uint32_t zone) {
    switch (zone) {
    case FW_ZONE_DROP:    return "drop";
    case FW_ZONE_PUBLIC:  return "public";
    case FW_ZONE_TRUSTED: return "trusted";
    default:              return "unknown";
    }
}

bool fw_add_rule(uint8_t proto, uint16_t port, bool allow) {
    spin_lock_irqsave(&fw_lock);
    for (int i = 0; i < FW_MAX_RULES; i++) {
        if (fw_rules[i].used && fw_rules[i].proto == proto && fw_rules[i].port == port) {
            fw_rules[i].allow = allow;
            spin_unlock_irqrestore(&fw_lock);
            return true;
        }
    }
    for (int i = 0; i < FW_MAX_RULES; i++) {
        if (!fw_rules[i].used) {
            fw_rules[i].used  = true;
            fw_rules[i].proto = proto;
            fw_rules[i].port  = port;
            fw_rules[i].allow = allow;
            spin_unlock_irqrestore(&fw_lock);
            return true;
        }
    }
    spin_unlock_irqrestore(&fw_lock);
    return false;
}

bool fw_remove_rule(uint8_t proto, uint16_t port) {
    spin_lock_irqsave(&fw_lock);
    for (int i = 0; i < FW_MAX_RULES; i++) {
        if (fw_rules[i].used && fw_rules[i].proto == proto && fw_rules[i].port == port) {
            fw_rules[i].used = false;
            spin_unlock_irqrestore(&fw_lock);
            return true;
        }
    }
    spin_unlock_irqrestore(&fw_lock);
    return false;
}

static bool has_allow_rule_locked(uint8_t proto, uint16_t port) {
    for (int i = 0; i < FW_MAX_RULES; i++) {
        if (fw_rules[i].used && fw_rules[i].allow && fw_rules[i].proto == proto &&
            (fw_rules[i].port == 0 || fw_rules[i].port == port)) {
            return true;
        }
    }
    return false;
}

bool fw_check_packet(uint8_t proto, uint16_t src_port, uint16_t dst_port) {
    spin_lock_irqsave(&fw_lock);

    if (fw_zone == FW_ZONE_TRUSTED) {
        fw_allowed_pkts++;
        spin_unlock_irqrestore(&fw_lock);
        return true;
    }
    if (fw_zone == FW_ZONE_DROP) {
        fw_dropped_pkts++;
        spin_unlock_irqrestore(&fw_lock);
        return false;
    }

    /* 1. Explicit DENY rules always win */
    for (int i = 0; i < FW_MAX_RULES; i++) {
        if (fw_rules[i].used && !fw_rules[i].allow && fw_rules[i].proto == proto &&
            (fw_rules[i].port == 0 || fw_rules[i].port == dst_port || fw_rules[i].port == src_port)) {
            fw_dropped_pkts++;
            spin_unlock_irqrestore(&fw_lock);
            return false;
        }
    }

    /* 2. Match explicit ALLOW rules or stateful ephemeral reply for an allowed service */
    bool ok = has_allow_rule_locked(proto, dst_port);
    if (!ok && dst_port >= 49152 && has_allow_rule_locked(proto, src_port)) {
        ok = true;
    }

    if (ok) fw_allowed_pkts++;
    else    fw_dropped_pkts++;

    spin_unlock_irqrestore(&fw_lock);
    return ok;
}

void fw_stats(uint32_t *zone_out, uint32_t *allowed_out, uint32_t *dropped_out) {
    spin_lock_irqsave(&fw_lock);
    if (zone_out)    *zone_out    = fw_zone;
    if (allowed_out) *allowed_out = fw_allowed_pkts;
    if (dropped_out) *dropped_out = fw_dropped_pkts;
    spin_unlock_irqrestore(&fw_lock);
}

size_t fw_list_rules(fw_rule_t *out, size_t max_rules) {
    if (!out || max_rules == 0) return 0;
    spin_lock_irqsave(&fw_lock);
    size_t n = 0;
    for (int i = 0; i < FW_MAX_RULES && n < max_rules; i++) {
        if (fw_rules[i].used) {
            out[n++] = fw_rules[i];
        }
    }
    spin_unlock_irqrestore(&fw_lock);
    return n;
}
