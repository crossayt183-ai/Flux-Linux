#ifndef FLUX_SECCOMP_EBPF_LSM_H
#define FLUX_SECCOMP_EBPF_LSM_H

#include "types.h"

typedef struct {
    uint32_t filter_rule_id;
    uint8_t  hook_kind;      /* 1=Seccomp-BPF, 2=eBPF-KRSI-LSM, 3=Namespace-Isolation, 4=Capability-Bounding */
    uint8_t  verdict_action; /* 1=ALLOW, 2=ERRNO_EPERM, 3=KILL_PROCESS, 4=USER_NOTIF_TRAP, 5=AUDIT_LOG */
    uint16_t syscall_nr_or_cap;
    const char *sandbox_profile;
    const char *bpf_predicate;
    const char *mitigation_effect;
} seccomp_ebpf_rule_t;

typedef struct {
    bool     seccomp_bpf_tsync_online;
    bool     ebpf_krsi_lsm_attached;
    bool     userns_unpriv_restricted;
    bool     no_new_privs_enforced;
    uint32_t active_seccomp_filters;
} seccomp_ebpf_status_t;

void   seccomp_ebpf_lsm_init(void);
bool   seccomp_ebpf_get_status(seccomp_ebpf_status_t *out);
size_t seccomp_ebpf_db_size(void);
bool   seccomp_ebpf_lsm_selftest(void);

#endif
