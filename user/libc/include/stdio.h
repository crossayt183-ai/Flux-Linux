#ifndef FLUX_USER_STDIO_H
#define FLUX_USER_STDIO_H

#include "flux/types.h"

int putchar(int c);
int puts(const char *s);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int printf(const char *fmt, ...);
int fprintf(int fd, const char *fmt, ...);

#endif
