#ifndef FLUX_ZSWAP_LZ4_COMPRESSOR_H
#define FLUX_ZSWAP_LZ4_COMPRESSOR_H

#include "types.h"

typedef struct {
    uint32_t pool_id;
    uint8_t  zpool_driver; /* 1=zsmalloc, 2=z3fold, 3=zbud */
    uint8_t  codec_id;     /* 1=LZ4-Fast, 2=LZ4HC, 3=ZSTD-Fast */
    uint16_t accept_thr_pct;
    const char *pool_zone;
    const char *eviction_policy;
    const char *simd_matchfinder;
} zswap_lz4_rule_t;

void   zswap_lz4_compressor_init(void);
size_t flux_lz4_compress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap);
size_t flux_lz4_decompress(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap);
size_t zswap_lz4_db_size(void);
bool   zswap_lz4_compressor_selftest(void);

#endif
