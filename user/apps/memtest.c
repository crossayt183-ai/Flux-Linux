#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

int main(int argc, char **argv) {
    if (argc < 3 || strcmp(argv[1], "flux-arg1") != 0 || atoi(argv[2]) != 42) {
        printf("[ELF/memtest] FAIL: unexpected argc=%d\n", argc);
        return 1;
    }

    char *p = (char *)malloc(8192);
    if (!p) return 2;
    strcpy(p, "parent-heap-secret");
    p = (char *)realloc(p, 16384);
    if (!p || strcmp(p, "parent-heap-secret") != 0) return 3;

    uint8_t *anon = (uint8_t *)mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON);
    if (!anon) return 4;
    anon[0] = 0x5A;
    anon[4096] = 0xA5;

    int64_t pid = fork();
    if (pid < 0) return 5;
    if (pid == 0) {
        /* Child writes to heap & anon mmap -> triggers COW isolation */
        strcpy(p, "child-mutated");
        anon[0] = 0xCC;
        exit((anon[4096] == 0xA5) ? 0 : 9);
    }

    int32_t st = -1;
    waitpid(pid, &st);
    if (st != 0 || strcmp(p, "parent-heap-secret") != 0 || anon[0] != 0x5A) {
        printf("[ELF/memtest] FAIL: COW broken st=%d\n", st);
        return 6;
    }

    meminfo_t mi;
    meminfo(&mi);
    printf("[ELF/memtest] OK (argc=%d argv2=%d cow=%lu lazy=%lu free_pages=%lu)\n",
           argc, atoi(argv[2]), mi.cow_faults, mi.lazy_faults, mi.free_pages);

    munmap(anon, 8192);
    free(p);
    return 0;
}
