#ifndef FLUX_SMB3_NFS4_FUSE_H
#define FLUX_SMB3_NFS4_FUSE_H

#include "types.h"

typedef struct {
    uint32_t proto_rule_id;
    uint8_t  netfs_type; /* 1=SMB3.1.1-ksmbd, 2=NFSv4.2-pNFS, 3=FUSE3-Passthrough, 4=VirtFS-9P2000.L */
    uint8_t  security_flavor;
    uint16_t max_credit_window;
    const char *share_endpoint;
    const char *rpc_operation;
    const char *cache_coherency_lease;
} smb3_nfs4_fuse_rule_t;

typedef struct {
    bool     smb311_gcm_multichannel;
    bool     nfsv42_pnfs_clone_ready;
    bool     fuse3_passthrough_zc;
    bool     vboxsf_virtfs_automount;
    uint32_t active_netfs_rules;
} smb3_nfs4_fuse_status_t;

void   smb3_nfs4_fuse_init(void);
bool   smb3_nfs4_fuse_get_status(smb3_nfs4_fuse_status_t *out);
size_t smb3_nfs4_fuse_db_size(void);
bool   smb3_nfs4_fuse_selftest(void);

#endif
