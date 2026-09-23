#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <kernel.h>
#include <delaythread.h>

#include <ath_env.h>

#include "../../mutex/native/mutex.h"
#include "../../thread/native/thread.h"
#include "../../image/native/image.h"
#include "../../image/quickjs/ath_image.h"
#include "ath_imagelist.h"

#define IMAGELIST_PRIORITY_HIGH 0
#define IMAGELIST_PRIORITY_NORMAL 1
#define IMAGELIST_PRIORITY_LOW 2

/* When process() makes a completed image resident in VRAM. */
#define IMAGELIST_UPLOAD_DRAW 0
#define IMAGELIST_UPLOAD_BIND 1
#define IMAGELIST_UPLOAD_LOCK 2

/* libpng and libjpeg need more stack than the thread default. */
#define IMAGELIST_WORKER_STACK_SIZE (64 * 1024)
/* Idle poll interval; lets the worker observe runtime shutdown requests. */
#define IMAGELIST_WORKER_IDLE_US 1000
/* Requests handed to the worker at once when maxMemory allows decode-ahead. */
#define IMAGELIST_WORKER_MAX_SUBMITTED 4
/* Upper bound for the cacheSize option. */
#define IMAGELIST_MAX_CACHE_SIZE 1024

typedef struct {
    JSValue on_load;
    JSValue on_error;
} ImageListCallback;

typedef struct {
    /* Owns the Image object; resolve it with athena_image_peek(). */
    JSValue image_ref;
    /* Normalized path used to deduplicate pending requests. */
    char *key;
    /* Path as requested; used for I/O and reported in errors. */
    char *path;
    uint32_t id;
    int priority;
    /* IMAGELIST_UPLOAD_*; the strongest request of all callers wins. */
    int upload;
    /* Handed to the worker; the request is in the loading state. */
    bool submitted;
    /* Served from the cache; only its callbacks remain to be dispatched. */
    bool cached;
    ImageListCallback *callbacks;
    unsigned int callback_count;
    unsigned int callback_capacity;
} ImageListJob;

/*
 * Work item exchanged with the worker. The main thread allocates it, the
 * worker owns it while decoding and publishes it in `results`, after which it
 * belongs to the main thread again. It never holds QuickJS or graphics state.
 */
typedef struct ImageListRequest {
    struct ImageListRequest *next;
    uint32_t id;
    char *path;
    AthenaImageBuffer buffer;
} ImageListRequest;

typedef struct {
    AthenaThread *thread;
    AthenaMutex *mutex;
    /* Protected by `mutex`. */
    ImageListRequest *requests;
    ImageListRequest *results;
    uint32_t result_bytes;
    uint32_t peak_bytes;
    uint64_t decode_ticks;
    bool stop;
    bool exited;
    /* Immutable while the worker runs. */
    uint32_t max_memory;
} ImageListWorker;

/*
 * Completed image retained by the cache. The cache owns one reference to the
 * Image; its surface is not locked, so the texture manager may still evict it
 * from VRAM.
 */
typedef struct {
    char *key;
    JSValue image_ref;
} ImageListCacheEntry;

typedef struct {
    ImageListJob *jobs;
    unsigned int count;
    unsigned int capacity;
    uint32_t next_id;
    unsigned int completed;
    unsigned int failed;
    unsigned int cancelled;
    unsigned int cache_hits;
    uint32_t workers;
    uint32_t max_memory;
    uint32_t peak_bytes;
    /* clock() ticks spent decoding on this thread and building surfaces. */
    uint64_t decode_ticks;
    uint64_t apply_ticks;
    uint64_t upload_ticks;
    /* Most recently used first; at most cache_size entries. */
    ImageListCacheEntry *cache;
    unsigned int cache_count;
    uint32_t cache_size;
    /* Set by close(); the list accepts no new requests. */
    bool closed;
    ImageListWorker worker;
} AthenaImageList;

static JSClassID imagelist_class_id;

static double imagelist_ticks_to_ms(uint64_t ticks)
{
    return (double)ticks * 1000.0 / (double)CLOCKS_PER_SEC;
}

static void imagelist_request_free(ImageListRequest *request)
{
    if (!request)
        return;
    athena_image_buffer_release(&request->buffer);
    free(request->path);
    free(request);
}

static void imagelist_request_free_all(ImageListRequest *request)
{
    while (request) {
        ImageListRequest *next = request->next;
        imagelist_request_free(request);
        request = next;
    }
}

static void imagelist_request_append(ImageListRequest **head,
    ImageListRequest *request)
{
    while (*head)
        head = &(*head)->next;
    *head = request;
}

static ImageListRequest *imagelist_request_unlink(ImageListRequest **head,
    uint32_t id)
{
    for (; *head; head = &(*head)->next) {
        ImageListRequest *request = *head;
        if (request->id == id) {
            *head = request->next;
            request->next = NULL;
            return request;
        }
    }
    return NULL;
}

/*
 * Worker thread. It only performs file I/O and CPU decoding; results are
 * applied to Image objects, surfaces and VRAM on the main thread.
 */
static void imagelist_worker(void *arg)
{
    AthenaImageList *list = arg;
    ImageListWorker *worker = &list->worker;

    for (;;) {
        ImageListRequest *request = NULL;
        clock_t started;
        clock_t elapsed;

        if (athena_mutex_core_lock(worker->mutex) < 0)
            break;
        if (worker->stop || athena_thread_core_stop_requested()) {
            worker->exited = true;
            athena_mutex_core_unlock(worker->mutex);
            break;
        }
        if (worker->requests && (!worker->results ||
            worker->result_bytes < worker->max_memory)) {
            request = worker->requests;
            worker->requests = request->next;
            request->next = NULL;
        }
        athena_mutex_core_unlock(worker->mutex);

        if (!request) {
            DelayThread(IMAGELIST_WORKER_IDLE_US);
            continue;
        }
        started = clock();
        athena_image_decode(request->path, &request->buffer);
        elapsed = clock() - started;

        if (athena_mutex_core_lock(worker->mutex) < 0) {
            imagelist_request_free(request);
            break;
        }
        worker->decode_ticks += (uint64_t)elapsed;
        imagelist_request_append(&worker->results, request);
        if (request->buffer.bytes <= UINT32_MAX - worker->result_bytes)
            worker->result_bytes += request->buffer.bytes;
        else
            worker->result_bytes = UINT32_MAX;
        if (worker->result_bytes > worker->peak_bytes)
            worker->peak_bytes = worker->result_bytes;
        athena_mutex_core_unlock(worker->mutex);
    }
    athena_thread_core_worker_finished(worker->thread);
    ExitThread();
}

