#ifndef FLUX_FIREWALL_H
#define FLUX_FIREWALL_H

#include "types.h"

#define FW_ZONE_DROP    0U
#define FW_ZONE_PUBLIC  1U
#define FW_ZONE_TRUSTED 2U

#define FW_MAX_RULES 24

typedef struct {
    bool     used;
    uint8_t  proto;
    uint16_t port; /* 0 = any port for proto */
    bool     allow;
} fw_rule_t;

void        fw_init(void);
void        fw_set_zone(uint32_t zone);
uint32_t    fw_get_zone(void);
const char *fw_zone_name(uint32_t zone);
bool        fw_add_rule(uint8_t proto, uint16_t port, bool allow);
bool        fw_remove_rule(uint8_t proto, uint16_t port);
bool        fw_check_packet(uint8_t proto, uint16_t src_port, uint16_t dst_port);
void        fw_stats(uint32_t *zone_out, uint32_t *allowed_out, uint32_t *dropped_out);
size_t      fw_list_rules(fw_rule_t *out, size_t max_rules);

#endif
