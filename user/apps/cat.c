#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: cat <path>...\n");
        return 1;
    }
    char *buf = (char *)malloc(1024);
    if (!buf) return 1;

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: cannot open '%s'\n", argv[i]);
            rc = 1;
            continue;
        }
        ssize_t n;
        while ((n = read(fd, buf, 1024)) > 0) {
            write(1, buf, (size_t)n);
        }
        close(fd);
    }
    free(buf);
    return rc;
}
