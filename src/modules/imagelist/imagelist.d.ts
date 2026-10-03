/**
 * Cooperative asynchronous image loading.
 *
 * ImageList applies decoded images to surfaces and VRAM, and runs callbacks,
 * only on the thread that calls `process()`. By default decoding also happens
 * there; `new ImageList({ workers: 1 })` moves file I/O and decoding to one
 * CPU worker thread. Call `process()` from the frame loop with a small budget
 * to bound the work performed in one frame.
 *
 * @example
 * ```js
 * const images = new ImageList();
 * const logo = images.load("tests/my_image.png", {
 *     onLoad: (image) => image.lock(),
 *     onError: (image, error) => console.log(`Failed: ${error.path}`),
 * });
 *
 * while (true) {
 *     images.process(1);
 *     Screen.clear(0x80182030);
 *     if (logo.ready()) logo.draw(100, 80);
 *     Screen.flip();
 * }
 * ```
 */

/**
 * Options for one queued image request.
 *
 * Callbacks run synchronously inside `process()` on the same thread that
 * called it. The returned `Image` remains valid after the callback and is
 * owned by the caller.
 */
interface ImageListLoadOptions {
    /** Whether texture uploads use the deferred VIF1 path; defaults to true. */
    delayed?: boolean;
    /**
     * Queue priority; defaults to `ImageList.NORMAL`. Requests with the same
     * priority keep their submission order.
     */
    priority?: number;
    /**
     * When the texture becomes resident in VRAM:
     * - `"draw"` (default): on the first draw, as with any `Image`.
     * - `"bind"`: inside `process()`, before `onLoad`. The texture manager may
     *   still evict it later.
     * - `"lock"`: inside `process()`, and locked until `unlock()`.
     *
     * With `"bind"` or `"lock"`, a texture that does not fit in VRAM fails
     * with `stage: "upload"`; its pixels are released and `onError` runs. The
     * status is `"ready"` when `onLoad` runs. Duplicate requests use the
     * strongest mode asked for.
     */
    upload?: "draw" | "bind" | "lock";
    /** Called after the image has been decoded successfully. */
    onLoad?: (image: Image) => void;
    /** Called after loading fails with structured diagnostics. */
    onError?: (image: Image, error: ImageLoadError) => void;
}

/** Options for an `ImageList` queue. */
interface ImageListOptions {
    /**
     * `1` decodes files on a single CPU worker thread; `0` (the default)
     * decodes inside `process()`. In both modes surfaces, VRAM and callbacks
     * are handled only on the thread that calls `process()`.
     */
    workers?: 0 | 1;
    /**
     * Worker mode only: decoded bytes the worker may keep ready ahead of
     * `process()`. `0` (the default) keeps at most one decoded image waiting.
     * The peak is bounded by this limit plus one decoded image.
     */
    maxMemory?: number;
    /**
     * Number of successfully loaded images kept for reuse, least recently
     * used first out; `0` (the default) disables the cache. Loading a cached
     * path returns the same `Image` without decoding it again, and its
     * callbacks still run inside `process()`. The cache holds a reference to
     * each `Image` but never locks it, so VRAM residency is unaffected.
     * Entries leave on eviction, `clearCache()`, `Image.free()` or when the
     * list is destroyed. At most 1024.
     */
    cacheSize?: number;
}

interface ImageListProcessOptions {
    /** Maximum number of requests to complete; defaults to one. */
    maxItems?: number;
    /**
     * Stops after the decoded bytes completed in this call reach this value.
     * The first request always completes; zero means no byte limit.
     */
    maxBytes?: number;
    /**
     * Stops starting new requests once this many milliseconds have elapsed
     * in this call. The first request always completes, so one large image
     * can exceed the limit; zero means no time limit.
     */
    maxTime?: number;
}

interface ImageListStats {
    /** Requests waiting to be decoded. */
    queued: number;
    /** Requests being decoded by the worker or waiting to be applied. */
    loading: number;
    completed: number;
    failed: number;
    cancelled: number;
    /** Decoded bytes currently held by the worker and not yet applied. */
    bufferedBytes: number;
    /** Largest decoded-but-unapplied CPU memory observed. */
    peakBufferedBytes: number;
    /** Worker threads currently running: 0 or 1. */
    workers: number;
    /** Images currently held by the cache. */
    cached: number;
    /** Requests completed from the cache; also counted in `completed`. */
    cacheHits: number;
    /** Total milliseconds spent decoding, on the worker or in `process()`. */
    decodeTime: number;
    /** Total milliseconds spent building surfaces from decoded buffers. */
    applyTime: number;
    /** Total milliseconds spent on `upload: "bind"` / `"lock"` VRAM uploads. */
    uploadTime: number;
}

declare class ImageList {
    static readonly HIGH: number;
    static readonly NORMAL: number;
    static readonly LOW: number;

    /** Creates an empty request queue. */
    constructor(options?: ImageListOptions);

    /**
     * Queues an image and returns it immediately in the queued state.
     *
     * Requests for the same normalized path that are still pending share one
     * decode and return the same `Image`; every caller's callbacks run. A more
     * urgent duplicate promotes a request that has not started loading. The
     * returned image can be inspected with `status()`, `loading()`, `ready()`
     * and `failed()` while it is pending.
     */
    load(path: string, options?: ImageListLoadOptions): Image;
    /**
     * Completes up to `budget` requests and dispatches their callbacks.
     *
     * A zero budget performs no work. The default budget is one image.
     * Returns the number of requests completed, including failures. In worker
     * mode only requests the worker has already decoded are completed, so
     * this can return zero while requests are still loading. Cache hits
     * complete before decoded requests and count toward the budget.
     */
    process(budget?: number | ImageListProcessOptions): number;
    /** Returns the number of requests not completed yet (queued or loading). */
    pending(): number;
    /** Returns queue, completion and memory counters. */
    stats(): ImageListStats;
    /**
     * Cancels one pending request. Returns true when a request was
     * cancelled, or false when it was already completed or not owned by
     * this list. A request the worker is decoding is discarded when the
     * decode finishes; its callbacks never run.
     */
    cancel(image: Image): boolean;
    /**
     * Cancels all pending requests.
     *
     * Already returned `Image` objects are not destroyed. Their pending
     * callbacks are discarded and their `status()` becomes "cancelled".
     * A request served from the cache only loses its callbacks; its image
     * keeps its current status.
     */
    clear(): void;
    /** Releases every cached image reference and returns how many were held. */
    clearCache(): number;
    /**
     * Releases the list's resources now instead of waiting for the garbage
     * collector. Pending requests are cancelled as by `clear()`. The worker
     * thread is joined and its stack freed, and the cache is emptied.
     * Afterwards `load()` throws a TypeError. `process()` returns 0, while
     * `stats()` keeps working. Calling it again does nothing.
     */
    close(): void;
}
