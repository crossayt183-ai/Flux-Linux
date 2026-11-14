#ifndef FLUX_VFS_H
#define FLUX_VFS_H

#include "types.h"

#define VFS_MAX_NAME 64
#define VFS_MAX_PATH 192

typedef enum {
    VFS_NONE = 0,
    VFS_FILE = 1,
    VFS_DIR  = 2,
    VFS_CHAR = 3
} vfs_type_t;

struct vfs_fs;

typedef struct vfs_inode {
    uint32_t       ino;
    vfs_type_t     type;
    uint32_t       mode;
    uint64_t       size;
    uint32_t       links;
    uint32_t       refcount;
    uint64_t       stamp;
    struct vfs_fs *fs;
    uint64_t       fs_data[4];
} vfs_inode_t;

typedef struct {
    char       name[VFS_MAX_NAME];
    uint32_t   ino;
    vfs_type_t type;
    uint64_t   size;
} vfs_dirent_t;

typedef struct {
    uint64_t total_kb;
    uint64_t free_kb;
    uint32_t total_inodes;
    uint32_t free_inodes;
    uint32_t block_size;
    uint32_t cache_hits;
    uint32_t cache_misses;
} vfs_statfs_t;

typedef struct vfs_fs_ops {
    const char *name;
    vfs_inode_t *(*root)(struct vfs_fs *fs);
    vfs_inode_t *(*lookup)(struct vfs_fs *fs, vfs_inode_t *dir, const char *name);
    int64_t      (*read)(struct vfs_fs *fs, vfs_inode_t *node, uint64_t off, void *buf, size_t len);
    int64_t      (*write)(struct vfs_fs *fs, vfs_inode_t *node, uint64_t off, const void *buf, size_t len);
    vfs_inode_t *(*create)(struct vfs_fs *fs, vfs_inode_t *dir, const char *name, vfs_type_t type);
    int          (*unlink)(struct vfs_fs *fs, vfs_inode_t *dir, const char *name);
    int          (*truncate)(struct vfs_fs *fs, vfs_inode_t *node, uint64_t new_size);
    int          (*readdir)(struct vfs_fs *fs, vfs_inode_t *dir, vfs_dirent_t *out, size_t max_entries);
    int          (*statfs)(struct vfs_fs *fs, vfs_statfs_t *out);
} vfs_fs_ops_t;

typedef struct vfs_fs {
    const vfs_fs_ops_t *ops;
    void               *priv;
} vfs_fs_t;

void         vfs_init(void);
bool         vfs_mount_root(vfs_fs_t *fs);
bool         vfs_is_mounted(void);

vfs_inode_t *vfs_icache_find(vfs_fs_t *fs, uint32_t ino);
vfs_inode_t *vfs_icache_alloc(vfs_fs_t *fs, uint32_t ino);
void         vfs_icache_drop(vfs_fs_t *fs, uint32_t ino);
void         vfs_inode_ref(vfs_inode_t *node);
void         vfs_inode_put(vfs_inode_t *node);

vfs_inode_t *vfs_lookup(const char *path);
vfs_inode_t *vfs_open_path(const char *path, bool create, bool trunc, bool *is_new);
int64_t      vfs_read(vfs_inode_t *node, uint64_t off, void *buf, size_t len);
int64_t      vfs_write(vfs_inode_t *node, uint64_t off, const void *buf, size_t len);
int          vfs_mkdir(const char *path);
int          vfs_unlink(const char *path);
int          vfs_readdir(const char *path, vfs_dirent_t *out, size_t max_entries);
int          vfs_statfs(vfs_statfs_t *out);
bool         vfs_read_all(const char *path, uint8_t **out_buf, size_t *out_size);

#endif
