#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <kernel.h>
#include <delaythread.h>

#include <athena/imagelist.h>
#include <athena/mutex.h>
#include <athena/thread.h>

/* libpng and libjpeg need more stack than the thread default. */
#define DECODER_STACK_SIZE (64 * 1024)
/* Idle poll interval; lets the worker observe runtime shutdown requests. */
#define DECODER_IDLE_US 1000

struct AthenaImageDecoder {
    AthenaThread *thread;
    AthenaMutex *mutex;
    /* Protected by `mutex`. */
    AthenaImageDecodeResult *requests;
    AthenaImageDecodeResult *results;
    uint32_t result_bytes;
    uint32_t peak_bytes;
    uint64_t decode_ticks;
    bool stop;
    bool exited;
    /* Immutable while the worker runs. */
    uint32_t max_memory;
};

void athena_image_decode_result_free(AthenaImageDecodeResult *result)
{
    if (!result)
        return;
    athena_image_buffer_release(&result->buffer);
    free(result->path);
    free(result);
}

static void decoder_free_all(AthenaImageDecodeResult *result)
{
    while (result) {
        AthenaImageDecodeResult *next = result->next;
        athena_image_decode_result_free(result);
        result = next;
    }
}

static void decoder_append(AthenaImageDecodeResult **head,
    AthenaImageDecodeResult *result)
{
    while (*head)
        head = &(*head)->next;
    *head = result;
}

static AthenaImageDecodeResult *decoder_unlink(AthenaImageDecodeResult **head,
    uint32_t id)
{
    for (; *head; head = &(*head)->next) {
        AthenaImageDecodeResult *result = *head;
        if (result->id == id) {
            *head = result->next;
            result->next = NULL;
            return result;
        }
    }
    return NULL;
}

static void decoder_consume_bytes(AthenaImageDecoder *decoder, uint32_t bytes)
{
    decoder->result_bytes -= bytes <= decoder->result_bytes ?
        bytes : decoder->result_bytes;
}

/* Only file I/O and CPU decoding; never touches images, surfaces or VRAM. */
static void decoder_worker(void *arg)
{
    AthenaImageDecoder *decoder = arg;

    for (;;) {
        AthenaImageDecodeResult *request = NULL;
        clock_t started;
        clock_t elapsed;

        if (athena_mutex_core_lock(decoder->mutex) < 0)
            break;
        if (decoder->stop || athena_thread_core_stop_requested()) {
            decoder->exited = true;
            athena_mutex_core_unlock(decoder->mutex);
            break;
        }
        if (decoder->requests && (!decoder->results ||
            decoder->result_bytes < decoder->max_memory)) {
            request = decoder->requests;
            decoder->requests = request->next;
            request->next = NULL;
        }
        athena_mutex_core_unlock(decoder->mutex);

        if (!request) {
            DelayThread(DECODER_IDLE_US);
            continue;
        }
        started = clock();
        athena_image_decode(request->path, &request->buffer);
        elapsed = clock() - started;

        if (athena_mutex_core_lock(decoder->mutex) < 0) {
            athena_image_decode_result_free(request);
            break;
        }
        decoder->decode_ticks += (uint64_t)elapsed;
        decoder_append(&decoder->results, request);
        if (request->buffer.bytes <= UINT32_MAX - decoder->result_bytes)
            decoder->result_bytes += request->buffer.bytes;
        else
            decoder->result_bytes = UINT32_MAX;
        if (decoder->result_bytes > decoder->peak_bytes)
            decoder->peak_bytes = decoder->result_bytes;
        athena_mutex_core_unlock(decoder->mutex);
    }
    athena_thread_core_worker_finished(decoder->thread);
    ExitThread();
}

