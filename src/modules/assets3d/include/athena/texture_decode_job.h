#ifndef ATHENA_TEXTURE_DECODE_JOB_H
#define ATHENA_TEXTURE_DECODE_JOB_H
#include <stddef.h>
#include <stdint.h>
#include <athena/job.h>
/*
 * Reading and decoding a PNG/JPEG/BMP file into the retained pixel copy of a Texture3D on a
 * worker of the job pool (athena/job.h), so a slow device and the decoder (about 80 ms for a
 * 256x256 PNG on the EE) never run inside a frame. The worker owns everything it touches; the
 * script thread then adopts the pixels (athena_texture3d_adopt) in O(1).
 */
#define ATHENA_TEXTURE_DECODE_CANCELLED (-100)
#define ATHENA_TEXTURE_DECODE_TOO_LARGE (-101)
/* The job's result codes otherwise are the negative AthenaModel3DResult of the decoder. */
AthenaJob *athena_texture_decode_submit(const char *path,size_t max_bytes);
/* The finished job's pixels (128-byte aligned; the caller owns them), or NULL. */
uint32_t *athena_texture_decode_take(AthenaJob *job,uint32_t *width,uint32_t *height);
#endif
