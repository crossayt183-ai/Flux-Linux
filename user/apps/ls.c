#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/";
    flux_dirent_t *ents = (flux_dirent_t *)malloc(sizeof(flux_dirent_t) * 32);
    if (!ents) {
        printf("ls: out of memory\n");
        return 1;
    }
    int n = readdir(path, ents, 32);
    if (n < 0) {
        printf("ls: cannot read '%s'\n", path);
        free(ents);
        return 1;
    }
    for (int i = 0; i < n; i++) {
        printf("%s %-18s %8lu B (ino=%u)\n",
               ents[i].type == 2 ? "<DIR>" : "     ",
               ents[i].name, ents[i].size, ents[i].ino);
    }
    free(ents);
    return 0;
}
