#ifndef FLUX_BPF_VERIFIER_JIT_H
#define FLUX_BPF_VERIFIER_JIT_H

#include "types.h"

typedef struct PACKED {
    uint8_t  opcode;
    uint8_t  dst_reg : 4;
    uint8_t  src_reg : 4;
    int16_t  off;
    int32_t  imm;
} bpf_insn_t;

typedef struct {
    uint32_t prog_id;
    uint8_t  attach_type;
    uint8_t  insn_count;
    uint16_t jit_flags;
    const char *hook_symbol;
    const char *verifier_invariant;
    const char *x86_jit_lowering;
} bpf_verifier_rule_t;

void   bpf_verifier_jit_init(void);
bool   bpf_verify_program(const bpf_insn_t *insns, size_t len);
bool   bpf_run_verified(const bpf_insn_t *insns, size_t len, uint64_t ctx_arg, uint64_t *out_r0);
size_t bpf_verifier_db_size(void);
bool   bpf_verifier_jit_selftest(void);

#endif
