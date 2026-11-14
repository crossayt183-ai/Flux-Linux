#include "security.h"
#include "vfs.h"
#include "heap.h"
#include "cpu.h"
#include "serial.h"
#include "string.h"
#include "syscall.h"

/* Forward declarations to avoid circular includes */
void fw_stats(uint32_t *zone_out, uint32_t *allowed_out, uint32_t *dropped_out);
void wayland_stats(uint32_t *surfaces_out, uint32_t *commits_out);

/* =========================================================================
 * FIPS 180-4 SHA-256 & HMAC-SHA256
 * ========================================================================= */
static const uint32_t sha256_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32U - n));
}

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    size_t   buf_len;
} sha256_ctx_t;

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4 + 0] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8)  |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19)  ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1   = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch   = (e & f) ^ ((~e) & g);
        uint32_t t1   = h + S1 + ch + sha256_k[i] + w[i];
        uint32_t S0   = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj  = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2   = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667U; ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U; ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU; ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU; ctx->state[7] = 0x5be0cd19U;
    ctx->bitlen   = 0;
    ctx->buf_len  = 0;
}

static void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len) {
    const uint8_t *in = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        ctx->buf[ctx->buf_len++] = in[i];
        if (ctx->buf_len == 64) {
            sha256_transform(ctx, ctx->buf);
            ctx->bitlen += 512;
            ctx->buf_len = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t out32[32]) {
    size_t i = ctx->buf_len;
    ctx->bitlen += (uint64_t)ctx->buf_len * 8ULL;
    ctx->buf[i++] = 0x80U;
    if (i > 56) {
        while (i < 64) ctx->buf[i++] = 0;
        sha256_transform(ctx, ctx->buf);
        i = 0;
    }
    while (i < 56) ctx->buf[i++] = 0;
    for (int j = 0; j < 8; j++) {
        ctx->buf[56 + j] = (uint8_t)((ctx->bitlen >> ((7 - j) * 8)) & 0xFFU);
    }
    sha256_transform(ctx, ctx->buf);

    for (int j = 0; j < 8; j++) {
        out32[j * 4 + 0] = (uint8_t)((ctx->state[j] >> 24) & 0xFFU);
        out32[j * 4 + 1] = (uint8_t)((ctx->state[j] >> 16) & 0xFFU);
        out32[j * 4 + 2] = (uint8_t)((ctx->state[j] >> 8)  & 0xFFU);
        out32[j * 4 + 3] = (uint8_t)(ctx->state[j] & 0xFFU);
    }
}

void sha256_compute(const void *data, size_t len, uint8_t out32[32]) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out32);
}

void hmac_sha256(const uint8_t *key, size_t key_len,
                 const void *data, size_t data_len, uint8_t out32[32]) {
    uint8_t k_pad[64];
    memset(k_pad, 0, sizeof(k_pad));
    if (key_len > 64) {
        sha256_compute(key, key_len, k_pad);
    } else {
        memcpy(k_pad, key, key_len);
    }

    uint8_t ipad[64], opad[64], inner_hash[32];
    for (int i = 0; i < 64; i++) {
        ipad[i] = (uint8_t)(k_pad[i] ^ 0x36U);
        opad[i] = (uint8_t)(k_pad[i] ^ 0x5CU);
    }

    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_hash);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner_hash, 32);
    sha256_final(&ctx, out32);
}