static int imagelist_worker_start(AthenaImageList *list)
{
    ImageListWorker *worker = &list->worker;

    if (list->workers == 0)
        return 0;
    worker->max_memory = list->max_memory;
    worker->mutex = athena_mutex_core_create();
    if (!worker->mutex)
        return -1;
    worker->thread = athena_thread_core_create("ImageList decoder",
        imagelist_worker, list, IMAGELIST_WORKER_STACK_SIZE,
        ATHENA_THREAD_DEFAULT_PRIORITY + 1);
    if (!worker->thread) {
        athena_mutex_core_destroy(worker->mutex);
        worker->mutex = NULL;
        return -1;
    }
    if (athena_thread_core_start(worker->thread) < 0) {
        /* Never started, so destroy() also finalizes the thread. */
        athena_thread_core_destroy(worker->thread);
        athena_mutex_core_destroy(worker->mutex);
        worker->thread = NULL;
        worker->mutex = NULL;
        return -1;
    }
    return 0;
}

/* Joins the worker; it may already have exited on a runtime stop request. */
static void imagelist_worker_stop(AthenaImageList *list)
{
    ImageListWorker *worker = &list->worker;
    int attempts;

    if (!worker->thread)
        return;
    if (athena_mutex_core_lock(worker->mutex) >= 0) {
        worker->stop = true;
        athena_mutex_core_unlock(worker->mutex);
    }
    athena_thread_core_stop(worker->thread);
    athena_thread_core_wait(worker->thread);
    /*
     * worker_finished() signals just before ExitThread(); give the worker
     * time to become dormant before its stack is released.
     */
    for (attempts = 0; attempts < 100 &&
        athena_thread_core_get_status(worker->thread) != THS_DORMANT;
        ++attempts)
        DelayThread(100);
    athena_thread_core_finalize(worker->thread);
    athena_mutex_core_destroy(worker->mutex);
    imagelist_request_free_all(worker->requests);
    imagelist_request_free_all(worker->results);
    if (worker->peak_bytes > list->peak_bytes)
        list->peak_bytes = worker->peak_bytes;
    list->decode_ticks += worker->decode_ticks;
    memset(worker, 0, sizeof(*worker));
    list->workers = 0;
}

static int imagelist_argc(JSContext *ctx, int argc, int minimum, int maximum,
    const char *name)
{
    if (argc < minimum || argc > maximum) {
        if (minimum == maximum)
            JS_ThrowTypeError(ctx, "%s expects exactly %d arguments", name,
                minimum);
        else
            JS_ThrowTypeError(ctx, "%s expects between %d and %d arguments",
                name, minimum, maximum);
        return 0;
    }
    return 1;
}

static AthenaImageList *imagelist_this(JSContext *ctx, JSValueConst value)
{
    return JS_GetOpaque2(ctx, value, imagelist_class_id);
}

static bool imagelist_is_separator(char value)
{
    return value == '/' || value == '\\';
}

/*
 * Produces the deduplication key for a path: collapses separators, "." and
 * ".." segments, keeps device prefixes such as "mass:" or "cdrom0:" and never
 * climbs above a root or device.
 */
static char *imagelist_normalize_path(const char *path)
{
    char *normalized;
    size_t *starts;
    size_t length;
    size_t input = 0;
    size_t output = 0;
    size_t root = 0;
    unsigned int depth = 0;
    bool anchored = false;

    if (!path)
        return NULL;
    length = strlen(path);
    normalized = malloc(length + 2);
    starts = malloc((length + 1) * sizeof(*starts));
    if (!normalized || !starts) {
        free(normalized);
        free(starts);
        return NULL;
    }
    while (input < length && !imagelist_is_separator(path[input]) &&
        path[input] != ':')
        input++;
    if (input < length && path[input] == ':') {
        input++;
        memcpy(normalized, path, input);
        output = input;
        anchored = true;
    } else {
        input = 0;
    }
    if (input < length && imagelist_is_separator(path[input])) {
        normalized[output++] = '/';
        anchored = true;
    }
    root = output;
    while (input < length) {
        size_t begin;
        size_t segment_length;

        while (input < length && imagelist_is_separator(path[input]))
            input++;
        begin = input;
        while (input < length && !imagelist_is_separator(path[input]))
            input++;
        segment_length = input - begin;
        if (segment_length == 0 ||
            (segment_length == 1 && path[begin] == '.'))
            continue;
        if (segment_length == 2 && path[begin] == '.' &&
            path[begin + 1] == '.') {
            if (depth > 0) {
                output = starts[--depth];
                continue;
            }
            if (anchored)
                continue;
            /* A leading ".." of a relative path can never be removed. */
            if (output > root)
                normalized[output++] = '/';
            memcpy(normalized + output, path + begin, 2);
            output += 2;
            continue;
        }
        starts[depth++] = output;
        if (output > root)
            normalized[output++] = '/';
        memcpy(normalized + output, path + begin, segment_length);
        output += segment_length;
    }
    if (output == 0)
        normalized[output++] = '.';
    normalized[output] = '\0';
    free(starts);
    return normalized;
}

static int imagelist_callback_reserve(ImageListJob *job, unsigned int needed)
{
    ImageListCallback *callbacks;
    unsigned int capacity = job->callback_capacity ?
        job->callback_capacity * 2 : 2;

    if (needed <= job->callback_capacity)
        return 1;
    while (capacity < needed)
        capacity *= 2;
    callbacks = realloc(job->callbacks, capacity * sizeof(*callbacks));
    if (!callbacks)
        return 0;
    job->callbacks = callbacks;
    job->callback_capacity = capacity;
    return 1;
}

