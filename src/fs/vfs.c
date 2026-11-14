#include "vfs.h"
#include "heap.h"
#include "mutex.h"
#include "string.h"
#include "serial.h"

#define VFS_ICACHE_CAP 64

static vfs_fs_t   *root_fs = NULL;
static vfs_inode_t icache[VFS_ICACHE_CAP];
static uint64_t    icache_clock = 0;
static uint32_t    icache_hits = 0;
static uint32_t    icache_misses = 0;
static mutex_t     vfs_mtx;

void vfs_init(void) {
    mutex_init(&vfs_mtx);
    memset(icache, 0, sizeof(icache));
    root_fs = NULL;
    icache_clock = 0;
    icache_hits = 0;
    icache_misses = 0;
}

bool vfs_mount_root(vfs_fs_t *fs) {
    if (!fs || !fs->ops || !fs->ops->root) return false;
    mutex_lock(&vfs_mtx);
    memset(icache, 0, sizeof(icache));
    root_fs = fs;
    mutex_unlock(&vfs_mtx);
    kprintf("[VFS] Mounted '%s' at '/' (icache=%u slots)\n", fs->ops->name, VFS_ICACHE_CAP);
    return true;
}

bool vfs_is_mounted(void) {
    return root_fs != NULL;
}

vfs_inode_t *vfs_icache_find(vfs_fs_t *fs, uint32_t ino) {
    mutex_lock(&vfs_mtx);
    for (int i = 0; i < VFS_ICACHE_CAP; i++) {
        if (icache[i].ino == ino && icache[i].fs == fs && icache[i].type != VFS_NONE) {
            icache[i].refcount++;
            icache[i].stamp = ++icache_clock;
            icache_hits++;
            mutex_unlock(&vfs_mtx);
            return &icache[i];
        }
    }
    icache_misses++;
    mutex_unlock(&vfs_mtx);
    return NULL;
}

vfs_inode_t *vfs_icache_alloc(vfs_fs_t *fs, uint32_t ino) {
    mutex_lock(&vfs_mtx);
    int empty = -1;
    int lru = -1;
    uint64_t best_stamp = UINT64_MAX;

    for (int i = 0; i < VFS_ICACHE_CAP; i++) {
        if (icache[i].ino == ino && icache[i].fs == fs && icache[i].type != VFS_NONE) {
            icache[i].refcount++;
            icache[i].stamp = ++icache_clock;
            mutex_unlock(&vfs_mtx);
            return &icache[i];
        }
        if (icache[i].type == VFS_NONE && empty < 0) empty = i;
        if (icache[i].refcount == 0 && icache[i].type != VFS_NONE && icache[i].stamp < best_stamp) {
            best_stamp = icache[i].stamp;
            lru = i;
        }
    }

    int slot = (empty >= 0) ? empty : lru;
    if (slot < 0) {
        mutex_unlock(&vfs_mtx);
        return NULL;
    }
    memset(&icache[slot], 0, sizeof(vfs_inode_t));
    icache[slot].ino      = ino;
    icache[slot].fs       = fs;
    icache[slot].refcount = 1;
    icache[slot].stamp    = ++icache_clock;
    mutex_unlock(&vfs_mtx);
    return &icache[slot];
}

void vfs_icache_drop(vfs_fs_t *fs, uint32_t ino) {
    mutex_lock(&vfs_mtx);
    for (int i = 0; i < VFS_ICACHE_CAP; i++) {
        if (icache[i].fs == fs && icache[i].ino == ino) {
            memset(&icache[i], 0, sizeof(vfs_inode_t));
            break;
        }
    }
    mutex_unlock(&vfs_mtx);
}

void vfs_inode_ref(vfs_inode_t *node) {
    if (!node) return;
    mutex_lock(&vfs_mtx);
    node->refcount++;
    node->stamp = ++icache_clock;
    mutex_unlock(&vfs_mtx);
}

void vfs_inode_put(vfs_inode_t *node) {
    if (!node) return;
    mutex_lock(&vfs_mtx);
    if (node->refcount > 0) node->refcount--;
    mutex_unlock(&vfs_mtx);
}

static bool split_parent(const char *path, vfs_inode_t **out_parent, char *out_leaf) {
    if (!root_fs || !path || path[0] != '/') return false;

    const char *p = path;
    while (*p == '/') p++;
    if (*p == '\0') return false;

    vfs_inode_t *cur = root_fs->ops->root(root_fs);
    if (!cur) return false;

    char seg[VFS_MAX_NAME];
    while (*p) {
        size_t len = 0;
        while (p[len] && p[len] != '/') {
            if (len + 1 < sizeof(seg)) seg[len] = p[len];
            len++;
        }
        size_t clen = MIN(len, sizeof(seg) - 1);
        seg[clen] = '\0';
        p += len;
        while (*p == '/') p++;

        if (*p == '\0') {
            if (clen == 0) { vfs_inode_put(cur); return false; }
            strcpy(out_leaf, seg);
            *out_parent = cur;
            return true;
        }

        if (cur->type != VFS_DIR) {
            vfs_inode_put(cur);
            return false;
        }
        vfs_inode_t *next = root_fs->ops->lookup(root_fs, cur, seg);
        vfs_inode_put(cur);
        if (!next) return false;
        cur = next;
    }

    vfs_inode_put(cur);
    return false;
}

