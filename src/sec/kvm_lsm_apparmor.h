#ifndef FLUX_KVM_LSM_APPARMOR_H
#define FLUX_KVM_LSM_APPARMOR_H

#include "types.h"

typedef struct {
    uint16_t rule_id;
    uint8_t  engine_type; /* 1=KVM-VT-x-VMCS, 2=KVM-AMD-V-SVM, 3=LSM-AppArmor, 4=LSM-SELinux, 5=LSM-Landlock */
    uint8_t  action_code;
    uint32_t vmexit_or_perm_mask;
    const char *profile_or_vmcs_field;
    const char *trap_or_path_pattern;
    const char *handler_action;
} kvm_lsm_rule_t;

typedef struct {
    bool     kvm_vtx_ept_online;
    bool     kvm_amdv_npt_ready;
    bool     lsm_selinux_enforcing;
    bool     lsm_apparmor_enforcing;
    bool     lsm_landlock_active;
    uint32_t max_vcpus_per_vm;
    uint32_t loaded_lsm_profiles;
    char     hypervisor_caps[72];
} kvm_lsm_status_t;

void   kvm_lsm_apparmor_init(void);
bool   kvm_lsm_get_status(kvm_lsm_status_t *out);
bool   kvm_test_vmx_vmentry_roundtrip(uint32_t *out_cycles);
size_t kvm_lsm_rule_db_size(void);
bool   kvm_lsm_apparmor_selftest(void);

#endif
