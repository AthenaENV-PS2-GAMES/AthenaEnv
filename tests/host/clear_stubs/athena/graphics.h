#include "../../font_stubs/athena/graphics.h"
#define GS_TEST_1 0x47
#define GS_XYOFFSET_1 0x18
#define GS_SETREG_XYOFFSET(x,y) ((uint64_t)(x)|((uint64_t)(y)<<32))
#define GS_SETREG_XYZ(x,y,z) ((uint64_t)(x)|((uint64_t)(y)<<16)|((uint64_t)(z)<<32))
