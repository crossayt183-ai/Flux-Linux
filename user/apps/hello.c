#include "stdio.h"
#include "unistd.h"

int main(int argc, char **argv) {
    char ubuf[64];
    uname(ubuf);
    printf("[ELF/hello] pid=%ld os=\"%s\" argc=%d\n", getpid(), ubuf, argc);
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d] = \"%s\"\n", i, argv[i]);
    }
    return 0;
}