static void imagelist_callbacks_release(JSRuntime *rt, ImageListJob *job)
{
    unsigned int i;

    for (i = 0; i < job->callback_count; ++i) {
        JS_FreeValueRT(rt, job->callbacks[i].on_load);
        JS_FreeValueRT(rt, job->callbacks[i].on_error);
    }
    free(job->callbacks);
    job->callbacks = NULL;
    job->callback_count = 0;
    job->callback_capacity = 0;
}

static void imagelist_job_release(JSRuntime *rt, ImageListJob *job,
    int mark_cancelled)
{
    AthenaImage *image = athena_image_peek(job->image_ref);

    if (image && mark_cancelled) {
        image->loading = false;
        image->failed = false;
        image->status = ATHENA_IMAGE_STATUS_CANCELLED;
    }
    JS_FreeValueRT(rt, job->image_ref);
    imagelist_callbacks_release(rt, job);
    free(job->key);
    free(job->path);
    memset(job, 0, sizeof(*job));
}

static void imagelist_cache_remove(JSRuntime *rt, AthenaImageList *list,
    unsigned int index)
{
    ImageListCacheEntry *entry = &list->cache[index];

    JS_FreeValueRT(rt, entry->image_ref);
    free(entry->key);
    memmove(entry, entry + 1,
        (list->cache_count - index - 1) * sizeof(*list->cache));
    list->cache_count--;
}

static unsigned int imagelist_cache_clear(JSRuntime *rt, AthenaImageList *list)
{
    unsigned int removed = list->cache_count;

    while (list->cache_count > 0)
        imagelist_cache_remove(rt, list, list->cache_count - 1);
    return removed;
}

/*
 * Returns the cache entry for `key` and marks it most recently used. Entries
 * whose Image was freed or no longer holds pixels are evicted.
 */
static ImageListCacheEntry *imagelist_cache_lookup(JSRuntime *rt,
    AthenaImageList *list, const char *key)
{
    ImageListCacheEntry entry;
    AthenaImage *image;
    unsigned int i;

    for (i = 0; i < list->cache_count; ++i) {
        if (strcmp(list->cache[i].key, key) == 0)
            break;
    }
    if (i == list->cache_count)
        return NULL;
    image = athena_image_peek(list->cache[i].image_ref);
    if (!image || !athena_image_is_loaded(image)) {
        imagelist_cache_remove(rt, list, i);
        return NULL;
    }
    entry = list->cache[i];
    memmove(&list->cache[1], &list->cache[0], i * sizeof(*list->cache));
    list->cache[0] = entry;
    return &list->cache[0];
}

/* Retains a completed image, evicting the least recently used entry. */
static void imagelist_cache_store(JSContext *ctx, AthenaImageList *list,
    const ImageListJob *job)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    char *key;

    if (list->cache_size == 0 || job->cached ||
        imagelist_cache_lookup(rt, list, job->key))
        return;
    if (!list->cache) {
        list->cache = calloc(list->cache_size, sizeof(*list->cache));
        if (!list->cache)
            return;
    }
    key = strdup(job->key);
    if (!key)
        return;
    if (list->cache_count == list->cache_size)
        imagelist_cache_remove(rt, list, list->cache_count - 1);
    memmove(&list->cache[1], &list->cache[0],
        list->cache_count * sizeof(*list->cache));
    list->cache[0].key = key;
    list->cache[0].image_ref = JS_DupValue(ctx, job->image_ref);
    list->cache_count++;
}

/* Drops a submitted request the worker has not started or already finished. */
static void imagelist_worker_discard(AthenaImageList *list, uint32_t id)
{
    ImageListWorker *worker = &list->worker;
    ImageListRequest *request;

    if (!worker->thread || athena_mutex_core_lock(worker->mutex) < 0)
        return;
    request = imagelist_request_unlink(&worker->requests, id);
    if (!request) {
        request = imagelist_request_unlink(&worker->results, id);
        if (request)
            worker->result_bytes -= request->buffer.bytes <=
                worker->result_bytes ? request->buffer.bytes :
                worker->result_bytes;
    }
    athena_mutex_core_unlock(worker->mutex);
    /* A request being decoded is discarded when its result is consumed. */
    imagelist_request_free(request);
}

static void imagelist_remove_job(AthenaImageList *list, unsigned int index,
    ImageListJob *out)
{
    *out = list->jobs[index];
    memmove(&list->jobs[index], &list->jobs[index + 1],
        (list->count - index - 1) * sizeof(*list->jobs));
    list->count--;
}

static void imagelist_cancel_at(JSRuntime *rt, AthenaImageList *list,
    unsigned int index)
{
    ImageListJob job;

    imagelist_remove_job(list, index, &job);
    if (job.submitted)
        imagelist_worker_discard(list, job.id);
    list->cancelled++;
    /* A cached Image is already loaded; only its callbacks are dropped. */
    imagelist_job_release(rt, &job, !job.cached);
}

static void imagelist_finalizer(JSRuntime *rt, JSValue value)
{
    AthenaImageList *list = JS_GetOpaque(value, imagelist_class_id);
    unsigned int i;

    if (!list)
        return;
    imagelist_worker_stop(list);
    for (i = 0; i < list->count; ++i)
        imagelist_job_release(rt, &list->jobs[i], !list->jobs[i].cached);
    imagelist_cache_clear(rt, list);
    free(list->cache);
    free(list->jobs);
    free(list);
    JS_SetOpaque(value, NULL);
}

static int imagelist_reserve(AthenaImageList *list, unsigned int needed)
{
    ImageListJob *jobs;
    unsigned int capacity = list->capacity ? list->capacity * 2 : 4;

    if (needed <= list->capacity)
        return 1;
    while (capacity < needed)
        capacity *= 2;
    jobs = realloc(list->jobs, capacity * sizeof(*jobs));
    if (!jobs)
        return 0;
    list->jobs = jobs;
    list->capacity = capacity;
    return 1;
}

