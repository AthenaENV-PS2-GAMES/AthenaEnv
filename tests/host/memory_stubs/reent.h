#ifndef TEST_REENT_H
#define TEST_REENT_H
#include <stddef.h>
#define _REENT NULL
void *_malloc_r(void *, size_t);
void *_calloc_r(void *, size_t, size_t);
void *_realloc_r(void *, void *, size_t);
void *_memalign_r(void *, size_t, size_t);
void _free_r(void *, void *);
size_t _malloc_usable_size_r(void *, void *);
#endif
