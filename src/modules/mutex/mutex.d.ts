declare namespace Mutex {
    /** Creates an unlocked mutex object. */
    function new(): object;

    /** Blocks until the mutex is acquired and returns the EE result code. */
    function lock(mutex: object): number;

    /** Releases the mutex and returns the EE result code. */
    function unlock(mutex: object): number;

    /** Releases the native mutex immediately. */
    function destroy(mutex: object): void;
}