static int imagelist_callback_option(JSContext *ctx, JSValueConst options,
    const char *name, JSValue *callback)
{
    JSValue value = JS_GetPropertyStr(ctx, options, name);

    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value) && !JS_IsFunction(ctx, value)) {
        JS_FreeValue(ctx, value);
        JS_ThrowTypeError(ctx, "ImageList options.%s must be a function",
            name);
        return 0;
    }
    *callback = value;
    return 1;
}

static int imagelist_upload_option(JSContext *ctx, JSValueConst options,
    int *upload)
{
    JSValue value = JS_GetPropertyStr(ctx, options, "upload");
    const char *mode;

    if (JS_IsException(value))
        return 0;
    if (JS_IsUndefined(value))
        return 1;
    mode = JS_IsString(value) ? JS_ToCString(ctx, value) : NULL;
    JS_FreeValue(ctx, value);
    if (mode && strcmp(mode, "draw") == 0)
        *upload = IMAGELIST_UPLOAD_DRAW;
    else if (mode && strcmp(mode, "bind") == 0)
        *upload = IMAGELIST_UPLOAD_BIND;
    else if (mode && strcmp(mode, "lock") == 0)
        *upload = IMAGELIST_UPLOAD_LOCK;
    else {
        if (mode)
            JS_FreeCString(ctx, mode);
        JS_ThrowRangeError(ctx,
            "ImageList options.upload must be \"draw\", \"bind\" or \"lock\"");
        return 0;
    }
    JS_FreeCString(ctx, mode);
    return 1;
}

static int imagelist_options(JSContext *ctx, JSValueConst value, bool *delayed,
    int *priority, int *upload, JSValue *on_load, JSValue *on_error)
{
    JSValue option;
    uint32_t priority_value;

    if (JS_IsUndefined(value))
        return 1;
    if (!JS_IsObject(value) || JS_IsArray(ctx, value)) {
        JS_ThrowTypeError(ctx, "ImageList options must be an object");
        return 0;
    }
    option = JS_GetPropertyStr(ctx, value, "delayed");
    if (JS_IsException(option))
        return 0;
    if (!JS_IsUndefined(option)) {
        if (!JS_IsBool(option)) {
            JS_FreeValue(ctx, option);
            JS_ThrowTypeError(ctx, "ImageList options.delayed must be a boolean");
            return 0;
        }
        *delayed = JS_ToBool(ctx, option) != 0;
    }
    JS_FreeValue(ctx, option);
    option = JS_GetPropertyStr(ctx, value, "priority");
    if (JS_IsException(option))
        return 0;
    if (!JS_IsUndefined(option)) {
        if (!JS_IsNumber(option) ||
            JS_ToUint32(ctx, &priority_value, option) ||
            priority_value > IMAGELIST_PRIORITY_LOW) {
            JS_FreeValue(ctx, option);
            JS_ThrowRangeError(ctx,
                "ImageList options.priority must be HIGH, NORMAL or LOW");
            return 0;
        }
        *priority = (int)priority_value;
    }
    JS_FreeValue(ctx, option);
    if (!imagelist_upload_option(ctx, value, upload))
        return 0;
    if (!imagelist_callback_option(ctx, value, "onLoad", on_load))
        return 0;
    return imagelist_callback_option(ctx, value, "onError", on_error);
}

static int imagelist_find_job(AthenaImageList *list, const char *key)
{
    unsigned int i;

    for (i = 0; i < list->count; ++i) {
        if (strcmp(list->jobs[i].key, key) == 0)
            return (int)i;
    }
    return -1;
}

static int imagelist_find_id(AthenaImageList *list, uint32_t id)
{
    unsigned int i;

    for (i = 0; i < list->count; ++i) {
        if (list->jobs[i].id == id)
            return (int)i;
    }
    return -1;
}

/* Inserts after every job of equal or higher priority, keeping FIFO order. */
static void imagelist_insert_job(AthenaImageList *list, ImageListJob *job)
{
    unsigned int index = list->count;

    while (index > 0 &&
        list->jobs[index - 1].priority > job->priority)
        index--;
    memmove(&list->jobs[index + 1], &list->jobs[index],
        (list->count - index) * sizeof(*list->jobs));
    list->jobs[index] = *job;
    list->count++;
}

/*
 * Hands the highest-priority queued jobs to the worker. Without maxMemory
 * only one request is outstanding, so at most one decoded image waits in
 * CPU memory; with maxMemory the worker may decode ahead until its unconsumed
 * results reach that limit.
 */
static int imagelist_worker_pump(AthenaImageList *list)
{
    ImageListWorker *worker = &list->worker;
    ImageListRequest *batch = NULL;
    ImageListRequest **tail = &batch;
    unsigned int limit = list->max_memory ? IMAGELIST_WORKER_MAX_SUBMITTED : 1;
    unsigned int submitted = 0;
    unsigned int i;

    if (!worker->thread)
        return 0;
    for (i = 0; i < list->count; ++i) {
        if (list->jobs[i].submitted)
            submitted++;
    }
    for (i = 0; i < list->count && submitted < limit; ++i) {
        ImageListJob *job = &list->jobs[i];
        ImageListRequest *request;
        AthenaImage *image;

        if (job->submitted || job->cached)
            continue;
        request = calloc(1, sizeof(*request));
        if (request)
            request->path = strdup(job->path);
        if (!request || !request->path) {
            free(request);
            break;
        }
        request->id = job->id;
        *tail = request;
        tail = &request->next;
        job->submitted = true;
        image = athena_image_peek(job->image_ref);
        if (image)
            image->status = ATHENA_IMAGE_STATUS_LOADING;
        submitted++;
    }
    if (!batch)
        return 0;
    if (athena_mutex_core_lock(worker->mutex) < 0) {
        ImageListRequest *request;
        for (request = batch; request; request = request->next) {
            int index = imagelist_find_id(list, request->id);
            if (index >= 0) {
                AthenaImage *image =
                    athena_image_peek(list->jobs[index].image_ref);
                list->jobs[index].submitted = false;
                if (image)
                    image->status = ATHENA_IMAGE_STATUS_QUEUED;
            }
        }
        imagelist_request_free_all(batch);
        return -1;
    }
    imagelist_request_append(&worker->requests, batch);
    athena_mutex_core_unlock(worker->mutex);
    return 0;
}

