#include "ext2.h"
#include "ahci.h"
#include "heap.h"
#include "mutex.h"
#include "string.h"
#include "serial.h"

#define EXT2_MAGIC        0xEF53U
#define EXT2_ROOT_INO     2U
#define EXT2_S_IFMT       0xF000U
#define EXT2_S_IFDIR      0x4000U
#define EXT2_S_IFREG      0x8000U
#define EXT2_FT_REG_FILE  1
#define EXT2_FT_DIR       2

typedef struct PACKED {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint8_t  rsv[934];
} ext2_superblock_t;

typedef struct PACKED {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} ext2_bgd_t;

typedef struct PACKED {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} ext2_inode_t;

typedef struct PACKED {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} ext2_dirent_t;

typedef struct {
    ext2_superblock_t sb;
    ext2_bgd_t       *bgdt;
    uint32_t          group_count;
    uint32_t          block_size;
    uint32_t          sectors_per_block;
    uint32_t          inode_size;
    uint32_t          bgdt_block;
    mutex_t           lock;
} ext2_priv_t;

static int read_blk(ext2_priv_t *e, uint32_t blk, void *buf) {
    uint64_t lba = (uint64_t)blk * e->sectors_per_block;
    return ahci_read_sectors(lba, e->sectors_per_block, buf);
}

static int write_blk(ext2_priv_t *e, uint32_t blk, const void *buf) {
    uint64_t lba = (uint64_t)blk * e->sectors_per_block;
    return ahci_write_sectors(lba, e->sectors_per_block, buf);
}

static int sync_metadata(ext2_priv_t *e) {
    if (ahci_write_sectors(2, 2, &e->sb) != 0) return -1;
    uint8_t *blk = (uint8_t *)kmalloc(e->block_size);
    if (!blk) return -1;
    uint32_t bg_bytes = e->group_count * sizeof(ext2_bgd_t);
    uint32_t bg_blks = ALIGN_UP(bg_bytes, e->block_size) / e->block_size;
    const uint8_t *src = (const uint8_t *)e->bgdt;
    for (uint32_t i = 0; i < bg_blks; i++) {
        memset(blk, 0, e->block_size);
        uint32_t chunk = MIN(bg_bytes - i * e->block_size, e->block_size);
        memcpy(blk, src + i * e->block_size, chunk);
        write_blk(e, e->bgdt_block + i, blk);
    }
    kfree(blk);
    return 0;
}

static int read_raw_inode(ext2_priv_t *e, uint32_t ino, ext2_inode_t *out) {
    if (ino == 0 || ino > e->sb.s_inodes_count) return -1;
    uint32_t idx = ino - 1;
    uint32_t grp = idx / e->sb.s_inodes_per_group;
    uint32_t lidx = idx % e->sb.s_inodes_per_group;
    if (grp >= e->group_count) return -1;

    uint32_t per_blk = e->block_size / e->inode_size;
    uint32_t blk = e->bgdt[grp].bg_inode_table + (lidx / per_blk);
    uint32_t off = (lidx % per_blk) * e->inode_size;

    uint8_t *buf = (uint8_t *)kmalloc(e->block_size);
    if (!buf) return -1;
    int rc = read_blk(e, blk, buf);
    if (rc == 0) {
        memcpy(out, buf + off, sizeof(ext2_inode_t));
    }
    kfree(buf);
    return rc;
}

static int write_raw_inode(ext2_priv_t *e, uint32_t ino, const ext2_inode_t *in) {
    if (ino == 0 || ino > e->sb.s_inodes_count) return -1;
    uint32_t idx = ino - 1;
    uint32_t grp = idx / e->sb.s_inodes_per_group;
    uint32_t lidx = idx % e->sb.s_inodes_per_group;
    if (grp >= e->group_count) return -1;

    uint32_t per_blk = e->block_size / e->inode_size;
    uint32_t blk = e->bgdt[grp].bg_inode_table + (lidx / per_blk);
    uint32_t off = (lidx % per_blk) * e->inode_size;

    uint8_t *buf = (uint8_t *)kmalloc(e->block_size);
    if (!buf) return -1;
    int rc = read_blk(e, blk, buf);
    if (rc == 0) {
        memcpy(buf + off, in, sizeof(ext2_inode_t));
        rc = write_blk(e, blk, buf);
    }
    kfree(buf);
    return rc;
}

