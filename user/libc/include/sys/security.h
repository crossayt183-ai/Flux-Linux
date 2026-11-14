#ifndef FLUX_USER_SECURITY_H
#define FLUX_USER_SECURITY_H

#include "flux/types.h"
#include "flux/syscall.h"

#define FLATPAK_PERM_STDIO   (1U << 0)
#define FLATPAK_PERM_WAYLAND (1U << 1)
#define FLATPAK_PERM_NET     (1U << 2)
#define FLATPAK_PERM_FS_HOME (1U << 3)
#define FLATPAK_PERM_AUDIO   (1U << 4)

#define SEC_OP_SET_ENFORCING    1U
#define SEC_OP_TEST_TAMPER      2U
#define SEC_OP_LUKS_RW_TEST     3U
#define SEC_OP_FW_BLOCK_PORT    4U
#define SEC_OP_FW_ALLOW_PORT    5U
#define SEC_OP_FLATPAK_CONFINE  6U
#define SEC_OP_SET_CTX          7U

typedef struct {
    uint64_t pie_base;
    uint32_t relro_pages;
    uint32_t secboot_enforcing;
    uint32_t secboot_verified;
    uint32_t secboot_rejected;
    uint32_t luks2_unlocked;
    uint32_t luks2_sectors_rw;
    uint32_t selinux_enforcing;
    uint32_t avc_allowed;
    uint32_t avc_denied;
    uint32_t fw_zone;
    uint32_t fw_allowed;
    uint32_t fw_dropped;
    uint32_t wl_surfaces;
    uint32_t wl_commits;
    uint32_t flatpak_apps;
    uint32_t sandbox_perms;
    char     sec_ctx[40];
    char     luks_cipher[24];
    char     last_avc[96];
} flux_sec_info_t;

static inline int wl_surface_create(const char *title, int w, int h, void **shm_out) {
    uint64_t uva = 0;
    int id = (int)__syscall4(SYS_WL_CREATE, (uint64_t)(uintptr_t)title,
                             (uint64_t)w, (uint64_t)h, (uint64_t)(uintptr_t)&uva);
    if (id > 0 && shm_out) {
        *shm_out = (void *)(uintptr_t)uva;
    }
    return id;
}

static inline int wl_surface_commit(int surf_id) {
    return (int)__syscall1(SYS_WL_COMMIT, (uint64_t)surf_id);
}

static inline int sec_get_info(flux_sec_info_t *out) {
    return (int)__syscall1(SYS_SEC_INFO, (uint64_t)(uintptr_t)out);
}

static inline int sec_ctl(uint32_t op, uint64_t arg1, uint64_t arg2) {
    return (int)__syscall3(SYS_SEC_CTL, (uint64_t)op, arg1, arg2);
}

#endif
