#include "unistd.h"
#include "flux/syscall.h"

void exit(int code) {
    __syscall1(SYS_EXIT, (uint64_t)(int64_t)code);
    for (;;) {}
}

ssize_t write(int fd, const void *buf, size_t len) {
    return (ssize_t)__syscall3(SYS_WRITE, (uint64_t)(int64_t)fd, (uint64_t)(uintptr_t)buf, (uint64_t)len);
}

ssize_t read(int fd, void *buf, size_t len) {
    return (ssize_t)__syscall3(SYS_READ, (uint64_t)(int64_t)fd, (uint64_t)(uintptr_t)buf, (uint64_t)len);
}

int open(const char *path, int flags) {
    return (int)__syscall2(SYS_OPEN, (uint64_t)(uintptr_t)path, (uint64_t)(uint32_t)flags);
}

int close(int fd) {
    return (int)__syscall1(SYS_CLOSE, (uint64_t)(int64_t)fd);
}

int64_t getpid(void)  { return __syscall0(SYS_GETPID); }
int64_t getppid(void) { return __syscall0(SYS_GETPPID); }
int64_t fork(void)    { return __syscall0(SYS_FORK); }

int execve(const char *path, const char *const *argv, const char *const *envp) {
    return (int)__syscall3(SYS_EXECVE, (uint64_t)(uintptr_t)path,
                           (uint64_t)(uintptr_t)argv, (uint64_t)(uintptr_t)envp);
}

int64_t waitpid(int64_t pid, int32_t *status) {
    return __syscall2(SYS_WAITPID, (uint64_t)pid, (uint64_t)(uintptr_t)status);
}

void yield(void)             { (void)__syscall0(SYS_YIELD); }
void sleep_ms(uint64_t ms)   { (void)__syscall1(SYS_SLEEP_MS, ms); }
uint64_t uptime_ms(void)     { return (uint64_t)__syscall0(SYS_UPTIME_MS); }

void *brk(void *addr) {
    int64_t r = __syscall1(SYS_BRK, (uint64_t)(uintptr_t)addr);
    return (r < 0) ? (void *)-1 : (void *)(uintptr_t)r;
}

void *sbrk(int64_t inc) {
    int64_t cur = __syscall1(SYS_BRK, 0);
    if (cur < 0) return (void *)-1;
    if (inc == 0) return (void *)(uintptr_t)cur;
    int64_t nxt = __syscall1(SYS_BRK, (uint64_t)(cur + inc));
    if (nxt < 0) return (void *)-1;
    return (void *)(uintptr_t)cur;
}

void *mmap(void *addr, size_t len, int prot, int flags) {
    int64_t r = __syscall4(SYS_MMAP, (uint64_t)(uintptr_t)addr, (uint64_t)len,
                           (uint64_t)(uint32_t)prot, (uint64_t)(uint32_t)flags);
    return (r < 0) ? NULL : (void *)(uintptr_t)r;
}

int munmap(void *addr, size_t len) {
    return (int)__syscall2(SYS_MUNMAP, (uint64_t)(uintptr_t)addr, (uint64_t)len);
}

int pipe(int fds[2]) {
    return (int)__syscall1(SYS_PIPE, (uint64_t)(uintptr_t)fds);
}

int dup2(int oldfd, int newfd) {
    return (int)__syscall2(SYS_DUP2, (uint64_t)(int64_t)oldfd, (uint64_t)(int64_t)newfd);
}

int mkdir(const char *path)  { return (int)__syscall1(SYS_MKDIR, (uint64_t)(uintptr_t)path); }
int unlink(const char *path) { return (int)__syscall1(SYS_UNLINK, (uint64_t)(uintptr_t)path); }

int readdir(const char *path, flux_dirent_t *out, size_t max_entries) {
    return (int)__syscall3(SYS_READDIR, (uint64_t)(uintptr_t)path,
                           (uint64_t)(uintptr_t)out, (uint64_t)max_entries);
}

int stat(const char *path, flux_stat_t *out) {
    return (int)__syscall2(SYS_STAT, (uint64_t)(uintptr_t)path, (uint64_t)(uintptr_t)out);
}

int meminfo(meminfo_t *out) {
    return (int)__syscall1(SYS_MEMINFO, (uint64_t)(uintptr_t)out);
}

int uname(char *buf) {
    return (int)__syscall1(SYS_UNAME, (uint64_t)(uintptr_t)buf);
}

void audio_note(uint32_t freq_hz, uint32_t dur_ms, uint8_t wave, uint8_t vol) {
    (void)__syscall4(SYS_AUDIO_NOTE, freq_hz, dur_ms, wave, vol);
}