static vfs_inode_t *load_vfs_inode(vfs_fs_t *fs, uint32_t ino) {
    vfs_inode_t *cached = vfs_icache_find(fs, ino);
    if (cached) return cached;

    ext2_priv_t *e = (ext2_priv_t *)fs->priv;
    ext2_inode_t raw;
    if (read_raw_inode(e, ino, &raw) != 0) return NULL;
    if ((raw.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR &&
        (raw.i_mode & EXT2_S_IFMT) != EXT2_S_IFREG) {
        return NULL;
    }

    vfs_inode_t *vn = vfs_icache_alloc(fs, ino);
    if (!vn) return NULL;
    vn->type  = ((raw.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) ? VFS_DIR : VFS_FILE;
    vn->mode  = raw.i_mode;
    vn->size  = raw.i_size;
    vn->links = raw.i_links_count;
    return vn;
}

static uint32_t alloc_block(ext2_priv_t *e, uint32_t pref_grp) {
    uint8_t *bmp = (uint8_t *)kmalloc(e->block_size);
    if (!bmp) return 0;

    for (uint32_t pass = 0; pass < e->group_count; pass++) {
        uint32_t g = (pref_grp + pass) % e->group_count;
        if (e->bgdt[g].bg_free_blocks_count == 0) continue;
        if (read_blk(e, e->bgdt[g].bg_block_bitmap, bmp) != 0) continue;

        uint32_t max_b = e->sb.s_blocks_per_group;
        uint32_t base = g * e->sb.s_blocks_per_group + e->sb.s_first_data_block;
        if (base + max_b > e->sb.s_blocks_count) max_b = e->sb.s_blocks_count - base;

        for (uint32_t bi = 0; bi < max_b; bi++) {
            if ((bmp[bi >> 3] & (1U << (bi & 7))) == 0) {
                bmp[bi >> 3] |= (uint8_t)(1U << (bi & 7));
                write_blk(e, e->bgdt[g].bg_block_bitmap, bmp);
                e->bgdt[g].bg_free_blocks_count--;
                e->sb.s_free_blocks_count--;
                sync_metadata(e);

                memset(bmp, 0, e->block_size);
                uint32_t phys_blk = base + bi;
                write_blk(e, phys_blk, bmp);
                kfree(bmp);
                return phys_blk;
            }
        }
    }
    kfree(bmp);
    return 0;
}

static void free_block(ext2_priv_t *e, uint32_t blk) {
    if (blk < e->sb.s_first_data_block || blk >= e->sb.s_blocks_count) return;
    uint32_t adj = blk - e->sb.s_first_data_block;
    uint32_t g = adj / e->sb.s_blocks_per_group;
    uint32_t bi = adj % e->sb.s_blocks_per_group;
    if (g >= e->group_count) return;

    uint8_t *bmp = (uint8_t *)kmalloc(e->block_size);
    if (!bmp) return;
    if (read_blk(e, e->bgdt[g].bg_block_bitmap, bmp) == 0) {
        if (bmp[bi >> 3] & (1U << (bi & 7))) {
            bmp[bi >> 3] &= (uint8_t)~(1U << (bi & 7));
            write_blk(e, e->bgdt[g].bg_block_bitmap, bmp);
            e->bgdt[g].bg_free_blocks_count++;
            e->sb.s_free_blocks_count++;
            sync_metadata(e);
        }
    }
    kfree(bmp);
}

static uint32_t alloc_inode(ext2_priv_t *e, bool is_dir) {
    uint8_t *bmp = (uint8_t *)kmalloc(e->block_size);
    if (!bmp) return 0;

    for (uint32_t g = 0; g < e->group_count; g++) {
        if (e->bgdt[g].bg_free_inodes_count == 0) continue;
        if (read_blk(e, e->bgdt[g].bg_inode_bitmap, bmp) != 0) continue;

        for (uint32_t ii = 0; ii < e->sb.s_inodes_per_group; ii++) {
            uint32_t ino = g * e->sb.s_inodes_per_group + ii + 1;
            if (ino < e->sb.s_first_ino && ino != EXT2_ROOT_INO) continue;
            if ((bmp[ii >> 3] & (1U << (ii & 7))) == 0) {
                bmp[ii >> 3] |= (uint8_t)(1U << (ii & 7));
                write_blk(e, e->bgdt[g].bg_inode_bitmap, bmp);
                e->bgdt[g].bg_free_inodes_count--;
                if (is_dir) e->bgdt[g].bg_used_dirs_count++;
                e->sb.s_free_inodes_count--;
                sync_metadata(e);
                kfree(bmp);
                return ino;
            }
        }
    }
    kfree(bmp);
    return 0;
}

static void free_inode_num(ext2_priv_t *e, uint32_t ino, bool is_dir) {
    if (ino == 0 || ino > e->sb.s_inodes_count) return;
    uint32_t idx = ino - 1;
    uint32_t g = idx / e->sb.s_inodes_per_group;
    uint32_t ii = idx % e->sb.s_inodes_per_group;

    uint8_t *bmp = (uint8_t *)kmalloc(e->block_size);
    if (!bmp) return;
    if (read_blk(e, e->bgdt[g].bg_inode_bitmap, bmp) == 0) {
        if (bmp[ii >> 3] & (1U << (ii & 7))) {
            bmp[ii >> 3] &= (uint8_t)~(1U << (ii & 7));
            write_blk(e, e->bgdt[g].bg_inode_bitmap, bmp);
            e->bgdt[g].bg_free_inodes_count++;
            if (is_dir && e->bgdt[g].bg_used_dirs_count > 0) e->bgdt[g].bg_used_dirs_count--;
            e->sb.s_free_inodes_count++;
            sync_metadata(e);
        }
    }
    kfree(bmp);
}

static uint32_t get_or_alloc_file_blk(ext2_priv_t *e, ext2_inode_t *ri, uint32_t lblk, bool create) {
    uint32_t ptrs_per_blk = e->block_size / 4U;
    if (lblk < 12) {
        if (ri->i_block[lblk] == 0 && create) {
            uint32_t nb = alloc_block(e, 0);
            if (!nb) return 0;
            ri->i_block[lblk] = nb;
            ri->i_blocks += e->sectors_per_block;
        }
        return ri->i_block[lblk];
    }

    uint32_t ind_idx = lblk - 12;
    if (ind_idx >= ptrs_per_blk) return 0;

    if (ri->i_block[12] == 0) {
        if (!create) return 0;
        uint32_t ib = alloc_block(e, 0);
        if (!ib) return 0;
        ri->i_block[12] = ib;
        ri->i_blocks += e->sectors_per_block;
    }

    uint32_t *ind = (uint32_t *)kmalloc(e->block_size);
    if (!ind) return 0;
    if (read_blk(e, ri->i_block[12], ind) != 0) { kfree(ind); return 0; }

    uint32_t phys = ind[ind_idx];
    if (phys == 0 && create) {
        phys = alloc_block(e, 0);
        if (phys) {
            ind[ind_idx] = phys;
            write_blk(e, ri->i_block[12], ind);
            ri->i_blocks += e->sectors_per_block;
        }
    }
    kfree(ind);
    return phys;
}

static void free_all_inode_blocks(ext2_priv_t *e, ext2_inode_t *ri) {
    for (int i = 0; i < 12; i++) {
        if (ri->i_block[i]) { free_block(e, ri->i_block[i]); ri->i_block[i] = 0; }
    }
    if (ri->i_block[12]) {
        uint32_t *ind = (uint32_t *)kmalloc(e->block_size);
        if (ind && read_blk(e, ri->i_block[12], ind) == 0) {
            uint32_t cnt = e->block_size / 4U;
            for (uint32_t i = 0; i < cnt; i++) {
                if (ind[i]) free_block(e, ind[i]);
            }
        }
        if (ind) kfree(ind);
        free_block(e, ri->i_block[12]);
        ri->i_block[12] = 0;
    }
    ri->i_blocks = 0;
    ri->i_size = 0;
}

static vfs_inode_t *ext2_root(vfs_fs_t *fs) {
    return load_vfs_inode(fs, EXT2_ROOT_INO);
}

static vfs_inode_t *ext2_lookup(vfs_fs_t *fs, vfs_inode_t *dir, const char *name) {
    ext2_priv_t *e = (ext2_priv_t *)fs->priv;
    mutex_lock(&e->lock);

    ext2_inode_t raw;
    if (read_raw_inode(e, dir->ino, &raw) != 0) { mutex_unlock(&e->lock); return NULL; }

    size_t nlen = strlen(name);
    uint32_t blocks = (raw.i_size + e->block_size - 1) / e->block_size;
    uint8_t *blk = (uint8_t *)kmalloc(e->block_size);
    if (!blk) { mutex_unlock(&e->lock); return NULL; }

    uint32_t found_ino = 0;
    for (uint32_t b = 0; b < blocks && !found_ino; b++) {
        uint32_t pblk = get_or_alloc_file_blk(e, &raw, b, false);
        if (!pblk || read_blk(e, pblk, blk) != 0) continue;
        uint32_t off = 0;
        while (off + 8 <= e->block_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(blk + off);
            if (de->rec_len < 8 || off + de->rec_len > e->block_size) break;
            if (de->inode != 0 && de->name_len == nlen &&
                memcmp(de->name, name, nlen) == 0) {
                found_ino = de->inode;
                break;
            }
            off += de->rec_len;
        }
    }
    kfree(blk);
    mutex_unlock(&e->lock);
    return found_ino ? load_vfs_inode(fs, found_ino) : NULL;
}

static int64_t ext2_read(vfs_fs_t *fs, vfs_inode_t *node, uint64_t off, void *buf, size_t len) {
    ext2_priv_t *e = (ext2_priv_t *)fs->priv;
    mutex_lock(&e->lock);

    ext2_inode_t raw;
    if (read_raw_inode(e, node->ino, &raw) != 0) { mutex_unlock(&e->lock); return -1; }
    if (off >= raw.i_size) { mutex_unlock(&e->lock); return 0; }

    size_t avail = raw.i_size - (size_t)off;
    if (len > avail) len = avail;

    uint8_t *blk = (uint8_t *)kmalloc(e->block_size);
    if (!blk) { mutex_unlock(&e->lock); return -1; }

    size_t done = 0;
    uint8_t *dst = (uint8_t *)buf;
    while (done < len) {
        uint32_t lblk = (uint32_t)((off + done) / e->block_size);
        uint32_t boff = (uint32_t)((off + done) % e->block_size);
        uint32_t chunk = MIN((uint32_t)(len - done), e->block_size - boff);

        uint32_t pblk = get_or_alloc_file_blk(e, &raw, lblk, false);
        if (pblk == 0) {
            memset(dst + done, 0, chunk);
        } else {
            if (read_blk(e, pblk, blk) != 0) { kfree(blk); mutex_unlock(&e->lock); return -1; }
            memcpy(dst + done, blk + boff, chunk);
        }
        done += chunk;
    }
    kfree(blk);
    mutex_unlock(&e->lock);
    return (int64_t)done;
}

static int64_t ext2_write(vfs_fs_t *fs, vfs_inode_t *node, uint64_t off, const void *buf, size_t len) {
    ext2_priv_t *e = (ext2_priv_t *)fs->priv;
    mutex_lock(&e->lock);

    ext2_inode_t raw;
    if (read_raw_inode(e, node->ino, &raw) != 0) { mutex_unlock(&e->lock); return -1; }

    uint8_t *blk = (uint8_t *)kmalloc(e->block_size);
    if (!blk) { mutex_unlock(&e->lock); return -1; }

    size_t done = 0;
    const uint8_t *src = (const uint8_t *)buf;
    while (done < len) {
        uint32_t lblk = (uint32_t)((off + done) / e->block_size);
        uint32_t boff = (uint32_t)((off + done) % e->block_size);
        uint32_t chunk = MIN((uint32_t)(len - done), e->block_size - boff);

        uint32_t pblk = get_or_alloc_file_blk(e, &raw, lblk, true);
        if (pblk == 0) break;

        if (boff != 0 || chunk != e->block_size) {
            if (read_blk(e, pblk, blk) != 0) break;
        }
        memcpy(blk + boff, src + done, chunk);
        if (write_blk(e, pblk, blk) != 0) break;
        done += chunk;
    }

    if (off + done > raw.i_size) {
        raw.i_size = (uint32_t)(off + done);
    }
    node->size = raw.i_size;
    write_raw_inode(e, node->ino, &raw);
    kfree(blk);
    mutex_unlock(&e->lock);
    return done > 0 ? (int64_t)done : -1;
}

static bool add_dirent(ext2_priv_t *e, uint32_t dir_ino, ext2_inode_t *dir_raw,
                       uint32_t child_ino, const char *name, uint8_t ftype) {
    size_t nlen = strlen(name);
    uint16_t needed = (uint16_t)ALIGN_UP(8 + nlen, 4);
    uint8_t *blk = (uint8_t *)kmalloc(e->block_size);
    if (!blk) return false;

    uint32_t blocks = (dir_raw->i_size + e->block_size - 1) / e->block_size;
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t pblk = get_or_alloc_file_blk(e, dir_raw, b, false);
        if (!pblk || read_blk(e, pblk, blk) != 0) continue;

        uint32_t off = 0;
        while (off + 8 <= e->block_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(blk + off);
            if (de->rec_len < 8 || off + de->rec_len > e->block_size) break;

            if (de->inode == 0 && de->rec_len >= needed) {
                de->inode = child_ino;
                de->name_len = (uint8_t)nlen;
                de->file_type = ftype;
                memcpy(de->name, name, nlen);
                write_blk(e, pblk, blk);
                kfree(blk);
                return true;
            }
            uint16_t actual = (de->inode == 0) ? 8 : (uint16_t)ALIGN_UP(8 + de->name_len, 4);
            if (de->rec_len >= actual + needed) {
                uint16_t rem = (uint16_t)(de->rec_len - actual);
                de->rec_len = actual;
                ext2_dirent_t *nde = (ext2_dirent_t *)(blk + off + actual);
                nde->inode = child_ino;
                nde->rec_len = rem;
                nde->name_len = (uint8_t)nlen;
                nde->file_type = ftype;
                memcpy(nde->name, name, nlen);
                write_blk(e, pblk, blk);
                kfree(blk);
                return true;
            }
            off += de->rec_len;
        }
    }

    uint32_t nb = get_or_alloc_file_blk(e, dir_raw, blocks, true);
    if (!nb) { kfree(blk); return false; }

    memset(blk, 0, e->block_size);
    ext2_dirent_t *de = (ext2_dirent_t *)blk;
    de->inode = child_ino;
    de->rec_len = (uint16_t)e->block_size;
    de->name_len = (uint8_t)nlen;
    de->file_type = ftype;
    memcpy(de->name, name, nlen);
    write_blk(e, nb, blk);
    dir_raw->i_size += e->block_size;
    write_raw_inode(e, dir_ino, dir_raw);
    kfree(blk);
    return true;
}

static vfs_inode_t *ext2_create(vfs_fs_t *fs, vfs_inode_t *dir, const char *name, vfs_type_t type) {
    ext2_priv_t *e = (ext2_priv_t *)fs->priv;
    mutex_lock(&e->lock);

    ext2_inode_t dir_raw;
    if (read_raw_inode(e, dir->ino, &dir_raw) != 0) { mutex_unlock(&e->lock); return NULL; }

    bool is_dir = (type == VFS_DIR);
    uint32_t new_ino = alloc_inode(e, is_dir);
    if (!new_ino) { mutex_unlock(&e->lock); return NULL; }

    ext2_inode_t child;
    memset(&child, 0, sizeof(child));
    child.i_mode = is_dir ? (EXT2_S_IFDIR | 0755U) : (EXT2_S_IFREG | 0644U);
    child.i_links_count = is_dir ? 2 : 1;

    if (is_dir) {
        uint32_t dbl = alloc_block(e, 0);
        if (!dbl) { free_inode_num(e, new_ino, true); mutex_unlock(&e->lock); return NULL; }
        child.i_block[0] = dbl;
        child.i_blocks = e->sectors_per_block;
        child.i_size = e->block_size;

        uint8_t *blk = (uint8_t *)kmalloc(e->block_size);
        if (!blk) { free_block(e, dbl); free_inode_num(e, new_ino, true); mutex_unlock(&e->lock); return NULL; }
        memset(blk, 0, e->block_size);
        ext2_dirent_t *dot = (ext2_dirent_t *)blk;
        dot->inode = new_ino; dot->rec_len = 12; dot->name_len = 1; dot->file_type = EXT2_FT_DIR; dot->name[0] = '.';
        ext2_dirent_t *dotdot = (ext2_dirent_t *)(blk + 12);
        dotdot->inode = dir->ino; dotdot->rec_len = (uint16_t)(e->block_size - 12);
        dotdot->name_len = 2; dotdot->file_type = EXT2_FT_DIR; dotdot->name[0] = '.'; dotdot->name[1] = '.';
        write_blk(e, dbl, blk);
        kfree(blk);

        dir_raw.i_links_count++;
        write_raw_inode(e, dir->ino, &dir_raw);
    }

    write_raw_inode(e, new_ino, &child);
    if (!add_dirent(e, dir->ino, &dir_raw, new_ino, name, is_dir ? EXT2_FT_DIR : EXT2_FT_REG_FILE)) {
        free_all_inode_blocks(e, &child);
        free_inode_num(e, new_ino, is_dir);
        mutex_unlock(&e->lock);
        return NULL;
    }

    dir->size = dir_raw.i_size;
    mutex_unlock(&e->lock);
    return load_vfs_inode(fs, new_ino);
}

static int ext2_unlink(vfs_fs_t *fs, vfs_inode_t *dir, const char *name) {
    ext2_priv_t *e = (ext2_priv_t *)fs->priv;
    mutex_lock(&e->lock);

    ext2_inode_t dir_raw;
    if (read_raw_inode(e, dir->ino, &dir_raw) != 0) { mutex_unlock(&e->lock); return -1; }

    size_t nlen = strlen(name);
    uint32_t blocks = (dir_raw.i_size + e->block_size - 1) / e->block_size;
    uint8_t *blk = (uint8_t *)kmalloc(e->block_size);
    if (!blk) { mutex_unlock(&e->lock); return -1; }

    uint32_t target_ino = 0;
    for (uint32_t b = 0; b < blocks && !target_ino; b++) {
        uint32_t pblk = get_or_alloc_file_blk(e, &dir_raw, b, false);
        if (!pblk || read_blk(e, pblk, blk) != 0) continue;

        uint32_t off = 0;
        ext2_dirent_t *prev = NULL;
        while (off + 8 <= e->block_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(blk + off);
            if (de->rec_len < 8 || off + de->rec_len > e->block_size) break;
            if (de->inode != 0 && de->name_len == nlen && memcmp(de->name, name, nlen) == 0) {
                target_ino = de->inode;
                if (prev) prev->rec_len = (uint16_t)(prev->rec_len + de->rec_len);
                else de->inode = 0;
                write_blk(e, pblk, blk);
                break;
            }
            prev = de;
            off += de->rec_len;
        }
    }
    kfree(blk);

    if (!target_ino) { mutex_unlock(&e->lock); return -1; }

    ext2_inode_t raw;
    if (read_raw_inode(e, target_ino, &raw) == 0) {
        if (raw.i_links_count > 0) raw.i_links_count--;
        if (raw.i_links_count == 0) {
            bool is_dir = (raw.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
            free_all_inode_blocks(e, &raw);
            raw.i_mode = 0;
            write_raw_inode(e, target_ino, &raw);
            free_inode_num(e, target_ino, is_dir);
        } else {
            write_raw_inode(e, target_ino, &raw);
        }
    }
    vfs_icache_drop(fs, target_ino);
    mutex_unlock(&e->lock);
    return 0;
}

static int ext2_truncate(vfs_fs_t *fs, vfs_inode_t *node, uint64_t new_size) {
    if (new_size != 0) return -1;
    ext2_priv_t *e = (ext2_priv_t *)fs->priv;
    mutex_lock(&e->lock);
    ext2_inode_t raw;
    if (read_raw_inode(e, node->ino, &raw) != 0) { mutex_unlock(&e->lock); return -1; }
    free_all_inode_blocks(e, &raw);
    write_raw_inode(e, node->ino, &raw);
    node->size = 0;
    mutex_unlock(&e->lock);
    return 0;
}

static int ext2_readdir(vfs_fs_t *fs, vfs_inode_t *dir, vfs_dirent_t *out, size_t max_entries) {
    ext2_priv_t *e = (ext2_priv_t *)fs->priv;
    mutex_lock(&e->lock);

    ext2_inode_t raw;
    if (read_raw_inode(e, dir->ino, &raw) != 0) { mutex_unlock(&e->lock); return -1; }

    uint32_t blocks = (raw.i_size + e->block_size - 1) / e->block_size;
    uint8_t *blk = (uint8_t *)kmalloc(e->block_size);
    if (!blk) { mutex_unlock(&e->lock); return -1; }

    size_t cnt = 0;
    for (uint32_t b = 0; b < blocks && cnt < max_entries; b++) {
        uint32_t pblk = get_or_alloc_file_blk(e, &raw, b, false);
        if (!pblk || read_blk(e, pblk, blk) != 0) continue;

        uint32_t off = 0;
        while (off + 8 <= e->block_size && cnt < max_entries) {
            ext2_dirent_t *de = (ext2_dirent_t *)(blk + off);
            if (de->rec_len < 8 || off + de->rec_len > e->block_size) break;
            if (de->inode != 0 && de->name_len > 0) {
                size_t clen = MIN((size_t)de->name_len, (size_t)(VFS_MAX_NAME - 1));
                memcpy(out[cnt].name, de->name, clen);
                out[cnt].name[clen] = '\0';
                out[cnt].ino = de->inode;
                ext2_inode_t child;
                if (read_raw_inode(e, de->inode, &child) == 0) {
                    out[cnt].type = ((child.i_mode & EXT2_S_IFMT) == EXT2_S_IFDIR) ? VFS_DIR : VFS_FILE;
                    out[cnt].size = child.i_size;
                } else {
                    out[cnt].type = (de->file_type == EXT2_FT_DIR) ? VFS_DIR : VFS_FILE;
                    out[cnt].size = 0;
                }
                cnt++;
            }
            off += de->rec_len;
        }
    }
    kfree(blk);
    mutex_unlock(&e->lock);
    return (int)cnt;
}

static int ext2_statfs(vfs_fs_t *fs, vfs_statfs_t *out) {
    ext2_priv_t *e = (ext2_priv_t *)fs->priv;
    mutex_lock(&e->lock);
    out->total_kb     = ((uint64_t)e->sb.s_blocks_count * e->block_size) / 1024ULL;
    out->free_kb      = ((uint64_t)e->sb.s_free_blocks_count * e->block_size) / 1024ULL;
    out->total_inodes = e->sb.s_inodes_count;
    out->free_inodes  = e->sb.s_free_inodes_count;
    out->block_size   = e->block_size;
    mutex_unlock(&e->lock);
    return 0;
}

static const vfs_fs_ops_t ext2_ops = {
    .name     = "ext2",
    .root     = ext2_root,
    .lookup   = ext2_lookup,
    .read     = ext2_read,
    .write    = ext2_write,
    .create   = ext2_create,
    .unlink   = ext2_unlink,
    .truncate = ext2_truncate,
    .readdir  = ext2_readdir,
    .statfs   = ext2_statfs,
};

vfs_fs_t *ext2_mount_ahci(void) {
    int count = ahci_drive_count();
    if (count <= 0) return NULL;

    ext2_priv_t *e = (ext2_priv_t *)kcalloc(1, sizeof(ext2_priv_t));
    if (!e) return NULL;
    mutex_init(&e->lock);

    bool found = false;
    for (int d = 0; d < count; d++) {
        if (!ahci_select_drive(d)) continue;
        if (ahci_read_sectors(2, 2, &e->sb) == 0 && e->sb.s_magic == EXT2_MAGIC) {
            found = true;
            break;
        }
    }
    if (!found) {
        kprintf("[EXT2] No valid ext2 superblock found on %d AHCI drive(s)\n", count);
        kfree(e);
        return NULL;
    }

    e->block_size        = 1024U << e->sb.s_log_block_size;
    e->sectors_per_block = e->block_size / AHCI_SECTOR_SIZE;
    e->inode_size        = (e->sb.s_rev_level >= 1 && e->sb.s_inode_size >= 128) ? e->sb.s_inode_size : 128;
    e->bgdt_block        = e->sb.s_first_data_block + 1;
    e->group_count       = (e->sb.s_blocks_count + e->sb.s_blocks_per_group - 1) / e->sb.s_blocks_per_group;

    uint32_t bg_bytes = e->group_count * sizeof(ext2_bgd_t);
    uint32_t bg_blks  = ALIGN_UP(bg_bytes, e->block_size) / e->block_size;
    e->bgdt = (ext2_bgd_t *)kcalloc(1, bg_blks * e->block_size);
    if (!e->bgdt) { kfree(e); return NULL; }

    uint8_t *dst = (uint8_t *)e->bgdt;
    for (uint32_t i = 0; i < bg_blks; i++) {
        if (read_blk(e, e->bgdt_block + i, dst + i * e->block_size) != 0) {
            kfree(e->bgdt); kfree(e); return NULL;
        }
    }

    vfs_fs_t *fs = (vfs_fs_t *)kcalloc(1, sizeof(vfs_fs_t));
    if (!fs) { kfree(e->bgdt); kfree(e); return NULL; }
    fs->ops  = &ext2_ops;
    fs->priv = e;

    kprintf("[EXT2] Superblock valid: %u blocks (%uB), %u/%u free inodes\n",
            e->sb.s_blocks_count, e->block_size,
            e->sb.s_free_inodes_count, e->sb.s_inodes_count);
    return fs;
}
