#ifndef FLUX_FS_JOURNAL_FSCK_H
#define FLUX_FS_JOURNAL_FSCK_H

#include "types.h"

typedef struct {
    uint32_t tx_id;
    uint16_t fs_type; /* 0=ext2/JBD2, 1=Btrfs-CoW, 2=NTFS3-$LogFile, 3=LiveRAMDisk */
    uint16_t op_kind;
    uint32_t target_ino;
    uint32_t block_crc32c;
    const char *invariant_name;
    const char *repair_routine;
} fs_heal_rule_t;

typedef struct {
    bool     journal_clean;
    uint32_t transactions_committed;
    uint32_t orphan_inodes_reclaimed;
    uint32_t superblock_mirrors_synced;
    uint32_t crc32c_blocks_verified;
} fs_journal_status_t;

void   fs_journal_fsck_init(void);
bool   fsck_online_verify_and_heal(fs_journal_status_t *out);
size_t fs_heal_rule_db_size(void);
bool   fs_journal_fsck_selftest(void);

#endif