AthenaImageDecoder *athena_image_decoder_create(uint32_t max_memory)
{
    AthenaImageDecoder *decoder = calloc(1, sizeof(*decoder));

    if (!decoder)
        return NULL;
    decoder->max_memory = max_memory;
    decoder->mutex = athena_mutex_core_create();
    if (!decoder->mutex)
        goto fail;
    decoder->thread = athena_thread_core_create("ImageList decoder",
        decoder_worker, decoder, DECODER_STACK_SIZE,
        ATHENA_THREAD_DEFAULT_PRIORITY + 1);
    if (!decoder->thread)
        goto fail;
    if (athena_thread_core_start(decoder->thread) < 0) {
        /* Never started, so destroy() also finalizes the thread. */
        athena_thread_core_destroy(decoder->thread);
        decoder->thread = NULL;
        goto fail;
    }
    return decoder;

fail:
    if (decoder->mutex)
        athena_mutex_core_destroy(decoder->mutex);
    free(decoder);
    return NULL;
}

void athena_image_decoder_stats(AthenaImageDecoder *decoder,
    AthenaImageDecoderStats *out)
{
    memset(out, 0, sizeof(*out));
    if (!decoder || athena_mutex_core_lock(decoder->mutex) < 0)
        return;
    out->buffered_bytes = decoder->result_bytes;
    out->peak_bytes = decoder->peak_bytes;
    out->decode_ticks = decoder->decode_ticks;
    out->exited = decoder->exited;
    athena_mutex_core_unlock(decoder->mutex);
}

/* Joins the worker; it may already have exited on a runtime stop request. */
void athena_image_decoder_destroy(AthenaImageDecoder *decoder,
    AthenaImageDecoderStats *final)
{
    int attempts;

    if (!decoder)
        return;
    if (athena_mutex_core_lock(decoder->mutex) >= 0) {
        decoder->stop = true;
        athena_mutex_core_unlock(decoder->mutex);
    }
    athena_thread_core_stop(decoder->thread);
    athena_thread_core_wait(decoder->thread);
    /*
     * worker_finished() signals just before ExitThread(); give the worker
     * time to become dormant before its stack is released.
     */
    for (attempts = 0; attempts < 100 &&
        athena_thread_core_get_status(decoder->thread) != THS_DORMANT;
        ++attempts)
        DelayThread(100);
    athena_thread_core_finalize(decoder->thread);
    if (final) {
        final->buffered_bytes = decoder->result_bytes;
        final->peak_bytes = decoder->peak_bytes;
        final->decode_ticks = decoder->decode_ticks;
        final->exited = true;
    }
    athena_mutex_core_destroy(decoder->mutex);
    decoder_free_all(decoder->requests);
    decoder_free_all(decoder->results);
    free(decoder);
}

int athena_image_decoder_submit(AthenaImageDecoder *decoder, uint32_t id,
    const char *path)
{
    AthenaImageDecodeResult *request = calloc(1, sizeof(*request));

    if (request)
        request->path = strdup(path);
    if (!request || !request->path) {
        free(request);
        return -1;
    }
    request->id = id;
    if (athena_mutex_core_lock(decoder->mutex) < 0) {
        athena_image_decode_result_free(request);
        return -1;
    }
    decoder_append(&decoder->requests, request);
    athena_mutex_core_unlock(decoder->mutex);
    return 0;
}

AthenaImageDecodeResult *athena_image_decoder_take(AthenaImageDecoder *decoder,
    bool *exited)
{
    AthenaImageDecodeResult *result = NULL;

    *exited = false;
    if (athena_mutex_core_lock(decoder->mutex) < 0) {
        *exited = true;
        return NULL;
    }
    if (decoder->results) {
        result = decoder->results;
        decoder->results = result->next;
        result->next = NULL;
        decoder_consume_bytes(decoder, result->buffer.bytes);
    } else {
        *exited = decoder->exited;
    }
    athena_mutex_core_unlock(decoder->mutex);
    return result;
}

void athena_image_decoder_discard(AthenaImageDecoder *decoder, uint32_t id)
{
    AthenaImageDecodeResult *result;

    if (athena_mutex_core_lock(decoder->mutex) < 0)
        return;
    result = decoder_unlink(&decoder->requests, id);
    if (!result) {
        result = decoder_unlink(&decoder->results, id);
        if (result)
            decoder_consume_bytes(decoder, result->buffer.bytes);
    }
    athena_mutex_core_unlock(decoder->mutex);
    athena_image_decode_result_free(result);
}
