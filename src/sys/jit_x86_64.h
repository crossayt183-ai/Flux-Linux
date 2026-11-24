#ifndef FLUX_JIT_X86_64_H
#define FLUX_JIT_X86_64_H

#include "types.h"

/* Native System V AMD64 ABI JIT-compiled function: (RDI=x, RSI=y, RDX=t) -> RAX */
typedef int64_t (*flux_jit_fn_t)(int64_t x, int64_t y, int64_t t);

typedef struct {
    flux_jit_fn_t fn;
    uint64_t      code_va;
    uint64_t      code_phys;
    size_t        code_bytes;
} flux_jit_module_t;

void flux_jit_init(void);
bool flux_jit_compile(const char *expr, flux_jit_module_t *out_mod);
void flux_jit_free(flux_jit_module_t *mod);
bool flux_jit_selftest(void);

#endif
