#include "jit_x86_64.h"
#include "pmm.h"
#include "vmm.h"
#include "cpu.h"
#include "spinlock.h"
#include "string.h"
#include "serial.h"

/*
 * Native x86_64 Recursive-Descent Expression -> Machine-Code JIT Compiler
 * Compiles C/shader expressions over (int64_t x, int64_t y, int64_t t) directly
 * into W^X protected Ring-0/Ring-3 native x86_64 instructions:
 *   Prologue saves x->[rbp-8], y->[rbp-16], t->[rbp-24].
 *   Recursive emitter evaluates subexpressions in RAX using hardware stack + RCX,
 *   including division-by-zero hardware guard before CQO; IDIV RCX.
 *   Seals the 4 KiB page Read+Exec (PTE_PRESENT without PTE_WRITE or PTE_NX) before execution!
 */

#define JIT_VA_ARENA_BASE 0xFFFFFFFFB0000000ULL
#define JIT_MAX_CODE      3800U

static uint64_t   g_jit_next_va = JIT_VA_ARENA_BASE;
static spinlock_t g_jit_lock    = SPINLOCK_INIT;
static uint32_t   g_jit_compiled_count = 0;

typedef struct {
    const char *p;
    uint8_t    *buf;
    size_t      len;
    bool        err;
} jit_ctx_t;

static void emit1(jit_ctx_t *c, uint8_t b) {
    if (c->len >= JIT_MAX_CODE) { c->err = true; return; }
    c->buf[c->len++] = b;
}

static void emit_bytes(jit_ctx_t *c, const void *src, size_t n) {
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) emit1(c, s[i]);
}

static void emit_imm64_rax(jit_ctx_t *c, int64_t val) {
    if (val >= -2147483648LL && val <= 2147483647LL) {
        /* 48 C7 C0 imm32 (mov rax, sign_ext_imm32) */
        uint32_t imm32 = (uint32_t)(int32_t)val;
        emit1(c, 0x48); emit1(c, 0xC7); emit1(c, 0xC0);
        emit_bytes(c, &imm32, 4);
    } else {
        /* 48 B8 imm64 (movabs rax, imm64) */
        uint64_t u = (uint64_t)val;
        emit1(c, 0x48); emit1(c, 0xB8);
        emit_bytes(c, &u, 8);
    }
}

