declare namespace Thread {
    interface TaskInfo {
        id: number;
        name: string;
        status: number;
        stack: number;
    }

    /**
     * Creates a new thread.
     * @param callback Function to execute in the thread.
     * @param name Optional thread name (maximum 63 characters).
     * @param stackSize Optional thread stack size in bytes (default 16384).
     * @param priority Optional thread priority from 1 to 127 (default 16).
     */
    function new(
        callback: () => void,
        name?: string,
        stackSize?: number,
        priority?: number
    ): object;

    /** Starts thread execution and returns the EE result code. */
    function start(thread: object): number;

    /** Stops / terminates thread execution and returns the EE result code. */
    function stop(thread: object): number;

    /** Returns the native EE thread ID. */
    function getId(thread: object): number;

    /** Returns the thread name. */
    function getName(thread: object): string;

    /** Sets the thread name. */
    function setName(thread: object, name: string): void;

    /** Returns the current thread status code. */
    function getStatus(thread: object): number;

    /** Releases the native thread immediately; the object must not be reused. */
    function destroy(thread: object): void;

    /** Lists all active and tracked threads in the system. */
    function list(): TaskInfo[];

    /** Force terminates a thread by its native ID. */
    function kill(id: number): number;
}