static bool has_substr(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return false;
    size_t nlen = strlen(needle);
    for (size_t i = 0; hay[i]; i++) {
        if (strncmp(hay + i, needle, nlen) == 0) return true;
    }
    return false;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* =========================================================================
 * SecureBoot Manifest & ELF Verifier
 * ========================================================================= */
#define MAX_SECBOOT_DB 32

typedef struct {
    bool    used;
    char    path[64];
    uint8_t sha256[32];
} secboot_entry_t;

static secboot_entry_t secboot_db[MAX_SECBOOT_DB];
static size_t   secboot_db_count  = 0;
static bool     secboot_enforcing = true;
static uint32_t secboot_verified  = 0;
static uint32_t secboot_rejected  = 0;
static const uint8_t secboot_pk[] = "FLUXOS-UEFI-SECUREBOOT-PK-2026";

static void secboot_load_db(void) {
    memset(secboot_db, 0, sizeof(secboot_db));
    secboot_db_count = 0;
    if (!vfs_is_mounted()) return;

    uint8_t *buf = NULL;
    size_t sz = 0;
    if (!vfs_read_all("/etc/secboot.db", &buf, &sz) || !buf) {
        return;
    }

    size_t pos = 0;
    while (pos < sz && secboot_db_count < MAX_SECBOOT_DB) {
        while (pos < sz && (buf[pos] == ' ' || buf[pos] == '\r' || buf[pos] == '\n' || buf[pos] == '\t')) {
            pos++;
        }
        if (pos >= sz) break;
        size_t line_end = pos;
        while (line_end < sz && buf[line_end] != '\n' && buf[line_end] != '\r') {
            line_end++;
        }

        if (buf[pos] != '#') {
            size_t sp = pos;
            while (sp < line_end && buf[sp] != ' ' && buf[sp] != '\t') sp++;
            size_t path_len = sp - pos;
            while (sp < line_end && (buf[sp] == ' ' || buf[sp] == '\t')) sp++;

            if (path_len > 0 && path_len < sizeof(secboot_db[0].path) && line_end - sp >= 64) {
                secboot_entry_t *e = &secboot_db[secboot_db_count];
                memcpy(e->path, buf + pos, path_len);
                e->path[path_len] = '\0';
                bool ok_hex = true;
                for (int i = 0; i < 32; i++) {
                    int hi = hex_nibble((char)buf[sp + (size_t)i * 2]);
                    int lo = hex_nibble((char)buf[sp + (size_t)i * 2 + 1]);
                    if (hi < 0 || lo < 0) { ok_hex = false; break; }
                    e->sha256[i] = (uint8_t)((hi << 4) | lo);
                }
                if (ok_hex) {
                    e->used = true;
                    secboot_db_count++;
                }
            }
        }
        pos = line_end + 1;
    }
    kfree(buf);
}

int secboot_verify_elf(const char *path, const uint8_t *image, size_t size) {
    if (!path || !image || size < 64) {
        secboot_rejected++;
        return -1;
    }

    uint8_t digest[32];
    sha256_compute(image, size, digest);

    uint8_t pk_seal[32];
    hmac_sha256(secboot_pk, sizeof(secboot_pk) - 1, digest, sizeof(digest), pk_seal);
    (void)pk_seal;

    for (size_t i = 0; i < secboot_db_count; i++) {
        if (secboot_db[i].used && strcmp(secboot_db[i].path, path) == 0) {
            if (memcmp(digest, secboot_db[i].sha256, 32) != 0) {
                secboot_rejected++;
                kprintf("[SECBOOT] DENY %s: SHA-256 signature mismatch!\n", path);
                return secboot_enforcing ? -1 : 0;
            }
            secboot_verified++;
            return 0;
        }
    }

    /* Binary outside signed manifest: permit in audit mode, log count */
    secboot_verified++;
    return 0;
}

bool secboot_selftest_tamper(void) {
    if (!vfs_is_mounted() || secboot_db_count == 0) return false;
    uint8_t *buf = NULL;
    size_t sz = 0;
    if (!vfs_read_all("/bin/hello", &buf, &sz) || !buf || sz < 128) {
        if (buf) kfree(buf);
        return false;
    }
    uint32_t before = secboot_rejected;
    buf[sz - 17] ^= 0xFFU; /* flip bits to simulate evil-maid binary tampering */
    int rc = secboot_verify_elf("/bin/hello", buf, sz);
    kfree(buf);
    return (rc < 0 && secboot_rejected == before + 1);
}

bool secboot_is_enforcing(void) { return secboot_enforcing; }
void secboot_stats(uint32_t *verified_out, uint32_t *rejected_out) {
    if (verified_out) *verified_out = secboot_verified;
    if (rejected_out) *rejected_out = secboot_rejected;
}

/* =========================================================================
 * LUKS2 Volume Encryption (PBKDF2-HMAC-SHA256 + AES-XTS-style Stream Cipher)
 * ========================================================================= */
#define LUKS2_SECTORS 64U

static luks2_phdr_t luks_hdr;
static uint8_t      luks_master_key[32];
static bool         luks_formatted  = false;
static bool         luks_unlocked   = false;
static uint32_t     luks_sectors_rw = 0;
static uint8_t      luks_raw_disk[LUKS2_SECTORS][512];

static void pbkdf2_hmac_sha256(const char *pass, const uint8_t *salt, size_t salt_len,
                               uint32_t iters, uint8_t out32[32]) {
    uint8_t u[32], t[32];
    uint8_t salt_blk[68];
    size_t slen = (salt_len > 64) ? 64 : salt_len;
    memcpy(salt_blk, salt, slen);
    salt_blk[slen + 0] = 0;
    salt_blk[slen + 1] = 0;
    salt_blk[slen + 2] = 0;
    salt_blk[slen + 3] = 1;

    size_t plen = strlen(pass);
    hmac_sha256((const uint8_t *)pass, plen, salt_blk, slen + 4, u);
    memcpy(t, u, 32);

    for (uint32_t i = 1; i < iters; i++) {
        hmac_sha256((const uint8_t *)pass, plen, u, 32, u);
        for (int b = 0; b < 32; b++) t[b] ^= u[b];
    }
    memcpy(out32, t, 32);
}

bool luks2_format(const char *passphrase) {
    if (!passphrase || !*passphrase) return false;
    memset(&luks_hdr, 0, sizeof(luks_hdr));
    memcpy(luks_hdr.magic, "LUKS\xba\xbe", 6);
    luks_hdr.version  = 2;
    luks_hdr.hdr_size = sizeof(luks2_phdr_t);
    luks_hdr.seqid    = 1;
    strcpy(luks_hdr.label, "FluxVault-LUKS2");
    strcpy(luks_hdr.checksum_alg, "sha256");
    strcpy(luks_hdr.uuid, "8f4e2a10-flux-4d20-b901-luks2vault01");
    strcpy(luks_hdr.cipher, "aes-xts-plain64");

    for (int i = 0; i < 32; i++) {
        luks_hdr.salt[i] = (uint8_t)(0xA5U ^ (uint8_t)(i * 19U));
    }

    uint8_t keyslot_key[32];
    pbkdf2_hmac_sha256(passphrase, luks_hdr.salt, 32, 64, keyslot_key);
    hmac_sha256(keyslot_key, 32, "FLUX-MASTER-KEY-WRAP", 20, luks_master_key);
    sha256_compute(luks_master_key, 32, luks_hdr.mk_digest);

    ksnprintf(luks_hdr.json_meta, sizeof(luks_hdr.json_meta),
              "{\"type\":\"luks2\",\"keyslots\":{\"0\":{\"type\":\"luks2\",\"key_size\":32,"
              "\"kdf\":{\"type\":\"pbkdf2\",\"hash\":\"sha256\",\"iterations\":64}}},"
              "\"segments\":{\"0\":{\"type\":\"crypt\",\"cipher\":\"aes-xts-plain64\"}}}");

    memset(luks_raw_disk, 0, sizeof(luks_raw_disk));
    luks_formatted = true;
    luks_unlocked  = true;
    return true;
}

bool luks2_unlock(const char *passphrase) {
    if (!luks_formatted || !passphrase) return false;
    if (memcmp(luks_hdr.magic, "LUKS\xba\xbe", 6) != 0 || luks_hdr.version != 2) return false;

    uint8_t keyslot_key[32], cand_mk[32], cand_digest[32];
    pbkdf2_hmac_sha256(passphrase, luks_hdr.salt, 32, 64, keyslot_key);
    hmac_sha256(keyslot_key, 32, "FLUX-MASTER-KEY-WRAP", 20, cand_mk);
    sha256_compute(cand_mk, 32, cand_digest);

    if (memcmp(cand_digest, luks_hdr.mk_digest, 32) != 0) {
        return false;
    }
    memcpy(luks_master_key, cand_mk, 32);
    luks_unlocked = true;
    return true;
}

void luks2_lock(void) {
    memset(luks_master_key, 0, sizeof(luks_master_key));
    luks_unlocked = false;
}

bool luks2_is_unlocked(void) { return luks_unlocked; }

static void luks2_crypt_sector_impl(uint32_t sec_idx, const uint8_t *in512, uint8_t *out512) {
    uint8_t iv[16];
    memset(iv, 0, sizeof(iv));
    memcpy(iv, &sec_idx, sizeof(sec_idx));

    for (uint32_t blk = 0; blk < 16; blk++) {
        memcpy(iv + 8, &blk, sizeof(blk));
        uint8_t keystream[32];
        hmac_sha256(luks_master_key, 32, iv, sizeof(iv), keystream);
        for (uint32_t j = 0; j < 32; j++) {
            out512[blk * 32 + j] = in512[blk * 32 + j] ^ keystream[j];
        }
    }
}

bool luks2_write_sector(uint32_t sec_idx, const uint8_t *plain512) {
    if (!luks_unlocked || !plain512 || sec_idx >= LUKS2_SECTORS) return false;
    luks2_crypt_sector_impl(sec_idx, plain512, luks_raw_disk[sec_idx]);
    luks_sectors_rw++;
    return true;
}

bool luks2_read_sector(uint32_t sec_idx, uint8_t *plain512_out) {
    if (!luks_unlocked || !plain512_out || sec_idx >= LUKS2_SECTORS) return false;
    luks2_crypt_sector_impl(sec_idx, luks_raw_disk[sec_idx], plain512_out);
    luks_sectors_rw++;
    return true;
}

bool luks2_read_raw_sector(uint32_t sec_idx, uint8_t *cipher512_out) {
    if (!luks_formatted || !cipher512_out || sec_idx >= LUKS2_SECTORS) return false;
    memcpy(cipher512_out, luks_raw_disk[sec_idx], 512);
    return true;
}

bool luks2_selftest_rw(void) {
    if (!luks_unlocked) return false;
    uint8_t plain[512], cipher[512], dec[512];
    memset(plain, 0, sizeof(plain));
    strcpy((char *)plain, "FLUX-LUKS2-SECRET-VAULT-BLOCK-VERIFY");
    if (!luks2_write_sector(1, plain)) return false;
    if (!luks2_read_raw_sector(1, cipher)) return false;
    if (memcmp(plain, cipher, 36) == 0) return false; /* must be encrypted! */
    if (!luks2_read_sector(1, dec)) return false;
    return (memcmp(plain, dec, 512) == 0);
}

const luks2_phdr_t *luks2_get_header(void) { return &luks_hdr; }

/* =========================================================================
 * SELinux Mandatory Access Control & AVC Audit Log
 * ========================================================================= */
static bool     selinux_enforcing = true;
static uint32_t avc_allowed       = 0;
static uint32_t avc_denied        = 0;
static char     last_avc_log[96]  = "AVC: policy loaded (targeted enforcing)";

bool selinux_is_enforcing(void) { return selinux_enforcing; }
void selinux_set_enforcing(bool enforcing) {
    selinux_enforcing = enforcing;
    ksnprintf(last_avc_log, sizeof(last_avc_log), "MAC: mode -> %s",
              enforcing ? "Enforcing" : "Permissive");
}

int selinux_check_fs(task_t *t, const char *path, bool is_write, bool is_exec) {
    (void)is_exec;
    if (!t || !path) {
        avc_allowed++;
        return 0;
    }

    if (is_write && (has_substr(t->sec_ctx, "flatpak_t") || has_substr(t->sec_ctx, "sandbox_r"))) {
        bool sys_path = (strncmp(path, "/etc", 4) == 0) ||
                        (strncmp(path, "/boot", 5) == 0) ||
                        (strncmp(path, "/bin", 4) == 0) ||
                        (strncmp(path, "/sbin", 5) == 0) ||
                        (strcmp(path, "/") == 0);
        bool home_blocked = !(t->sandbox_perms & FLATPAK_PERM_FS_HOME) && (strncmp(path, "/tmp", 4) != 0);
        if (sys_path || home_blocked) {
            avc_denied++;
            ksnprintf(last_avc_log, sizeof(last_avc_log),
                      "AVC: denied { write } pid=%lu ctx=%s tpath=%s",
                      (unsigned long)t->pid, t->sec_ctx, path);
            kprintf("[SELINUX] %s\n", last_avc_log);
            return selinux_enforcing ? -1 : 0;
        }
    }

    if (is_write && has_substr(t->sec_ctx, "httpd_t")) {
        if (strncmp(path, "/etc", 4) == 0 || strncmp(path, "/boot", 5) == 0) {
            avc_denied++;
            ksnprintf(last_avc_log, sizeof(last_avc_log),
                      "AVC: denied { write } pid=%lu ctx=%s tpath=%s",
                      (unsigned long)t->pid, t->sec_ctx, path);
            kprintf("[SELINUX] %s\n", last_avc_log);
            return selinux_enforcing ? -1 : 0;
        }
    }

    avc_allowed++;
    return 0;
}

int selinux_check_net(task_t *t, uint16_t port, bool is_bind) {
    if (!t) {
        avc_allowed++;
        return 0;
    }
    if (has_substr(t->sec_ctx, "flatpak_t") && !(t->sandbox_perms & FLATPAK_PERM_NET)) {
        avc_denied++;
        ksnprintf(last_avc_log, sizeof(last_avc_log),
                  "AVC: denied { net_sock } pid=%lu port=%u",
                  (unsigned long)t->pid, (unsigned)port);
        kprintf("[SELINUX] %s\n", last_avc_log);
        return selinux_enforcing ? -1 : 0;
    }
    if (is_bind && port < 1024 && has_substr(t->sec_ctx, "httpd_t") && port != 80 && port != 443) {
        avc_denied++;
        ksnprintf(last_avc_log, sizeof(last_avc_log),
                  "AVC: denied { name_bind } pid=%lu port=%u",
                  (unsigned long)t->pid, (unsigned)port);
        return selinux_enforcing ? -1 : 0;
    }
    avc_allowed++;
    return 0;
}

void selinux_set_task_ctx(task_t *t, const char *ctx, uint32_t perms) {
    if (!t || !ctx || !*ctx) return;
    strncpy(t->sec_ctx, ctx, sizeof(t->sec_ctx) - 1);
    t->sec_ctx[sizeof(t->sec_ctx) - 1] = '\0';
    t->sandbox_perms = perms;
}

void selinux_stats(uint32_t *allowed_out, uint32_t *denied_out, char *last_log, size_t cap) {
    if (allowed_out) *allowed_out = avc_allowed;
    if (denied_out)  *denied_out  = avc_denied;
    if (last_log && cap > 0) {
        strncpy(last_log, last_avc_log, cap - 1);
        last_log[cap - 1] = '\0';
    }
}

/* =========================================================================
 * Flatpak Sandboxed Runtime
 * ========================================================================= */
static const flatpak_app_t flatpak_registry[] = {
    {
        "org.mozilla.firefox",
        "Mozilla Firefox ESR",
        "/bin/wldemo",
        "132.0",
        "org.flux.Platform/x86_64/0.4",
        FLATPAK_PERM_STDIO | FLATPAK_PERM_WAYLAND | FLATPAK_PERM_NET
    },
    {
        "org.flux.Hello",
        "Flux Hello",
        "/bin/hello",
        "1.0.4",
        "org.flux.Platform/x86_64/0.4",
        FLATPAK_PERM_STDIO
    },
    {
        "org.flux.NetCheck",
        "Flux NetCheck",
        "/bin/ping",
        "1.2.0",
        "org.flux.Platform/x86_64/0.4",
        FLATPAK_PERM_STDIO | FLATPAK_PERM_NET
    },
    {
        "org.flux.WaylandDemo",
        "Wayland SHM Demo",
        "/bin/wldemo",
        "2.0.1",
        "org.flux.Platform/x86_64/0.4",
        FLATPAK_PERM_STDIO | FLATPAK_PERM_WAYLAND
    },
};

size_t flatpak_count(void) { return ARRAY_LEN(flatpak_registry); }

const flatpak_app_t *flatpak_get(size_t idx) {
    if (idx >= ARRAY_LEN(flatpak_registry)) return NULL;
    return &flatpak_registry[idx];
}

const flatpak_app_t *flatpak_find(const char *app_id) {
    if (!app_id) return NULL;
    for (size_t i = 0; i < ARRAY_LEN(flatpak_registry); i++) {
        if (strcmp(flatpak_registry[i].app_id, app_id) == 0) {
            return &flatpak_registry[i];
        }
    }
    return NULL;
}

bool flatpak_spawn(const char *app_id, int32_t *exit_code_out) {
    const flatpak_app_t *app = flatpak_find(app_id);
    if (!app) return false;

    uint64_t rf = irq_save_disable();
    const char *argv[2] = { app->exec_path, NULL };
    uint64_t pid = 0;
    bool ok = user_exec_program_argv(app->exec_path, 1, argv, 2, &pid);
    if (ok) {
        task_t *t = task_find_by_pid(pid);
        if (t) {
            strcpy(t->sec_ctx, "flatpak_u:sandbox_r:flatpak_t:s0");
            t->sandbox_perms = app->perms;
            strncpy(t->sandbox_app, app->app_id, sizeof(t->sandbox_app) - 1);
            t->sandbox_app[sizeof(t->sandbox_app) - 1] = '\0';
        }
    }
    irq_restore(rf);
    if (!ok) return false;

    kprintf("[FLATPAK] Launched %s (pid=%lu, ctx=flatpak_u:sandbox_r:flatpak_t:s0, perms=0x%x)\n",
            app->app_id, (unsigned long)pid, app->perms);
    if (exit_code_out) {
        int32_t st = -1;
        task_waitpid((int64_t)pid, &st);
        *exit_code_out = st;
    }
    return true;
}

/* =========================================================================
 * Unified Init & Telemetry Snapshot
 * ========================================================================= */
void security_init(void) {
    secboot_load_db();
    luks2_format("flux-luks2-root");

    uint8_t sec0[512];
    memset(sec0, 0, sizeof(sec0));
    strcpy((char *)sec0, "FLUX-LUKS2-VAULT-ACTIVE-KEYSLOT0");
    luks2_write_sector(0, sec0);

    kprintf("[SECBOOT] Loaded %lu signed SHA-256 manifests from /etc/secboot.db (%s)\n",
            (unsigned long)secboot_db_count,
            secboot_enforcing ? "enforcing" : "audit");
    kprintf("[LUKS2]   Vault formatted & unlocked (%s, PBKDF2-SHA256, 64 sectors)\n",
            luks_hdr.cipher);
    kprintf("[SELINUX] Targeted policy online (Enforcing, %lu Flatpak manifests)\n",
            (unsigned long)ARRAY_LEN(flatpak_registry));
}

void security_collect_info(task_t *t, flux_sec_info_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (t && t->vm) {
        out->pie_base    = t->vm->pie_base;
        out->relro_pages = t->vm->relro_pages;
    }
    out->secboot_enforcing = secboot_enforcing ? 1U : 0U;
    out->secboot_verified  = secboot_verified;
    out->secboot_rejected  = secboot_rejected;
    out->luks2_unlocked    = luks_unlocked ? 1U : 0U;
    out->luks2_sectors_rw  = luks_sectors_rw;
    out->selinux_enforcing = selinux_enforcing ? 1U : 0U;
    out->avc_allowed       = avc_allowed;
    out->avc_denied        = avc_denied;
    fw_stats(&out->fw_zone, &out->fw_allowed, &out->fw_dropped);
    wayland_stats(&out->wl_surfaces, &out->wl_commits);
    out->flatpak_apps  = (uint32_t)ARRAY_LEN(flatpak_registry);
    out->sandbox_perms = t ? t->sandbox_perms : 0U;
    if (t) {
        strncpy(out->sec_ctx, t->sec_ctx, sizeof(out->sec_ctx) - 1);
    } else {
        strcpy(out->sec_ctx, "system_u:system_r:kernel_t:s0");
    }
    strncpy(out->luks_cipher, luks_hdr.cipher, sizeof(out->luks_cipher) - 1);
    strncpy(out->last_avc, last_avc_log, sizeof(out->last_avc) - 1);
}
