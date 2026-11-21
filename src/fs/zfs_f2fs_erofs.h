#ifndef FLUX_ZFS_F2FS_EROFS_H
#define FLUX_ZFS_F2FS_EROFS_H

#include "types.h"

typedef struct {
    uint32_t pool_rule_id;
    uint8_t  fs_engine; /* 1=OpenZFS-2.2, 2=F2FS-NVMe-Flash, 3=EROFS-LZ4HC, 4=OverlayFS-Container */
    uint8_t  raid_or_temp_level;
    uint16_t recordsize_kb;
    const char *dataset_or_zone;
    const char *pipeline_stage;
    const char *checksum_and_compression;
} zfs_f2fs_erofs_rule_t;

typedef struct {
    bool     openzfs_spa_arc_online;
    bool     f2fs_multihead_ftl_online;
    bool     erofs_lz4hc_direct_map;
    bool     overlayfs_metacopy_ready;
    uint32_t arc_hit_ratio_pct;
    uint32_t registered_rules;
} zfs_f2fs_erofs_status_t;

void   zfs_f2fs_erofs_init(void);
bool   zfs_f2fs_erofs_get_status(zfs_f2fs_erofs_status_t *out);
size_t zfs_f2fs_erofs_db_size(void);
bool   zfs_f2fs_erofs_selftest(void);

#endif
