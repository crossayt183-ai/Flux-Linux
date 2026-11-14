#ifndef FLUX_FS_MULTI_H
#define FLUX_FS_MULTI_H

#include "types.h"

#define BTRFS_MAGIC_64   0x4D5F53665248425FULL /* "_BHRfS_M" */
#define EXT4_SUPER_MAGIC 0xEF53U
#define NTFS_OEM_MAGIC   0x202020205346544EULL /* "NTFS    " */
#define EXFAT_OEM_MAGIC  0x2020205441465845ULL /* "EXFAT   " */

typedef enum {
    FS_KIND_EXT2  = 0,
    FS_KIND_BTRFS = 1,
    FS_KIND_EXT4  = 2,
    FS_KIND_NTFS3 = 3,
    FS_KIND_EXFAT = 4,
    FS_KIND_FAT32 = 5
} fs_kind_t;

typedef struct {
    uint32_t snap_id;
    uint64_t root_tree_bytenr;
    uint64_t generation;
    uint64_t timestamp_sec;
    uint32_t compressed_zstd_kb;
    bool     read_only;
    char     subvol_source[24];
    char     snap_path[64];
    char     description[64];
} btrfs_snapshot_t;

typedef struct {
    fs_kind_t kind;
    bool      mounted;
    bool      rw_enabled;
    bool      cow_enabled;
    bool      zstd_compressed;
    uint64_t  total_mib;
    uint64_t  used_mib;
    char      device[32];
    char      mount_point[48];
    char      fs_label[32];
    char      features[80];
} fs_mount_info_t;

void   fs_multi_init(void);
size_t fs_multi_mount_count(void);
bool   fs_multi_get_mount(size_t idx, fs_mount_info_t *out);
size_t btrfs_snapshot_count(void);
bool   btrfs_get_snapshot(size_t idx, btrfs_snapshot_t *out);
bool   btrfs_create_snapshot(const char *subvol, const char *desc, uint32_t *out_snap_id);
bool   btrfs_rollback_snapshot(uint32_t snap_id);
bool   fs_multi_selftest(void);

#endif
