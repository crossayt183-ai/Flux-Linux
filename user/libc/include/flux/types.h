#ifndef FLUX_USER_TYPES_H
#define FLUX_USER_TYPES_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long      uint64_t;
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long               int64_t;
typedef unsigned long      size_t;
typedef long               ssize_t;
typedef unsigned long      uintptr_t;

typedef _Bool bool;
#define true  1
#define false 0
#define NULL  ((void *)0)

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, l)   __builtin_va_arg(v, l)

#endif