static void skip_ws(jit_ctx_t *c) {
    while (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r') c->p++;
}

static void parse_bit_or(jit_ctx_t *c);

static void parse_primary(jit_ctx_t *c) {
    skip_ws(c);
    if (c->err) return;

    if (*c->p == '(') {
        c->p++;
        parse_bit_or(c);
        skip_ws(c);
        if (*c->p == ')') c->p++;
        else c->err = true;
        return;
    }

    if (*c->p == '-') {
        c->p++;
        parse_primary(c);
        /* neg rax: 48 F7 D8 */
        const uint8_t op[] = { 0x48, 0xF7, 0xD8 };
        emit_bytes(c, op, sizeof(op));
        return;
    }

    if (*c->p == '~') {
        c->p++;
        parse_primary(c);
        /* not rax: 48 F7 D0 */
        const uint8_t op[] = { 0x48, 0xF7, 0xD0 };
        emit_bytes(c, op, sizeof(op));
        return;
    }

    if ((*c->p == 'x' || *c->p == 'y' || *c->p == 't') &&
        !((c->p[1] >= 'a' && c->p[1] <= 'z') || (c->p[1] >= '0' && c->p[1] <= '9') || c->p[1] == '_')) {
        char v = *c->p++;
        uint8_t disp = (v == 'x') ? 0xF8U : ((v == 'y') ? 0xF0U : 0xE8U);
        /* mov rax, [rbp - 8/16/24]: 48 8B 45 disp8 */
        emit1(c, 0x48); emit1(c, 0x8B); emit1(c, 0x45); emit1(c, disp);
        return;
    }

    if (*c->p >= '0' && *c->p <= '9') {
        int64_t val = 0;
        if (c->p[0] == '0' && (c->p[1] == 'x' || c->p[1] == 'X')) {
            c->p += 2;
            while (1) {
                char ch = *c->p;
                if (ch >= '0' && ch <= '9') val = (val << 4) | (ch - '0');
                else if (ch >= 'a' && ch <= 'f') val = (val << 4) | (ch - 'a' + 10);
                else if (ch >= 'A' && ch <= 'F') val = (val << 4) | (ch - 'A' + 10);
                else break;
                c->p++;
            }
        } else {
            while (*c->p >= '0' && *c->p <= '9') {
                val = val * 10 + (*c->p - '0');
                c->p++;
            }
        }
        emit_imm64_rax(c, val);
        return;
    }

    c->err = true;
}

static void parse_mul(jit_ctx_t *c) {
    parse_primary(c);
    while (!c->err) {
        skip_ws(c);
        char op = *c->p;
        if (op != '*' && op != '/' && op != '%') break;
        c->p++;
        emit1(c, 0x50); /* push rax (left operand) */
        parse_primary(c);
        /* mov rcx, rax (right operand); pop rax (left operand) */
        const uint8_t pop_lhs[] = { 0x48, 0x89, 0xC1, 0x58 };
        emit_bytes(c, pop_lhs, sizeof(pop_lhs));

        if (op == '*') {
            /* imul rax, rcx: 48 0F AF C1 */
            const uint8_t imul[] = { 0x48, 0x0F, 0xAF, 0xC1 };
            emit_bytes(c, imul, sizeof(imul));
        } else {
            /* Guard RCX == 0 before IDIV: test rcx, rcx; jnz +5; mov ecx, 1; cqo; idiv rcx */
            const uint8_t div_seq[] = {
                0x48, 0x85, 0xC9,                   /* test rcx, rcx */
                0x75, 0x05,                         /* jnz +5 */
                0xB9, 0x01, 0x00, 0x00, 0x00,       /* mov ecx, 1 */
                0x48, 0x99,                         /* cqo */
                0x48, 0xF7, 0xF9                    /* idiv rcx */
            };
            emit_bytes(c, div_seq, sizeof(div_seq));
            if (op == '%') {
                /* mov rax, rdx: 48 89 D0 */
                const uint8_t rem[] = { 0x48, 0x89, 0xD0 };
                emit_bytes(c, rem, sizeof(rem));
            }
        }
    }
}

static void parse_add(jit_ctx_t *c) {
    parse_mul(c);
    while (!c->err) {
        skip_ws(c);
        char op = *c->p;
        if (op != '+' && op != '-') break;
        c->p++;
        emit1(c, 0x50); /* push rax */
        parse_mul(c);
        const uint8_t pop_lhs[] = { 0x48, 0x89, 0xC1, 0x58 };
        emit_bytes(c, pop_lhs, sizeof(pop_lhs));
        if (op == '+') {
            const uint8_t add_op[] = { 0x48, 0x01, 0xC8 }; /* add rax, rcx */
            emit_bytes(c, add_op, sizeof(add_op));
        } else {
            const uint8_t sub_op[] = { 0x48, 0x29, 0xC8 }; /* sub rax, rcx */
            emit_bytes(c, sub_op, sizeof(sub_op));
        }
    }
}

static void parse_shift(jit_ctx_t *c) {
    parse_add(c);
    while (!c->err) {
        skip_ws(c);
        bool shl = (c->p[0] == '<' && c->p[1] == '<');
        bool shr = (c->p[0] == '>' && c->p[1] == '>');
        if (!shl && !shr) break;
        c->p += 2;
        emit1(c, 0x50); /* push rax */
        parse_add(c);
        /* mov rcx, rax; and cl, 63; pop rax */
        const uint8_t prep[] = { 0x48, 0x89, 0xC1, 0x80, 0xE1, 0x3F, 0x58 };
        emit_bytes(c, prep, sizeof(prep));
        if (shl) {
            const uint8_t s[] = { 0x48, 0xD3, 0xE0 }; /* shl rax, cl */
            emit_bytes(c, s, sizeof(s));
        } else {
            const uint8_t s[] = { 0x48, 0xD3, 0xF8 }; /* sar rax, cl */
            emit_bytes(c, s, sizeof(s));
        }
    }
}

static void parse_cmp(jit_ctx_t *c) {
    parse_shift(c);
    while (!c->err) {
        skip_ws(c);
        uint8_t setcc = 0;
        if (c->p[0] == '<' && c->p[1] == '=') { setcc = 0x9EU; c->p += 2; }
        else if (c->p[0] == '>' && c->p[1] == '=') { setcc = 0x9DU; c->p += 2; }
        else if (c->p[0] == '=' && c->p[1] == '=') { setcc = 0x94U; c->p += 2; }
        else if (c->p[0] == '!' && c->p[1] == '=') { setcc = 0x95U; c->p += 2; }
        else if (c->p[0] == '<' && c->p[1] != '<') { setcc = 0x9CU; c->p += 1; }
        else if (c->p[0] == '>' && c->p[1] != '>') { setcc = 0x9FU; c->p += 1; }
        else break;

        emit1(c, 0x50); /* push rax */
        parse_shift(c);
        /* mov rcx, rax; pop rax; cmp rax, rcx; setCC al; movzx rax, al */
        const uint8_t cmp_pre[] = { 0x48, 0x89, 0xC1, 0x58, 0x48, 0x39, 0xC8 };
        emit_bytes(c, cmp_pre, sizeof(cmp_pre));
        emit1(c, 0x0F); emit1(c, setcc); emit1(c, 0xC0);
        const uint8_t zext[] = { 0x48, 0x0F, 0xB6, 0xC0 };
        emit_bytes(c, zext, sizeof(zext));
    }
}

static void parse_bit_and(jit_ctx_t *c) {
    parse_cmp(c);
    while (!c->err) {
        skip_ws(c);
        if (c->p[0] != '&' || c->p[1] == '&') break;
        c->p++;
        emit1(c, 0x50);
        parse_cmp(c);
        const uint8_t seq[] = { 0x48, 0x89, 0xC1, 0x58, 0x48, 0x21, 0xC8 };
        emit_bytes(c, seq, sizeof(seq));
    }
}

static void parse_bit_xor(jit_ctx_t *c) {
    parse_bit_and(c);
    while (!c->err) {
        skip_ws(c);
        if (c->p[0] != '^') break;
        c->p++;
        emit1(c, 0x50);
        parse_bit_and(c);
        const uint8_t seq[] = { 0x48, 0x89, 0xC1, 0x58, 0x48, 0x31, 0xC8 };
        emit_bytes(c, seq, sizeof(seq));
    }
}

static void parse_bit_or(jit_ctx_t *c) {
    parse_bit_xor(c);
    while (!c->err) {
        skip_ws(c);
        if (c->p[0] != '|' || c->p[1] == '|') break;
        c->p++;
        emit1(c, 0x50);
        parse_bit_xor(c);
        const uint8_t seq[] = { 0x48, 0x89, 0xC1, 0x58, 0x48, 0x09, 0xC8 };
        emit_bytes(c, seq, sizeof(seq));
    }
}

void flux_jit_init(void) {
    g_jit_next_va = JIT_VA_ARENA_BASE;
    g_jit_compiled_count = 0;
    kprintf("[JIT/x86_64] Native W^X machine-code JIT compiler initialized @ 0x%lx\n",
            JIT_VA_ARENA_BASE);
}

bool flux_jit_compile(const char *expr, flux_jit_module_t *out_mod) {
    if (!expr || !out_mod) return false;
    memset(out_mod, 0, sizeof(*out_mod));

    uint64_t phys = pmm_alloc_page();
    if (!phys) return false;

    uint8_t *code = (uint8_t *)phys_to_virt(phys);
    memset(code, 0xCC, PAGE_SIZE); /* INT3 trap fill on unused bytes */

    jit_ctx_t ctx = {
        .p   = expr,
        .buf = code,
        .len = 0,
        .err = false
    };

    /* Prologue:
     *   push rbp            (55)
     *   mov rbp, rsp        (48 89 E5)
     *   sub rsp, 32         (48 83 EC 20)
     *   mov [rbp-8],  rdi   (48 89 7D F8)
     *   mov [rbp-16], rsi   (48 89 75 F0)
     *   mov [rbp-24], rdx   (48 89 55 E8)
     */
    static const uint8_t prologue[] = {
        0x55,
        0x48, 0x89, 0xE5,
        0x48, 0x83, 0xEC, 0x20,
        0x48, 0x89, 0x7D, 0xF8,
        0x48, 0x89, 0x75, 0xF0,
        0x48, 0x89, 0x55, 0xE8
    };
    emit_bytes(&ctx, prologue, sizeof(prologue));

    parse_bit_or(&ctx);
    skip_ws(&ctx);
    if (*ctx.p != '\0') ctx.err = true;

    /* Epilogue: mov rsp, rbp (48 89 EC); pop rbp (5D); ret (C3) */
    static const uint8_t epilogue[] = { 0x48, 0x89, 0xEC, 0x5D, 0xC3 };
    emit_bytes(&ctx, epilogue, sizeof(epilogue));

    if (ctx.err) {
        pmm_free_page(phys);
        return false;
    }

    spin_lock_irqsave(&g_jit_lock);
    uint64_t va = g_jit_next_va;
    g_jit_next_va += PAGE_SIZE;
    g_jit_compiled_count++;
    spin_unlock_irqrestore(&g_jit_lock);

    /* Strict W^X: map Read-Only + Executable (PTE_PRESENT without PTE_WRITE and without PTE_NX!) */
    if (!vmm_map_page(vmm_kernel_space(), va, phys, PTE_PRESENT)) {
        pmm_free_page(phys);
        return false;
    }
    invlpg(va);

    out_mod->fn         = (flux_jit_fn_t)(uintptr_t)va;
    out_mod->code_va    = va;
    out_mod->code_phys  = phys;
    out_mod->code_bytes = ctx.len;
    return true;
}

void flux_jit_free(flux_jit_module_t *mod) {
    if (!mod || !mod->code_va) return;
    vmm_unmap_page(vmm_kernel_space(), mod->code_va, true);
    memset(mod, 0, sizeof(*mod));
}

bool flux_jit_selftest(void) {
    flux_jit_module_t m1, m2, m3;
    if (!flux_jit_compile("((x * x + y * y) >> 4) + (t * 3) - (x ^ y)", &m1)) return false;
    if (!flux_jit_compile("((x - 128) * (x - 128) + (y - 96) * (y - 96)) < 4096", &m2)) {
        flux_jit_free(&m1);
        return false;
    }
    if (!flux_jit_compile("((x * 31 + y * 17) / (t - t)) + ((x & 15) << 3)", &m3)) {
        flux_jit_free(&m2);
        flux_jit_free(&m1);
        return false;
    }

    bool ok = true;
    for (int64_t x = -20; x <= 160; x += 37) {
        for (int64_t y = -10; y <= 140; y += 29) {
            int64_t t = 14;
            int64_t exp1 = (((x * x + y * y) >> 4) + (t * 3) - (x ^ y));
            int64_t got1 = m1.fn(x, y, t);
            int64_t exp2 = (((x - 128) * (x - 128) + (y - 96) * (y - 96)) < 4096) ? 1 : 0;
            int64_t got2 = m2.fn(x, y, t);
            /* m3 divides by (t - t == 0) which JIT hardware-guards to / 1 */
            int64_t exp3 = ((x * 31 + y * 17) / 1) + ((x & 15) << 3);
            int64_t got3 = m3.fn(x, y, t);
            if (got1 != exp1 || got2 != exp2 || got3 != exp3) {
                ok = false;
                break;
            }
        }
    }

    kprintf("[JIT/x86_64] Executed 3 native W^X compiled shaders (%lu+%lu+%lu bytes) -> %s\n",
            m1.code_bytes, m2.code_bytes, m3.code_bytes, ok ? "PASS" : "FAIL");
    flux_jit_free(&m3);
    flux_jit_free(&m2);
    flux_jit_free(&m1);
    return ok;
}
