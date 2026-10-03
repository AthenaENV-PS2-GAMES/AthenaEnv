#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cutils.h"
int main(void) {
    const uint32_t values[]={0,32,36,0x80000020u,0x80000023u,0xdeadbeefu,0xffffffffu};
    uint8_t bytes[32];
    for(size_t offset=0;offset<16;offset++) {
        for(size_t i=0;i<sizeof(values)/sizeof(values[0]);i++) {
            memset(bytes,0xa5,sizeof(bytes));
            uint32_t v=values[i];
            for(unsigned j=0;j<4;j++) bytes[offset+j]=(uint8_t)(v>>(8*j));
            assert(get_u32(bytes+offset)==v);
            assert(get_i32(bytes+offset)==(int32_t)v);
        }
    }
    puts("QuickJS PS2 unaligned operand tests passed");
    return 0;
}
