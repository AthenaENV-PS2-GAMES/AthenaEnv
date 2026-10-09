#ifndef ATHENA_SAVEGAME_H
#define ATHENA_SAVEGAME_H
#include <stddef.h>
#include <stdint.h>
/* CRC-32 (IEEE 802.3, the zlib/gzip one) of size bytes, continuing crc
 * (0 to start). Table-driven; the table is built on first use. */
uint32_t athena_savegame_crc32(uint32_t crc,const void *data,size_t size);
#endif
