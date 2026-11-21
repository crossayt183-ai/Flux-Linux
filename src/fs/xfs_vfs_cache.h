#ifndef FLUX_XFS_VFS_CACHE_H
#define FLUX_XFS_VFS_CACHE_H

#include "types.h"

typedef struct {
    uint32_t ag_rule_id;
    uint16_t fs_driver; /* 1=XFS, 2=ext4, 3=Btrfs, 4=NTFS3, 5=exFAT/FAT32, 6=io_uring */
    uint16_t iouring_opcode;
    uint32_t ag_block_count;
    const char *subsys_label;
    const char *btree_or_ring_op;
    const char *consistency_guarantee;
} xfs_iouring_rule_t;

typedef struct {
    bool     xfs_ag_btree_online;
    bool     iouring_sqpoll_ready;
    bool     rcu_dcache_lockless;
    uint32_t xfs_allocation_groups;
    uint32_t iouring_iops_k;
} xfs_vfs_status_t;

void   xfs_vfs_cache_init(void);
bool   xfs_vfs_get_status(xfs_vfs_status_t *out);
size_t xfs_vfs_rule_db_size(void);
bool   xfs_vfs_cache_selftest(void);

#endif
