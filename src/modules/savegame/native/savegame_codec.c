#include <athena/savegame.h>
static uint32_t table[256];
static int ready;
static void build(void) {
    for(uint32_t n=0;n<256;n++) {
        uint32_t c=n;
        for(int k=0;k<8;k++) c=c&1?0xEDB88320u^(c>>1):c>>1;
        table[n]=c;
    }
    ready=1;
}
uint32_t athena_savegame_crc32(uint32_t crc,const void *data,size_t size) {
    if(!ready) build();
    const uint8_t *p=data;
    crc=~crc;
    while(size--) crc=table[(crc^*p++)&0xFF]^(crc>>8);
    return ~crc;
}
