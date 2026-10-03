/**
 * Native EE mutex primitives.
 *
 * Mutexes protect native/application data shared by callbacks or threads.
 * They do not make arbitrary QuickJS runtime access thread-safe; JavaScript
 * execution must still follow AthenaEnv's runtime-gate rules.
 *
 * Example:
 * ```js
 * const lock = Mutex.new();
 * Mutex.lock(lock);
 * try {
 *     // Update shared native/application state.
 * } finally {
 *     Mutex.unlock(lock);
 *     Mutex.destroy(lock);
 * }
 * ```
 */
declare namespace Mutex {
    /** Opaque handle returned by `Mutex.new()`. */
    interface Handle {
        readonly __brand: 'Mutex';
    }

    /** Creates an unlocked native mutex. */
    function new(): Handle;

    /** Blocks until acquired and returns the native EE result code. */
    function lock(mutex: Handle): number;

    /** Releases the mutex and returns the native EE result code. */
    function unlock(mutex: Handle): number;

    /** Releases the native mutex. Do not use `mutex` afterwards. */
    function destroy(mutex: Handle): void;
}
