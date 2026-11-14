#ifndef FLUX_SYSCALL_H
#define FLUX_SYSCALL_H

#include "types.h"
#include "idt.h"

/* Syscall numbers (Linux x86_64 calling convention: RAX=nr, RDI,RSI,RDX,R10,R8,R9) */
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

#define O_RDONLY        0
#define O_WRONLY        1
#define O_RDWR          2
#define O_CREAT         0x0040
#define O_TRUNC         0x0200
#define O_APPEND        0x0400

typedef struct {
    uint32_t ino;
    uint32_t type;
    uint64_t size;
} sys_stat_t;

typedef struct {
    uint8_t  mac[6];
    uint16_t reserved;
    uint32_t ip;
    uint32_t mask;
    uint32_t gw;
    uint64_t rx_packets;
    uint64_t tx_packets;
} sys_netinfo_t;

typedef struct {
    char     name[64];
    uint32_t ino;
    uint32_t type;
    uint64_t size;
} sys_dirent_t;

#define PROT_READ       1
#define PROT_WRITE      2
#define PROT_EXEC       4

#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANON        0x20

typedef struct {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t heap_used;
    uint64_t cow_faults;
    uint64_t lazy_faults;
} sys_meminfo_t;

typedef struct {
    uint64_t pid;
    uint64_t ppid;
    uint8_t  state;
    uint8_t  priority;
    uint8_t  is_user;
    uint8_t  reserved;
    uint64_t ticks;
    char     name[32];
} sys_taskinfo_t;

void syscall_init(void);
void syscall_set_kstack(uint64_t kstack_top);
cpu_regs_t *syscall_dispatch(cpu_regs_t *regs);

/* User binary registry for execve & kernel spawning */
typedef void (*console_sink_fn_t)(const char *buf, size_t len);
typedef int (*fb_info_hook_t)(void *out_info);
typedef void (*audio_note_hook_t)(uint32_t freq_hz, uint32_t dur_ms, uint8_t wave, uint8_t vol);

void syscall_set_console_sink(console_sink_fn_t fn);
void syscall_register_fb_hook(fb_info_hook_t fn);
void syscall_register_audio_hook(audio_note_hook_t fn);

bool user_exec_program(const char *path, int priority, uint64_t *out_pid);
bool user_exec_program_argv(const char *path, int argc, const char *const *argv, int priority, uint64_t *out_pid);
int64_t user_replace_current(const char *path, cpu_regs_t *regs);
int64_t user_replace_current_argv(const char *path, int argc, const char *const *argv, cpu_regs_t *regs);

#endif
