#include "../../../3d_stubs/athena/graphics/owl_packet.h"
#define VIF_MARK 7
/* Match the production fixed-point/clamping conversions for glyph positions. */
static inline int owl_uv_transform(float uv,int max) {
    int value=(int)(uv*16);
    if(value<0) value=0;
    if(value>max*16) value=max*16;
    if(value>=1024*16) value=1024*16-1;
    return value;
}
static inline int owl_coord_transform(float xy,int offset) {
    int value=(int)(xy*16)+offset;
    if(value<0) value=0;
    if(value>=4096*16) value=4096*16-1;
    return value;
}
