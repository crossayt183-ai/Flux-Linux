#ifndef FLUX_NFTABLES_IDS_DPI_H
#define FLUX_NFTABLES_IDS_DPI_H

#include "types.h"

typedef struct {
    uint32_t ids_rule_id;
    uint8_t  engine_stage; /* 1=nftables-PreRouting, 2=IDS-DPI-Signature, 3=ARP-Spoof-DAI-Guard, 4=DNSSEC-TLS13-Pin */
    uint8_t  severity_lvl; /* 1=CRITICAL-DROP, 2=HIGH-TARPIT, 3=MEDIUM-RATELIMIT, 4=STRICT-PIN */
    uint16_t proto_port;
    const char *signature_name;
    const char *bytecode_pattern;
    const char *countermeasure;
} nftables_ids_rule_t;

typedef struct {
    bool     nftables_vm_jit_online;
    bool     ids_zero_copy_dpi_active;
    bool     dynamic_arp_inspection;
    bool     dnssec_doh_strict_verify;
    uint32_t loaded_ids_signatures;
} nftables_ids_status_t;

void   nftables_ids_dpi_init(void);
bool   nftables_ids_get_status(nftables_ids_status_t *out);
size_t nftables_ids_db_size(void);
bool   nftables_ids_dpi_selftest(void);

#endif
