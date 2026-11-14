#ifndef FLUX_SERIAL_H
#define FLUX_SERIAL_H

#include "types.h"

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t size, const char *fmt, ...);
void kprintf(const char *fmt, ...);
void kpanic(const char *fmt, ...) NORETURN;

#endif
