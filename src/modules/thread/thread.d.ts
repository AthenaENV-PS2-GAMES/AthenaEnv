/**
 * EE thread management.
 *
 * Thread callbacks execute on native EE worker threads. Keep callbacks short,
 * avoid direct QuickJS runtime access from native workers, and use the
 * documented AthenaEnv synchronization boundaries.
 *
 * Example:
 * ```js
 * const worker = Thread.new(() => {
 *     System.delay();
 * }, 'worker', 32768, 16);
 * Thread.start(worker);
 * console.log(Thread.getStatus(worker));
 * Thread.destroy(worker);
 * ```
 */
declare namespace Thread {
    /** Opaque handle returned by `Thread.new()`. */
    interface Handle {
        readonly __brand: 'Thread';
    }

    /** Snapshot of one tracked native thread. */
    interface TaskInfo {
        /** Native EE thread ID. */
        id: number;
        /** Thread name. */
        name: string;
        /** Native status code. */
        status: number;
        /** Configured stack size in bytes. */
        stack: number;
    }

    /**
     * Creates a native EE thread.
     * @param callback Function executed by the new thread.
     * @param name Optional name, limited to 63 characters.
     * @param stackSize Stack size in bytes, at least 16384; defaults to 32768.
     *   JavaScript may use it all but 8 KB: deeper recursion throws a
     *   catchable "stack overflow" instead of overrunning the stack.
     * @param priority EE priority from 1 to 127; defaults to 16.
     */
    function new(
        callback: () => void,
        name?: string,
        stackSize?: number,
        priority?: number
    ): Handle;

    /** Starts execution and returns the native EE result code. */
    function start(thread: Handle): number;

    /** Requests thread termination and returns the native EE result code. */
    function stop(thread: Handle): number;

    /** Returns the native EE thread ID. */
    function getId(thread: Handle): number;

    /** Returns the current thread name. */
    function getName(thread: Handle): string;

    /** Replaces the thread name. */
    function setName(thread: Handle, name: string): void;

    /** Returns the native EE status code. */
    function getStatus(thread: Handle): number;

    /** Releases the thread handle. Stop it first; do not reuse the object. */
    function destroy(thread: Handle): void;

    /** Returns all active/tracked threads. */
    function list(): TaskInfo[];

    /** Force-terminates a native thread by ID. */
    function kill(id: number): number;
}

/**
 * A background job, as returned by `MemoryCard.readFileAsync()`,
 * `Archive.extractAsync()`, `Sound.loadSfxAsync()`, `Font.loadAsync()`...
 * Jobs run on a small shared pool of worker threads while frames keep
 * coming. Every job can be awaited, polled, waited for and cancelled; the
 * module functions (`MemoryCard.poll(job)`, ...) do the same.
 *
 * @example
 * ```js
 * const font = await Font.loadAsync("fonts/title.ttf", { size: 40 });
 *
 * const job = Archive.extractAsync("dlc.zip", "mass:/GAME/dlc");
 * Loop.run(() => {
 *     const status = job.poll();          // { state, result | error, ...progress }
 *     if (status.state === "running") drawProgress(status.bytesDone, status.bytesTotal);
 * });
 * ```
 */
interface AthenaJob<T, Status extends AthenaJobStatus<T> = AthenaJobStatus<T>> extends PromiseLike<T> {
    /** State without blocking; the result or error is converted once and kept. */
    poll(): Status;
    /** Blocks until the job settles or `timeoutMs` passes, then polls. */
    wait(timeoutMs?: number): Status;
    /** The job ends as `'cancelled'` unless it already finished. */
    cancel(): void;
}

interface AthenaJobStatus<T> {
    state: 'running' | 'done' | 'failed' | 'cancelled';
    /** When `state` is `'done'`. The same value on every later poll. */
    result?: T;
    /** When `state` is `'failed'` or `'cancelled'`. */
    error?: Error;
}
