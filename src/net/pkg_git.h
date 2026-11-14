#ifndef FLUX_PKG_GIT_H
#define FLUX_PKG_GIT_H

#include "types.h"

typedef enum {
    PKG_REPO_FEDORA_RPM = 0,
    PKG_REPO_RPMFUSION  = 1,
    PKG_REPO_FLATHUB    = 2,
    PKG_REPO_GITHUB_REL = 3
} pkg_repo_kind_t;

typedef struct {
    const char     *name;
    const char     *version;
    pkg_repo_kind_t repo;
    uint32_t        size_kib;
    uint32_t        sha256_prefix;
    const char     *app_id;
    const char     *summary;
} pkg_catalog_entry_t;

typedef struct {
    char     repo_url[96];
    char     target_dir[64];
    char     branch[24];
    uint32_t commits_fetched;
    uint32_t objects_unpacked;
    uint32_t bytes_downloaded;
    bool     verified_tls13;
} git_clone_result_t;

void   pkg_git_init(void);
bool   pkg_install(const char *pkg_name, char *out_msg, size_t msg_cap);
bool   pkg_remove(const char *pkg_name, char *out_msg, size_t msg_cap);
size_t pkg_catalog_size(void);
size_t pkg_installed_count(void);
bool   git_clone_repo(const char *url_or_slug, git_clone_result_t *out);
bool   http_download_file(const char *url, const char *dest_path, uint32_t *out_bytes);
bool   pkg_git_selftest(void);

#endif
