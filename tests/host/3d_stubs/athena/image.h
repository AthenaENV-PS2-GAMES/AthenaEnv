#ifndef ATHENA_TEXTURE_TEST_IMAGE_H
#define ATHENA_TEXTURE_TEST_IMAGE_H
#include <athena/graphics.h>
#define ATHENA_IMAGE_LOAD_OPEN 1
typedef struct { uint32_t width,height,psm,*mem; int error; } AthenaImageBuffer;
int athena_image_decode(const char *,AthenaImageBuffer *);
void athena_image_buffer_release(AthenaImageBuffer *);
#endif
