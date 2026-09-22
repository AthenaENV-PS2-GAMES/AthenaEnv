/**
 * Cooperative asynchronous image loading.
 *
 * ImageList keeps file decoding and graphics-resource preparation on the
 * calling thread. It never executes QuickJS callbacks from a worker thread.
 * Call `process()` from the frame loop with a small budget to bound the work
 * performed in one frame.
 *
 * @example
 * ```js
 * import * as ImageListModule from "ImageList";
 * import * as Screen from "Screen";
 *
 * const images = new ImageListModule.ImageList();
 * const logo = images.load("tests/my_image.png", {
 *     onLoad: (image) => image.lock(),
 *     onError: (image, path) => console.log(`Failed: ${path}`),
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
declare module "ImageList" {
    /**
     * Options for one queued image request.
     *
     * Callbacks run synchronously inside `process()` on the same thread that
     * called it. The returned `Image` remains valid after the callback and is
     * owned by the caller.
     */
    interface ImageListOptions {
        /** Whether texture uploads use the deferred VIF1 path; defaults to true. */
        delayed?: boolean;
        /** Called after the image has been decoded successfully. */
        onLoad?: (image: Image) => void;
        /** Called after loading fails; the image remains in the failed state. */
        onError?: (image: Image, path: string) => void;
    }

    class ImageList {
        /** Creates an empty request queue. */
        constructor();

        /**
         * Queues an image and returns it immediately in the loading state.
         *
         * The request is not processed until `process()` is called. The
         * returned image can be inspected with `loading()`, `ready()` and
         * `failed()` while it is queued.
         */
        load(path: string, options?: ImageListOptions): Image;
        /**
         * Processes up to `budget` queued images and dispatches callbacks.
         *
         * A zero budget performs no work. The default budget is one image.
         * Returns the number of requests completed, including failures.
         */
        process(budget?: number): number;
        /** Returns the number of queued requests not yet processed. */
        pending(): number;
        /**
         * Cancels all queued requests.
         *
         * Already returned `Image` objects are not destroyed. Their pending
         * callbacks are discarded and their `failed()` state becomes true.
         */
        clear(): void;
    }
}
