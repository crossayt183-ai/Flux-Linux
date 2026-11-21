#ifndef FLUX_ABI_SANITIZER_H
#define FLUX_ABI_SANITIZER_H

#include "types.h"

typedef struct {
    uint16_t contract_id;
    uint16_t syscall_nr;
    uint32_t arg_mask;
    uint64_t max_buf_bytes;
    const char *syscall_name;
    const char *bounds_contract;
    const char *fault_isolation_fn;
} abi_contract_rule_t;

typedef struct {
    bool     kasan_redzones_active;
    bool     stack_canaries_enforced;
    bool     smap_smep_emulated;
    uint64_t syscalls_validated;
    uint32_t bad_pointers_rejected;
    uint32_t double_frees_blocked;
} abi_sanitizer_status_t;

void   abi_sanitizer_init(void);
bool   abi_validate_user_buffer(uint64_t user_va, size_t byte_len);
bool   abi_sanitizer_get_status(abi_sanitizer_status_t *out);
size_t abi_contract_db_size(void);
bool   abi_sanitizer_selftest(void);

#endif
