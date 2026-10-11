#ifndef ATHENA_TEXTURE_TEST_IMAGE_H
#define ATHENA_TEXTURE_TEST_IMAGE_H
#include <stddef.h>
#include <athena/graphics.h>
#define ATHENA_IMAGE_LOAD_OPEN 1
typedef struct { uint32_t width,height,psm,*mem; int error; uint32_t *clut; uint8_t clut_psm; } AthenaImageBuffer;
int athena_image_decode(const char *,AthenaImageBuffer *);
int athena_image_decode_memory(const void *,size_t,AthenaImageBuffer *);
void athena_image_buffer_release(AthenaImageBuffer *);
#endif
