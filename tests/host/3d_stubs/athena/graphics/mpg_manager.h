#include <stdbool.h>
#include <stdint.h>
typedef struct { int placeholder; } vu_mpg;
#define VECTOR_UNIT_1 1
vu_mpg *vu_mpg_load_buffer(void *,uint32_t,int,bool);
int vu_mpg_preload(vu_mpg *,bool);
void vu_mpg_unload(vu_mpg *);
void vu1_invalidate_static_data(void);
