#ifndef FLUX_USER_SYSCALL_H
#define FLUX_USER_SYSCALL_H

#include "types.h"

#define SYS_EXIT        0
#define SYS_WRITE       1
#define SYS_READ        2
#define SYS_OPEN        3
#define SYS_CLOSE       4
#define SYS_GETPID      5
#define SYS_GETPPID     6
#define SYS_FORK        7
#define SYS_EXECVE      8
#define SYS_WAITPID     9
#define SYS_YIELD       10
#define SYS_SLEEP_MS    11
#define SYS_BRK         12
#define SYS_MMAP        13
#define SYS_MUNMAP      14
#define SYS_PIPE        15
#define SYS_DUP2        16
#define SYS_UPTIME_MS   17
#define SYS_GETTASKS    18
#define SYS_KILL        19
#define SYS_MEMINFO     20
#define SYS_REBOOT      21
#define SYS_UNAME       22
#define SYS_FB_INFO     23
#define SYS_AUDIO_NOTE  24
#define SYS_AUDIO_BEEP  25
#define SYS_GUI_PRINT   26
#define SYS_MKDIR       27
#define SYS_UNLINK      28
#define SYS_READDIR     29
#define SYS_STAT        30
#define SYS_SOCKET      31
#define SYS_BIND        32
#define SYS_CONNECT     33
#define SYS_SENDTO      34
#define SYS_RECVFROM    35
#define SYS_PING        36
#define SYS_NETINFO     37
#define SYS_LISTEN      38
#define SYS_ACCEPT      39
#define SYS_WL_CREATE   40
#define SYS_WL_COMMIT   41
#define SYS_SEC_INFO    42
#define SYS_SEC_CTL     43

static inline int64_t __syscall0(uint64_t nr) {
    int64_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(nr) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t __syscall1(uint64_t nr, uint64_t a1) {
    int64_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(nr), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t __syscall2(uint64_t nr, uint64_t a1, uint64_t a2) {
    int64_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t __syscall3(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3) {
    int64_t ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t __syscall4(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = a4;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t __syscall5(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3,
                                 uint64_t a4, uint64_t a5) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = a4;
    register uint64_t r8  __asm__("r8")  = a5;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t __syscall6(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3,
                                 uint64_t a4, uint64_t a5, uint64_t a6) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = a4;
    register uint64_t r8  __asm__("r8")  = a5;
    register uint64_t r9  __asm__("r9")  = a6;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2), "d"(a3),
                      "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}

#endif