/* Returns the oldest decoded result, or NULL and whether the worker exited. */
static ImageListRequest *imagelist_worker_take(AthenaImageList *list,
    bool *exited)
{
    ImageListWorker *worker = &list->worker;
    ImageListRequest *request = NULL;

    *exited = false;
    if (athena_mutex_core_lock(worker->mutex) < 0) {
        *exited = true;
        return NULL;
    }
    if (worker->results) {
        request = worker->results;
        worker->results = request->next;
        request->next = NULL;
        worker->result_bytes -= request->buffer.bytes <=
            worker->result_bytes ? request->buffer.bytes :
            worker->result_bytes;
    } else {
        *exited = worker->exited;
    }
    athena_mutex_core_unlock(worker->mutex);
    return request;
}

/*
 * The worker stopped on its own, for example on a runtime stop request.
 * Requeue its unstarted requests and continue cooperatively.
 */
static void imagelist_worker_fallback(AthenaImageList *list)
{
    unsigned int i;

    for (i = 0; i < list->count; ++i) {
        AthenaImage *image = athena_image_peek(list->jobs[i].image_ref);
        if (list->jobs[i].cached)
            continue;
        list->jobs[i].submitted = false;
        if (image)
            image->status = ATHENA_IMAGE_STATUS_QUEUED;
    }
    imagelist_worker_stop(list);
}

static JSValue imagelist_load(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    AthenaImageList *list = imagelist_this(ctx, this_val);
    AthenaImage *image;
    JSValue image_value;
    const char *path;
    char *key;
    int existing;
    ImageListJob job;
    ImageListCacheEntry *cache;
    bool delayed = true;
    int priority = IMAGELIST_PRIORITY_NORMAL;
    int upload = IMAGELIST_UPLOAD_DRAW;
    JSValue on_load = JS_UNDEFINED;
    JSValue on_error = JS_UNDEFINED;

    if (!list || !imagelist_argc(ctx, argc, 1, 2, "ImageList.load"))
        return JS_EXCEPTION;
    if (list->closed)
        return JS_ThrowTypeError(ctx, "ImageList is closed");
    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "ImageList.load path must be a string");
    if (argc == 2 && !imagelist_options(ctx, argv[1], &delayed,
        &priority, &upload, &on_load, &on_error)) {
        JS_FreeValue(ctx, on_load);
        JS_FreeValue(ctx, on_error);
        return JS_EXCEPTION;
    }
    path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        JS_FreeValue(ctx, on_load);
        JS_FreeValue(ctx, on_error);
        return JS_EXCEPTION;
    }
    key = imagelist_normalize_path(path);
    if (!key)
        goto out_of_memory;

    existing = imagelist_find_job(list, key);
    if (existing >= 0 &&
        !athena_image_peek(list->jobs[existing].image_ref)) {
        /* The shared Image was freed while queued; start a new request. */
        imagelist_cancel_at(JS_GetRuntime(ctx), list, (unsigned int)existing);
        existing = -1;
    }
    if (existing >= 0) {
        ImageListJob *shared = &list->jobs[existing];
        ImageListCallback *callback;

        if (!imagelist_callback_reserve(shared, shared->callback_count + 1))
            goto out_of_memory;
        callback = &shared->callbacks[shared->callback_count++];
        callback->on_load = on_load;
        callback->on_error = on_error;
        image_value = JS_DupValue(ctx, shared->image_ref);
        if (upload > shared->upload)
            shared->upload = upload;
        /* A more urgent duplicate promotes a request that is still queued. */
        if (priority < shared->priority && !shared->submitted) {
            imagelist_remove_job(list, (unsigned int)existing, &job);
            job.priority = priority;
            imagelist_insert_job(list, &job);
        }
        free(key);
        JS_FreeCString(ctx, path);
        return image_value;
    }

    if (!imagelist_reserve(list, list->count + 1))
        goto out_of_memory;
    memset(&job, 0, sizeof(job));
    job.key = key;
    job.path = strdup(path);
    job.priority = priority;
    job.upload = upload;
    job.id = ++list->next_id;
    if (!job.path || !imagelist_callback_reserve(&job, 1)) {
        free(job.path);
        free(job.callbacks);
        goto out_of_memory;
    }
    cache = imagelist_cache_lookup(JS_GetRuntime(ctx), list, key);
    if (cache) {
        /* Return the cached Image; process() dispatches its callbacks. */
        job.cached = true;
        job.image_ref = JS_DupValue(ctx, cache->image_ref);
        job.callbacks[0].on_load = on_load;
        job.callbacks[0].on_error = on_error;
        job.callback_count = 1;
        imagelist_insert_job(list, &job);
        JS_FreeCString(ctx, path);
        return JS_DupValue(ctx, job.image_ref);
    }
    image = athena_image_create_empty(delayed);
    if (!image) {
        free(job.path);
        free(job.callbacks);
        goto out_of_memory;
    }
    image->loading = true;
    image->failed = false;
    image->status = ATHENA_IMAGE_STATUS_QUEUED;
    image_value = athena_image_to_value(ctx, image);
    if (JS_IsException(image_value)) {
        athena_image_destroy(image);
        free(job.path);
        free(job.callbacks);
        free(key);
        JS_FreeCString(ctx, path);
        JS_FreeValue(ctx, on_load);
        JS_FreeValue(ctx, on_error);
        return image_value;
    }
    job.image_ref = JS_DupValue(ctx, image_value);
    job.callbacks[0].on_load = on_load;
    job.callbacks[0].on_error = on_error;
    job.callback_count = 1;
    imagelist_insert_job(list, &job);
    JS_FreeCString(ctx, path);
    /* Let the worker start decoding before the next process() call. */
    imagelist_worker_pump(list);
    return image_value;