vfs_inode_t *vfs_lookup(const char *path) {
    if (!root_fs || !path || path[0] != '/') return NULL;

    const char *p = path;
    while (*p == '/') p++;
    vfs_inode_t *cur = root_fs->ops->root(root_fs);
    if (!cur) return NULL;
    if (*p == '\0') return cur;

    char seg[VFS_MAX_NAME];
    while (*p) {
        size_t len = 0;
        while (p[len] && p[len] != '/') {
            if (len + 1 < sizeof(seg)) seg[len] = p[len];
            len++;
        }
        seg[MIN(len, sizeof(seg) - 1)] = '\0';
        p += len;
        while (*p == '/') p++;

        if (cur->type != VFS_DIR) {
            vfs_inode_put(cur);
            return NULL;
        }
        vfs_inode_t *next = root_fs->ops->lookup(root_fs, cur, seg);
        vfs_inode_put(cur);
        if (!next) return NULL;
        cur = next;
    }
    return cur;
}

vfs_inode_t *vfs_open_path(const char *path, bool create, bool trunc, bool *is_new) {
    if (is_new) *is_new = false;
    vfs_inode_t *node = vfs_lookup(path);
    if (node) {
        if (trunc && node->type == VFS_FILE && node->fs->ops->truncate) {
            node->fs->ops->truncate(node->fs, node, 0);
        }
        return node;
    }
    if (!create || !root_fs || !root_fs->ops->create) return NULL;

    vfs_inode_t *parent = NULL;
    char leaf[VFS_MAX_NAME];
    if (!split_parent(path, &parent, leaf)) return NULL;

    node = root_fs->ops->create(root_fs, parent, leaf, VFS_FILE);
    vfs_inode_put(parent);
    if (node && is_new) *is_new = true;
    return node;
}

int64_t vfs_read(vfs_inode_t *node, uint64_t off, void *buf, size_t len) {
    if (!node || !node->fs || !node->fs->ops->read || !buf) return -1;
    if (node->type != VFS_FILE) return -1;
    return node->fs->ops->read(node->fs, node, off, buf, len);
}

int64_t vfs_write(vfs_inode_t *node, uint64_t off, const void *buf, size_t len) {
    if (!node || !node->fs || !node->fs->ops->write || !buf) return -1;
    if (node->type != VFS_FILE) return -1;
    return node->fs->ops->write(node->fs, node, off, buf, len);
}

int vfs_mkdir(const char *path) {
    if (!root_fs || !root_fs->ops->create) return -1;
    vfs_inode_t *exist = vfs_lookup(path);
    if (exist) { vfs_inode_put(exist); return -1; }

    vfs_inode_t *parent = NULL;
    char leaf[VFS_MAX_NAME];
    if (!split_parent(path, &parent, leaf)) return -1;

    vfs_inode_t *created = root_fs->ops->create(root_fs, parent, leaf, VFS_DIR);
    vfs_inode_put(parent);
    if (!created) return -1;
    vfs_inode_put(created);
    return 0;
}

int vfs_unlink(const char *path) {
    if (!root_fs || !root_fs->ops->unlink) return -1;
    vfs_inode_t *parent = NULL;
    char leaf[VFS_MAX_NAME];
    if (!split_parent(path, &parent, leaf)) return -1;

    int rc = root_fs->ops->unlink(root_fs, parent, leaf);
    vfs_inode_put(parent);
    return rc;
}

int vfs_readdir(const char *path, vfs_dirent_t *out, size_t max_entries) {
    if (!root_fs || !root_fs->ops->readdir || !out || max_entries == 0) return -1;
    vfs_inode_t *dir = vfs_lookup(path);
    if (!dir) return -1;
    if (dir->type != VFS_DIR) {
        vfs_inode_put(dir);
        return -1;
    }
    int n = root_fs->ops->readdir(root_fs, dir, out, max_entries);
    vfs_inode_put(dir);
    return n;
}

int vfs_statfs(vfs_statfs_t *out) {
    if (!root_fs || !root_fs->ops->statfs || !out) return -1;
    int rc = root_fs->ops->statfs(root_fs, out);
    if (rc == 0) {
        out->cache_hits = icache_hits;
        out->cache_misses = icache_misses;
    }
    return rc;
}

bool vfs_read_all(const char *path, uint8_t **out_buf, size_t *out_size) {
    if (!out_buf || !out_size) return false;
    vfs_inode_t *node = vfs_lookup(path);
    if (!node || node->type != VFS_FILE || node->size == 0 || node->size > (4ULL << 20)) {
        if (node) vfs_inode_put(node);
        return false;
    }

    size_t sz = (size_t)node->size;
    uint8_t *buf = (uint8_t *)kmalloc(sz + 1);
    if (!buf) {
        vfs_inode_put(node);
        return false;
    }

    size_t total = 0;
    while (total < sz) {
        int64_t r = vfs_read(node, total, buf + total, sz - total);
        if (r <= 0) break;
        total += (size_t)r;
    }
    vfs_inode_put(node);

    if (total != sz) {
        kfree(buf);
        return false;
    }
    buf[sz] = 0;
    *out_buf = buf;
    *out_size = sz;
    return true;
}
