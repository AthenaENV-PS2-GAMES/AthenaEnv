/* Include AFTER system/library headers, with ATHENA_MEMORY_CATEGORY defined.
 * Attribution follows the allocation site, not the current JS owner. */
#include <athena/memory.h>
#if ATHENA_MEMORY_DIAGNOSTICS
#define malloc(n) athena_malloc_tagged((n),ATHENA_MEMORY_CATEGORY)
#define calloc(n,s) athena_calloc_tagged((n),(s),ATHENA_MEMORY_CATEGORY)
#define memalign(a,n) athena_memalign_tagged((a),(n),ATHENA_MEMORY_CATEGORY)
#define realloc(p,n) athena_realloc_tagged((p),(n),ATHENA_MEMORY_CATEGORY)
#endif
