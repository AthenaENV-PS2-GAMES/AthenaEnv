#ifndef ATHENA_IMAGELIST_H
#define ATHENA_IMAGELIST_H

#include <stdbool.h>
#include <stdint.h>

#include <athena/image.h>

/*
 * Background image decoder. A worker thread performs file I/O and CPU
 * decoding only; results are applied to images and uploaded to VRAM by the
 * caller, on its own thread (athena_image_apply_buffer / athena_image_upload).
 *
 *   AthenaImageDecoder *decoder = athena_image_decoder_create(0);
 *   athena_image_decoder_submit(decoder, 1, "mass:/sprite.png");
 *   ...
 *   bool exited;
 *   AthenaImageDecodeResult *result = athena_image_decoder_take(decoder, &exited);
 *   if (result) {
 *       athena_image_apply_buffer(image, result->path, &result->buffer);
 *       athena_image_decode_result_free(result);
 *   }
 */

typedef struct AthenaImageDecoder AthenaImageDecoder;

typedef struct AthenaImageDecodeResult {
    struct AthenaImageDecodeResult *next;  /* internal */
    uint32_t id;                           /* as passed to submit() */
    char *path;
    AthenaImageBuffer buffer;              /* check buffer.bytes / the decode status */
} AthenaImageDecodeResult;

typedef struct {
    uint32_t buffered_bytes;  /* decoded bytes not taken yet */
    uint32_t peak_bytes;
    uint64_t decode_ticks;    /* clock() ticks spent decoding */
    bool exited;              /* the worker stopped on its own (runtime stop request) */
} AthenaImageDecoderStats;

/*
 * Starts the worker. With max_memory == 0 the worker holds at most one
 * undelivered result; otherwise it decodes ahead until the undelivered
 * results reach max_memory bytes.
 */
AthenaImageDecoder *athena_image_decoder_create(uint32_t max_memory);

/* Joins the worker and frees queued work. *final receives the last stats (optional). */
void athena_image_decoder_destroy(AthenaImageDecoder *decoder, AthenaImageDecoderStats *final);

/* Queues a file; returns 0, or -1 when out of memory or the decoder is unusable. */
int athena_image_decoder_submit(AthenaImageDecoder *decoder, uint32_t id, const char *path);

/*
 * Oldest decoded result, or NULL. When NULL, *exited tells whether the
 * worker has stopped and no more results will arrive.
 */
AthenaImageDecodeResult *athena_image_decoder_take(AthenaImageDecoder *decoder, bool *exited);

/* Drops a request that is queued or already decoded. One being decoded is dropped by the caller on take(). */
void athena_image_decoder_discard(AthenaImageDecoder *decoder, uint32_t id);

void athena_image_decoder_stats(AthenaImageDecoder *decoder, AthenaImageDecoderStats *out);

void athena_image_decode_result_free(AthenaImageDecodeResult *result);

/*
 * Canonical form of a path, used to deduplicate requests: collapses
 * separators, "." and ".." segments, keeps device prefixes such as "mass:"
 * or "cdrom0:" and never climbs above a root or device. Caller frees.
 */
char *athena_path_normalize(const char *path);

#endif /* ATHENA_IMAGELIST_H */
