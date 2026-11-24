#ifndef FLUX_FLUXFS_BTREE_COW_H
#define FLUX_FLUXFS_BTREE_COW_H

#include "types.h"

#define FLUXFS_EXTENT_BYTES 256U
#define FLUXFS_MAX_NODES    64U
#define FLUXFS_FANOUT       8U

typedef struct {
    uint32_t preset_id;
    uint8_t  btree_id;     /* 1=Extents, 2=Inodes, 3=Dirents, 4=Reflink, 5=Checksums */
    uint8_t  compress_alg; /* 1=LZ4-Fast, 2=ZSTD-3, 3=ZSTD-12, 4=None-DAX */
    uint16_t bucket_kb;
    const char *subvol_policy;
    const char *cow_allocator_op;
    const char *self_heal_invariant;
} fluxfs_layout_rule_t;

void   fluxfs_btree_cow_init(void);
bool   fluxfs_put_extent(uint64_t ino_key, const void *data, size_t len);
bool   fluxfs_get_extent(uint32_t root_slot, uint64_t ino_key, void *out_buf, size_t len);
uint32_t fluxfs_create_snapshot(void);
uint32_t fluxfs_scrub_and_heal_all(void);
size_t fluxfs_layout_db_size(void);
bool   fluxfs_btree_cow_selftest(void);

#endif
