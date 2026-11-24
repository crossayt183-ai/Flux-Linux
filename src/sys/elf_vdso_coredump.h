#ifndef FLUX_ELF_VDSO_COREDUMP_H
#define FLUX_ELF_VDSO_COREDUMP_H

#include "types.h"

typedef struct {
    uint32_t rule_id;
    uint16_t hw_class;
    uint16_t param_val;
    const char *profile_key;
    const char *fastpath_engine;
    const char *telemetry_bound;
} elf_vdso_coredump_rule_t;

void   elf_vdso_coredump_init(void);
size_t elf_vdso_coredump_db_size(void);
bool   elf_vdso_coredump_selftest(void);

#endif
