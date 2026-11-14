#ifndef FLUX_USER_UNISTD_H
#define FLUX_USER_UNISTD_H

#include "flux/types.h"

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x0040
#define O_TRUNC  0x0200
#define O_APPEND 0x0400

#define PROT_READ   1
#define PROT_WRITE  2
#define PROT_EXEC   4
#define MAP_PRIVATE 0x02
#define MAP_ANON    0x20

typedef struct {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t heap_used;
    uint64_t cow_faults;
    uint64_t lazy_faults;
} meminfo_t;

typedef struct {
    uint32_t ino;
    uint32_t type;
    uint64_t size;
} flux_stat_t;

typedef struct {
    char     name[64];
    uint32_t ino;
    uint32_t type;
    uint64_t size;
} flux_dirent_t;

__attribute__((noreturn)) void exit(int code);
ssize_t write(int fd, const void *buf, size_t len);
ssize_t read(int fd, void *buf, size_t len);
int     open(const char *path, int flags);
int     close(int fd);
int64_t getpid(void);
int64_t getppid(void);
int64_t fork(void);
int     execve(const char *path, const char *const *argv, const char *const *envp);
int64_t waitpid(int64_t pid, int32_t *status);
void    yield(void);
void    sleep_ms(uint64_t ms);
void   *brk(void *addr);
void   *sbrk(int64_t inc);
void   *mmap(void *addr, size_t len, int prot, int flags);
int     munmap(void *addr, size_t len);
int     pipe(int fds[2]);
int     dup2(int oldfd, int newfd);
uint64_t uptime_ms(void);
int     mkdir(const char *path);
int     unlink(const char *path);
int     readdir(const char *path, flux_dirent_t *out, size_t max_entries);
int     stat(const char *path, flux_stat_t *out);
int     meminfo(meminfo_t *out);
int     uname(char *buf);
void    audio_note(uint32_t freq_hz, uint32_t dur_ms, uint8_t wave, uint8_t vol);

#endif
