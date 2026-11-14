#ifndef FLUX_USER_STDLIB_H
#define FLUX_USER_STDLIB_H

#include "flux/types.h"

void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

int   atoi(const char *s);

#endif
