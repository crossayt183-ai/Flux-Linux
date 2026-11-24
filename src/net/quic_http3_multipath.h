#ifndef FLUX_QUIC_HTTP3_MULTIPATH_H
#define FLUX_QUIC_HTTP3_MULTIPATH_H

#include "types.h"

typedef struct {
    uint32_t rule_id;
    uint16_t hw_class;
    uint16_t param_val;
    const char *profile_key;
    const char *fastpath_engine;
    const char *telemetry_bound;
} quic_http3_multipath_rule_t;

void   quic_http3_multipath_init(void);
size_t quic_http3_multipath_db_size(void);
bool   quic_http3_multipath_selftest(void);

#endif
