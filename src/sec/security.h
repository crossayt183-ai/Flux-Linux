#ifndef FLUX_SECURITY_H
#define FLUX_SECURITY_H

#include "types.h"
#include "task.h"

/* Flatpak / Sandbox capability bits */
#define FLATPAK_PERM_STDIO   (1U << 0)
#define FLATPAK_PERM_WAYLAND (1U << 1)
#define FLATPAK_PERM_NET     (1U << 2)
#define FLATPAK_PERM_FS_HOME (1U << 3)
#define FLATPAK_PERM_AUDIO   (1U << 4)

/* Security control opcodes for SYS_SEC_CTL (syscall 43) */
#define SEC_OP_SET_ENFORCING    1U
#define SEC_OP_TEST_TAMPER      2U
#define SEC_OP_LUKS_RW_TEST     3U
#define SEC_OP_FW_BLOCK_PORT    4U
#define SEC_OP_FW_ALLOW_PORT    5U
#define SEC_OP_FLATPAK_CONFINE  6U
#define SEC_OP_SET_CTX          7U

/* Shared ABI struct between kernel and Ring 3 (SYS_SEC_INFO = 42) */
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

/* On-disk / volume header representing LUKS2 metadata */
typedef struct PACKED {
    uint8_t  magic[6];          /* "LUKS\xba\xbe" */
    uint16_t version;           /* 2 */
    uint64_t hdr_size;          /* 4096 */
    uint64_t seqid;
    char     label[48];
    char     checksum_alg[32];
    uint8_t  salt[32];
    char     uuid[40];
    char     cipher[32];
    uint8_t  mk_digest[32];
    char     json_meta[512];
    uint8_t  reserved[3322];
} luks2_phdr_t;

typedef struct {
    const char *app_id;
    const char *name;
    const char *exec_path;
    const char *version;
    const char *runtime;
    uint32_t    perms;
} flatpak_app_t;

void security_init(void);

/* FIPS 180-4 SHA-256 + HMAC-SHA256 */
void sha256_compute(const void *data, size_t len, uint8_t out32[32]);
void hmac_sha256(const uint8_t *key, size_t key_len,
                 const void *data, size_t data_len, uint8_t out32[32]);

/* SecureBoot */
int  secboot_verify_elf(const char *path, const uint8_t *image, size_t size);
bool secboot_selftest_tamper(void);
bool secboot_is_enforcing(void);
void secboot_stats(uint32_t *verified_out, uint32_t *rejected_out);

/* LUKS2 encrypted volume */
bool luks2_format(const char *passphrase);
bool luks2_unlock(const char *passphrase);
void luks2_lock(void);
bool luks2_is_unlocked(void);
bool luks2_write_sector(uint32_t sec_idx, const uint8_t *plain512);
bool luks2_read_sector(uint32_t sec_idx, uint8_t *plain512_out);
bool luks2_read_raw_sector(uint32_t sec_idx, uint8_t *cipher512_out);
bool luks2_selftest_rw(void);
const luks2_phdr_t *luks2_get_header(void);

/* SELinux MAC + AVC */
bool selinux_is_enforcing(void);
void selinux_set_enforcing(bool enforcing);
int  selinux_check_fs(task_t *t, const char *path, bool is_write, bool is_exec);
int  selinux_check_net(task_t *t, uint16_t port, bool is_bind);
void selinux_set_task_ctx(task_t *t, const char *ctx, uint32_t perms);
void selinux_stats(uint32_t *allowed_out, uint32_t *denied_out, char *last_log, size_t cap);

/* Flatpak runtime & manifests */
size_t               flatpak_count(void);
const flatpak_app_t *flatpak_get(size_t idx);
const flatpak_app_t *flatpak_find(const char *app_id);
bool                 flatpak_spawn(const char *app_id, int32_t *exit_code_out);

/* Aggregated security snapshot */
void security_collect_info(task_t *t, flux_sec_info_t *out);

#endif