out_of_memory:
    free(key);
    JS_FreeCString(ctx, path);
    JS_FreeValue(ctx, on_load);
    JS_FreeValue(ctx, on_error);
    return JS_ThrowOutOfMemory(ctx);
}

/* Runs the completion callbacks of `job`; returns -1 if one threw. */
static int imagelist_dispatch(JSContext *ctx, ImageListJob *job,
    AthenaImage *image, int result)
{
    unsigned int i;

    for (i = 0; i < job->callback_count; ++i) {
        ImageListCallback *callback = &job->callbacks[i];
        JSValue callback_result;

        if (result == 0 && !JS_IsUndefined(callback->on_load)) {
            callback_result = JS_Call(ctx, callback->on_load, JS_UNDEFINED,
                1, (JSValueConst *)&job->image_ref);
        } else if (result < 0 && !JS_IsUndefined(callback->on_error)) {
            JSValue args[2];
            args[0] = job->image_ref;
            args[1] = athena_image_error_value(ctx, image, job->path);
            callback_result = JS_Call(ctx, callback->on_error, JS_UNDEFINED,
                2, (JSValueConst *)args);
            JS_FreeValue(ctx, args[1]);
        } else {
            continue;
        }
        if (JS_IsException(callback_result))
            return -1;
        JS_FreeValue(ctx, callback_result);
    }
    return 0;
}

static int imagelist_budget(JSContext *ctx, int argc, JSValueConst *argv,
    uint32_t *budget, uint32_t *max_bytes, clock_t *max_ticks)
{
    JSValue value;
    double max_time;

    if (argc == 0)
        return 1;
    if (!JS_IsObject(argv[0]) || JS_IsArray(ctx, argv[0]))
        return JS_ToUint32(ctx, budget, argv[0]) == 0;
    value = JS_GetPropertyStr(ctx, argv[0], "maxItems");
    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value) && JS_ToUint32(ctx, budget, value)) {
        JS_FreeValue(ctx, value);
        return 0;
    }
    JS_FreeValue(ctx, value);
    value = JS_GetPropertyStr(ctx, argv[0], "maxBytes");
    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value) && JS_ToUint32(ctx, max_bytes, value)) {
        JS_FreeValue(ctx, value);
        return 0;
    }
    JS_FreeValue(ctx, value);
    value = JS_GetPropertyStr(ctx, argv[0], "maxTime");
    if (JS_IsException(value))
        return 0;
    if (!JS_IsUndefined(value)) {
        /* Milliseconds; 0 disables the limit like maxBytes. */
        if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &max_time, value) ||
            !(max_time >= 0.0 && max_time <= 60000.0)) {
            JS_FreeValue(ctx, value);
            JS_ThrowRangeError(ctx,
                "ImageList process maxTime must be between 0 and 60000 ms");
            return 0;
        }
        *max_ticks = (clock_t)(max_time * (double)CLOCKS_PER_SEC / 1000.0);
        if (max_time > 0.0 && *max_ticks == 0)
            *max_ticks = 1;
    }
    JS_FreeValue(ctx, value);
    return 1;
}

/* Runs the upload stage the request asked for; returns -1 on failure. */
static int imagelist_upload(AthenaImageList *list, const ImageListJob *job,
    AthenaImage *image)
{
    clock_t started;
    int result;

    if (job->upload == IMAGELIST_UPLOAD_DRAW)
        return 0;
    started = clock();
    result = athena_image_upload(image, job->upload == IMAGELIST_UPLOAD_LOCK);
    list->upload_ticks += (uint64_t)(clock() - started);
    return result;
}

static int imagelist_find_cached(AthenaImageList *list)
{
    unsigned int i;

    for (i = 0; i < list->count; ++i) {
        if (list->jobs[i].cached)
            return (int)i;
    }
    return -1;
}

