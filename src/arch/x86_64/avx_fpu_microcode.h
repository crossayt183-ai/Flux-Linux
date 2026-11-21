#ifndef FLUX_AVX_FPU_MICROCODE_H
#define FLUX_AVX_FPU_MICROCODE_H

#include "types.h"

typedef struct {
    uint16_t patch_id;
    uint8_t  arch_feature; /* 1=Microcode-Update, 2=Spectre-MDS-Mitigation, 3=XSAVE-AVX-FPU, 4=AESNI-PCLMUL-Crypto */
    uint8_t  stepping_mask;
    uint32_t ucode_rev_or_xcr0;
    const char *cpu_family_target;
    const char *msr_patch_sequence;
    const char *errata_and_security_fix;
} avx_ucode_rule_t;

typedef struct {
    bool     ucode_early_loader_ok;
    bool     spectre_mds_verw_active;
    bool     xsave_avx_ymm_enabled;
    uint32_t active_ucode_revision;
    uint32_t xsave_area_bytes;
    uint32_t total_ucode_rules;
} avx_fpu_microcode_status_t;

void   avx_fpu_microcode_init(void);
bool   avx_fpu_microcode_get_status(avx_fpu_microcode_status_t *out);
size_t avx_fpu_microcode_db_size(void);
bool   avx_fpu_microcode_selftest(void);

#endif
