#ifndef ATHENA_TEST_MALLOC_H
#define ATHENA_TEST_MALLOC_H
#include <stddef.h>
struct mallinfo {
    size_t arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks;
    size_t uordblks, fordblks, keepcost;
};
size_t malloc_usable_size(void *ptr);
void *memalign(size_t alignment, size_t size);
struct mallinfo _mallinfo_r(void *reent);
#endif
