#include "serial.h"
#include "io.h"
#include "cpu.h"

#define COM1_PORT 0x3F8

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00); /* Disable interrupts */
    outb(COM1_PORT + 3, 0x80); /* Enable DLAB */
    outb(COM1_PORT + 0, 0x01); /* 115200 baud (divisor 1) */
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03); /* 8 bits, no parity, 1 stop bit */
    outb(COM1_PORT + 2, 0xC7); /* Enable FIFO, clear, 14-byte threshold */
    outb(COM1_PORT + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
}

static int serial_tx_empty(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

void serial_putc(char c) {
    if (c == '\n') {
        while (!serial_tx_empty()) {}
        outb(COM1_PORT, '\r');
    }
    while (!serial_tx_empty()) {}
    outb(COM1_PORT, (uint8_t)c);
}

void serial_puts(const char *s) {
    if (!s) return;
    while (*s) {
        serial_putc(*s++);
    }
}

typedef void (*emit_fn_t)(char c, void *ctx);

static void emit_uint(uint64_t val, int base, bool upper, int width, char pad,
                      bool left_align, bool neg, emit_fn_t emit, void *ctx) {
    char tmp[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int idx = 0;

    if (val == 0) {
        tmp[idx++] = '0';
    } else {
        while (val > 0 && idx < (int)sizeof(tmp)) {
            tmp[idx++] = digits[val % (uint64_t)base];
            val /= (uint64_t)base;
        }
    }

    int total_len = idx + (neg ? 1 : 0);
    int pad_cnt = (width > total_len) ? (width - total_len) : 0;

    if (!left_align) {
        if (neg && pad == '0') emit('-', ctx);
        for (int p = 0; p < pad_cnt; p++) emit(pad, ctx);
        if (neg && pad != '0') emit('-', ctx);
        while (idx > 0) emit(tmp[--idx], ctx);
    } else {
        if (neg) emit('-', ctx);
        while (idx > 0) emit(tmp[--idx], ctx);
        for (int p = 0; p < pad_cnt; p++) emit(' ', ctx);
    }
}

static int kvformat(emit_fn_t emit, void *ctx, const char *fmt, va_list ap) {
    int count = 0;
    while (*fmt) {
        if (*fmt != '%') {
            emit(*fmt++, ctx);
            count++;
            continue;
        }
        fmt++;
        char pad = ' ';
        bool left_align = false;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left_align = true;
            else if (!left_align) pad = '0';
            fmt++;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        bool is_long = false;
        if (*fmt == 'l') {
            is_long = true;
            fmt++;
            if (*fmt == 'l') fmt++;
        } else if (*fmt == 'z') {
            is_long = true;
            fmt++;
        }

        char spec = *fmt ? *fmt++ : '\0';
        switch (spec) {
            case 'c': {
                char c = (char)va_arg(ap, int);
                emit(c, ctx);
                count++;
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                int len = 0;
                while (s[len]) len++;
                if (!left_align) {
                    while (len < width) { emit(' ', ctx); count++; width--; }
                }
                while (*s) {
                    emit(*s++, ctx);
                    count++;
                }
                if (left_align) {
                    while (len < width) { emit(' ', ctx); count++; width--; }
                }
                break;
            }
            case 'd':
            case 'i': {
                int64_t v = is_long ? va_arg(ap, int64_t) : (int64_t)va_arg(ap, int);
                bool neg = (v < 0);
                uint64_t uv = neg ? (uint64_t)(-(v + 1)) + 1ULL : (uint64_t)v;
                emit_uint(uv, 10, false, width, pad, left_align, neg, emit, ctx);
                break;
            }
            case 'u': {
                uint64_t v = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
                emit_uint(v, 10, false, width, pad, left_align, false, emit, ctx);
                break;
            }
            case 'x':
            case 'X': {
                uint64_t v = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
                emit_uint(v, 16, spec == 'X', width, pad, left_align, false, emit, ctx);
                break;
            }
            case 'p': {
                uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void *);
                emit('0', ctx);
                emit('x', ctx);
                emit_uint(v, 16, false, 16, '0', false, false, emit, ctx);
                break;
            }
            case '%':
                emit('%', ctx);
                count++;
                break;
            default:
                emit('%', ctx);
                if (spec) emit(spec, ctx);
                break;
        }
    }
    return count;
}

typedef struct {
    char *buf;
    size_t cap;
    size_t pos;
} str_ctx_t;

static void emit_str(char c, void *raw) {
    str_ctx_t *ctx = (str_ctx_t *)raw;
    if (ctx->pos + 1 < ctx->cap) {
        ctx->buf[ctx->pos] = c;
    }
    ctx->pos++;
}

static void emit_serial(char c, void *raw) {
    (void)raw;
    serial_putc(c);
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    str_ctx_t ctx = { buf, size, 0 };
    kvformat(emit_str, &ctx, fmt, ap);
    if (size > 0) {
        size_t nul = (ctx.pos < size) ? ctx.pos : (size - 1);
        buf[nul] = '\0';
    }
    return (int)ctx.pos;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

void kprintf(const char *fmt, ...) {
    uint64_t rf = irq_save_disable();
    va_list ap;
    va_start(ap, fmt);
    kvformat(emit_serial, NULL, fmt, ap);
    va_end(ap);
    irq_restore(rf);
}

void kpanic(const char *fmt, ...) {
    cli();
    serial_puts("\n\n=== FLUXOS KERNEL PANIC ===\n");
    va_list ap;
    va_start(ap, fmt);
    kvformat(emit_serial, NULL, fmt, ap);
    va_end(ap);
    serial_puts("\nSystem halted.\n");
    for (;;) {
        hlt();
    }
}