static JSValue imagelist_process(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    AthenaImageList *list = imagelist_this(ctx, this_val);
    JSRuntime *rt = JS_GetRuntime(ctx);
    uint32_t budget = 1;
    uint32_t max_bytes = 0;
    uint32_t processed_bytes = 0;
    unsigned int processed = 0;
    clock_t max_ticks = 0;
    clock_t started = clock();

    if (!list || !imagelist_argc(ctx, argc, 0, 1, "ImageList.process"))
        return JS_EXCEPTION;
    if (!imagelist_budget(ctx, argc, argv, &budget, &max_bytes, &max_ticks))
        return JS_EXCEPTION;
    if (budget == 0)
        return JS_NewUint32(ctx, 0);

    imagelist_worker_pump(list);
    /* Every limit admits at least one item so progress is guaranteed. */
    while (processed < budget && list->count > 0 &&
        (max_bytes == 0 || processed == 0 || processed_bytes < max_bytes) &&
        (max_ticks == 0 || processed == 0 ||
            clock() - started < max_ticks)) {
        ImageListJob job;
        AthenaImageBuffer buffer;
        AthenaImage *image;
        int index;
        int result;
        uint32_t image_bytes;
        clock_t step;

        /* Cache hits need no I/O, so they complete ahead of decoding. */
        index = imagelist_find_cached(list);
        if (index >= 0) {
            imagelist_remove_job(list, (unsigned int)index, &job);
            image = athena_image_peek(job.image_ref);
            if (!image) {
                list->cancelled++;
                imagelist_job_release(rt, &job, 0);
                continue;
            }
            list->cache_hits++;
            result = imagelist_upload(list, &job, image);
            if (result == 0)
                list->completed++;
            else
                list->failed++;
            processed++;
            if (imagelist_dispatch(ctx, &job, image, result) < 0) {
                imagelist_job_release(rt, &job, 0);
                imagelist_worker_pump(list);
                return JS_EXCEPTION;
            }
            imagelist_job_release(rt, &job, 0);
            continue;
        }

        if (list->worker.thread) {
            bool exited;
            ImageListRequest *request = imagelist_worker_take(list, &exited);

            if (!request) {
                if (!exited)
                    break;
                imagelist_worker_fallback(list);
                continue;
            }
            index = imagelist_find_id(list, request->id);
            if (index < 0) {
                /* Cancelled while it was being decoded. */
                imagelist_request_free(request);
                continue;
            }
            imagelist_remove_job(list, (unsigned int)index, &job);
            buffer = request->buffer;
            memset(&request->buffer, 0, sizeof(request->buffer));
            imagelist_request_free(request);
        } else {
            imagelist_remove_job(list, 0, &job);
            memset(&buffer, 0, sizeof(buffer));
        }

        image = athena_image_peek(job.image_ref);
        if (!image) {
            /* The Image was freed while its request was pending. */
            athena_image_buffer_release(&buffer);
            list->cancelled++;
            imagelist_job_release(rt, &job, 0);
            continue;
        }
        if (!job.submitted) {
            image->status = ATHENA_IMAGE_STATUS_LOADING;
            step = clock();
            athena_image_decode(job.path, &buffer);
            list->decode_ticks += (uint64_t)(clock() - step);
            if (buffer.bytes > list->peak_bytes)
                list->peak_bytes = buffer.bytes;
        }
        image_bytes = buffer.bytes;
        step = clock();
        result = athena_image_apply_buffer(image, job.path, &buffer);
        list->apply_ticks += (uint64_t)(clock() - step);
        if (result == 0)
            result = imagelist_upload(list, &job, image);
        if (result == 0) {
            list->completed++;
            imagelist_cache_store(ctx, list, &job);
        } else {
            list->failed++;
        }

        if (image_bytes <= UINT32_MAX - processed_bytes)
            processed_bytes += image_bytes;
        else
            processed_bytes = UINT32_MAX;
        processed++;
        if (imagelist_dispatch(ctx, &job, image, result) < 0) {
            imagelist_job_release(rt, &job, 0);
            imagelist_worker_pump(list);
            return JS_EXCEPTION;
        }
        imagelist_job_release(rt, &job, 0);
    }
    imagelist_worker_pump(list);
    return JS_NewUint32(ctx, processed);
}

static JSValue imagelist_pending(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    AthenaImageList *list = imagelist_this(ctx, this_val);
    if (!list || !imagelist_argc(ctx, argc, 0, 0, "ImageList.pending"))
        return JS_EXCEPTION;
    return JS_NewUint32(ctx, list->count);
}

static JSValue imagelist_cancel(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    AthenaImageList *list = imagelist_this(ctx, this_val);
    unsigned int i;

    if (!list || !imagelist_argc(ctx, argc, 1, 1, "ImageList.cancel"))
        return JS_EXCEPTION;
    if (!JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "ImageList.cancel expects an Image");
    for (i = 0; i < list->count; ++i) {
        if (JS_VALUE_GET_PTR(list->jobs[i].image_ref) !=
            JS_VALUE_GET_PTR(argv[0]))
            continue;
        imagelist_cancel_at(JS_GetRuntime(ctx), list, i);
        return JS_NewBool(ctx, true);
    }
    return JS_NewBool(ctx, false);
}

static JSValue imagelist_clear(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    AthenaImageList *list = imagelist_this(ctx, this_val);

    if (!list || !imagelist_argc(ctx, argc, 0, 0, "ImageList.clear"))
        return JS_EXCEPTION;
    while (list->count > 0)
        imagelist_cancel_at(JS_GetRuntime(ctx), list, list->count - 1);
    return JS_UNDEFINED;
}

static JSValue imagelist_stats(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    AthenaImageList *list = imagelist_this(ctx, this_val);
    JSValue stats;
    unsigned int loading = 0;
    unsigned int i;
    uint32_t buffered = 0;
    uint32_t peak = 0;
    uint64_t decode_ticks = list ? list->decode_ticks : 0;

    if (!list || !imagelist_argc(ctx, argc, 0, 0, "ImageList.stats"))
        return JS_EXCEPTION;
    for (i = 0; i < list->count; ++i) {
        if (list->jobs[i].submitted)
            loading++;
    }
    if (list->worker.thread &&
        athena_mutex_core_lock(list->worker.mutex) >= 0) {
        buffered = list->worker.result_bytes;
        peak = list->worker.peak_bytes;
        decode_ticks += list->worker.decode_ticks;
        athena_mutex_core_unlock(list->worker.mutex);
    }
    if (list->peak_bytes > peak)
        peak = list->peak_bytes;
    stats = JS_NewObject(ctx);
    if (JS_IsException(stats))
        return stats;
    JS_SetPropertyStr(ctx, stats, "queued",
        JS_NewUint32(ctx, list->count - loading));
    JS_SetPropertyStr(ctx, stats, "loading", JS_NewUint32(ctx, loading));
    JS_SetPropertyStr(ctx, stats, "completed",
        JS_NewUint32(ctx, list->completed));
    JS_SetPropertyStr(ctx, stats, "failed", JS_NewUint32(ctx, list->failed));
    JS_SetPropertyStr(ctx, stats, "cancelled",
        JS_NewUint32(ctx, list->cancelled));
    JS_SetPropertyStr(ctx, stats, "bufferedBytes",
        JS_NewUint32(ctx, buffered));
    JS_SetPropertyStr(ctx, stats, "peakBufferedBytes",
        JS_NewUint32(ctx, peak));
    JS_SetPropertyStr(ctx, stats, "workers", JS_NewUint32(ctx, list->workers));
    JS_SetPropertyStr(ctx, stats, "cached",
        JS_NewUint32(ctx, list->cache_count));
    JS_SetPropertyStr(ctx, stats, "cacheHits",
        JS_NewUint32(ctx, list->cache_hits));
    JS_SetPropertyStr(ctx, stats, "decodeTime",
        JS_NewFloat64(ctx, imagelist_ticks_to_ms(decode_ticks)));
    JS_SetPropertyStr(ctx, stats, "applyTime",
        JS_NewFloat64(ctx, imagelist_ticks_to_ms(list->apply_ticks)));
    JS_SetPropertyStr(ctx, stats, "uploadTime",
        JS_NewFloat64(ctx, imagelist_ticks_to_ms(list->upload_ticks)));
    return stats;
}

