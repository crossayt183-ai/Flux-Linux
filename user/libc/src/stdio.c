#include "stdio.h"
#include "unistd.h"
#include "string.h"

typedef struct {
    char  *buf;
    size_t cap;
    size_t pos;
} fmt_out_t;

static void out_ch(fmt_out_t *o, char c) {
    if (o->pos + 1 < o->cap) o->buf[o->pos] = c;
    o->pos++;
}

static void fmt_uint(fmt_out_t *o, uint64_t v, int base, bool upper, int width, char pad, bool left, bool neg) {
    char tmp[32];
    const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int idx = 0;
    if (v == 0) tmp[idx++] = '0';
    else while (v > 0 && idx < 32) { tmp[idx++] = digs[v % (uint64_t)base]; v /= (uint64_t)base; }

    int total = idx + (neg ? 1 : 0);
    int pad_cnt = (width > total) ? (width - total) : 0;

    if (!left) {
        if (neg && pad == '0') out_ch(o, '-');
        for (int p = 0; p < pad_cnt; p++) out_ch(o, pad);
        if (neg && pad != '0') out_ch(o, '-');
        while (idx > 0) out_ch(o, tmp[--idx]);
    } else {
        if (neg) out_ch(o, '-');
        while (idx > 0) out_ch(o, tmp[--idx]);
        for (int p = 0; p < pad_cnt; p++) out_ch(o, ' ');
    }
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    fmt_out_t o = { buf, size, 0 };
    while (*fmt) {
        if (*fmt != '%') { out_ch(&o, *fmt++); continue; }
        fmt++;
        char pad = ' ';
        bool left = false;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left = true;
            else if (!left) pad = '0';
            fmt++;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt++ - '0'); }
        bool is_long = false;
        if (*fmt == 'l') { is_long = true; fmt++; if (*fmt == 'l') fmt++; }
        else if (*fmt == 'z') { is_long = true; fmt++; }

        char sp = *fmt ? *fmt++ : '\0';
        switch (sp) {
        case 'c': out_ch(&o, (char)va_arg(ap, int)); break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = (int)strlen(s);
            if (!left) while (len < width) { out_ch(&o, ' '); width--; }
            while (*s) out_ch(&o, *s++);
            if (left) while (len < width) { out_ch(&o, ' '); width--; }
            break;
        }
        case 'd':
        case 'i': {
            int64_t v = is_long ? va_arg(ap, int64_t) : (int64_t)va_arg(ap, int);
            bool neg = v < 0;
            uint64_t uv = neg ? (uint64_t)(-(v + 1)) + 1ULL : (uint64_t)v;
            fmt_uint(&o, uv, 10, false, width, pad, left, neg);
            break;
        }
        case 'u': {
            uint64_t v = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
            fmt_uint(&o, v, 10, false, width, pad, left, false);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t v = is_long ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
            fmt_uint(&o, v, 16, sp == 'X', width, pad, left, false);
            break;
        }
        case 'p':
            out_ch(&o, '0'); out_ch(&o, 'x');
            fmt_uint(&o, (uint64_t)(uintptr_t)va_arg(ap, void *), 16, false, 16, '0', false, false);
            break;
        case '%': out_ch(&o, '%'); break;
        default: out_ch(&o, '%'); if (sp) out_ch(&o, sp); break;
        }
    }
    if (size > 0) buf[(o.pos < size) ? o.pos : (size - 1)] = '\0';
    return (int)o.pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int fprintf(int fd, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    size_t len = (size_t)((n < (int)sizeof(tmp)) ? n : (int)sizeof(tmp) - 1);
    return (int)write(fd, tmp, len);
}

int printf(const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    size_t len = (size_t)((n < (int)sizeof(tmp)) ? n : (int)sizeof(tmp) - 1);
    return (int)write(1, tmp, len);
}

int putchar(int c) {
    char ch = (char)c;
    return write(1, &ch, 1) == 1 ? c : -1;
}

int puts(const char *s) {
    write(1, s, strlen(s));
    write(1, "\n", 1);
    return 0;
}