/*
 * Releases everything the list holds without waiting for the garbage
 * collector: cancels pending requests, joins the worker and frees its stack,
 * and empties the cache. Idempotent.
 */
static JSValue imagelist_close(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    AthenaImageList *list = imagelist_this(ctx, this_val);
    JSRuntime *rt = JS_GetRuntime(ctx);

    if (!list || !imagelist_argc(ctx, argc, 0, 0, "ImageList.close"))
        return JS_EXCEPTION;
    if (list->closed)
        return JS_UNDEFINED;
    /* Cancel first: discarding submitted requests needs the worker mutex. */
    while (list->count > 0)
        imagelist_cancel_at(rt, list, list->count - 1);
    imagelist_worker_stop(list);
    imagelist_cache_clear(rt, list);
    free(list->cache);
    list->cache = NULL;
    list->closed = true;
    return JS_UNDEFINED;
}

static JSValue imagelist_clear_cache(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    AthenaImageList *list = imagelist_this(ctx, this_val);

    if (!list || !imagelist_argc(ctx, argc, 0, 0, "ImageList.clearCache"))
        return JS_EXCEPTION;
    return JS_NewUint32(ctx, imagelist_cache_clear(JS_GetRuntime(ctx), list));
}

static JSClassDef imagelist_class = {
    "ImageList",
    .finalizer = imagelist_finalizer,
};

static const JSCFunctionListEntry imagelist_proto[] = {
    JS_CFUNC_DEF("load", 2, imagelist_load),
    JS_CFUNC_DEF("process", 1, imagelist_process),
    JS_CFUNC_DEF("pending", 0, imagelist_pending),
    JS_CFUNC_DEF("cancel", 1, imagelist_cancel),
    JS_CFUNC_DEF("stats", 0, imagelist_stats),
    JS_CFUNC_DEF("clear", 0, imagelist_clear),
    JS_CFUNC_DEF("clearCache", 0, imagelist_clear_cache),
    JS_CFUNC_DEF("close", 0, imagelist_close),
};

static const JSCFunctionListEntry imagelist_static[] = {
    JS_PROP_INT32_DEF("HIGH", IMAGELIST_PRIORITY_HIGH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("NORMAL", IMAGELIST_PRIORITY_NORMAL,
        JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("LOW", IMAGELIST_PRIORITY_LOW, JS_PROP_CONFIGURABLE),
};

static int imagelist_uint_option(JSContext *ctx, JSValueConst options,
    const char *name, uint32_t maximum, uint32_t *out)
{
    JSValue value = JS_GetPropertyStr(ctx, options, name);
    double number;

    if (JS_IsException(value))
        return 0;
    if (JS_IsUndefined(value))
        return 1;
    if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &number, value) ||
        number < 0 || number > maximum || number != (double)(uint32_t)number) {
        JS_FreeValue(ctx, value);
        JS_ThrowRangeError(ctx,
            "ImageList options.%s must be an integer between 0 and %lu",
            name, (unsigned long)maximum);
        return 0;
    }
    JS_FreeValue(ctx, value);
    *out = (uint32_t)number;
    return 1;
}

static JSValue imagelist_ctor(JSContext *ctx, JSValueConst new_target,
    int argc, JSValueConst *argv)
{
    AthenaImageList *list;
    JSValue proto, object;

    if (!imagelist_argc(ctx, argc, 0, 1, "ImageList"))
        return JS_EXCEPTION;
    list = calloc(1, sizeof(*list));
    if (!list)
        return JS_ThrowOutOfMemory(ctx);
    if (argc == 1 && !JS_IsUndefined(argv[0])) {
        if (!JS_IsObject(argv[0]) || JS_IsArray(ctx, argv[0])) {
            free(list);
            return JS_ThrowTypeError(ctx,
                "ImageList options must be an object");
        }
        if (!imagelist_uint_option(ctx, argv[0], "workers", 1,
                &list->workers) ||
            !imagelist_uint_option(ctx, argv[0], "maxMemory", UINT32_MAX,
                &list->max_memory) ||
            !imagelist_uint_option(ctx, argv[0], "cacheSize",
                IMAGELIST_MAX_CACHE_SIZE, &list->cache_size)) {
            free(list);
            return JS_EXCEPTION;
        }
    }
    proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto)) {
        free(list);
        return proto;
    }
    object = JS_NewObjectProtoClass(ctx, proto, imagelist_class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(object)) {
        free(list);
        return object;
    }
    if (imagelist_worker_start(list) < 0) {
        JS_FreeValue(ctx, object);
        free(list);
        return JS_ThrowInternalError(ctx,
            "failed to start ImageList worker");
    }
    JS_SetOpaque(object, list);
    return object;
}

static int imagelist_module_init(JSContext *ctx, JSModuleDef *module)
{
    JSValue proto, constructor;

    JS_NewClassID(&imagelist_class_id);
    JS_NewClass(JS_GetRuntime(ctx), imagelist_class_id, &imagelist_class);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, imagelist_proto,
        countof(imagelist_proto));
    JS_SetClassProto(ctx, imagelist_class_id, proto);
    constructor = JS_NewCFunction2(ctx, imagelist_ctor, "ImageList", 0,
        JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, constructor, proto);
    JS_SetPropertyFunctionList(ctx, constructor, imagelist_static,
        countof(imagelist_static));
    JS_SetModuleExport(ctx, module, "ImageList", constructor);
    return 0;
}

JSModuleDef *athena_imagelist_init(JSContext *ctx)
{
    JSModuleDef *module = athena_push_module(ctx, imagelist_module_init, NULL,
        0, "ImageList");
    if (!module)
        return NULL;
    JS_AddModuleExport(ctx, module, "ImageList");
    return module;
}
