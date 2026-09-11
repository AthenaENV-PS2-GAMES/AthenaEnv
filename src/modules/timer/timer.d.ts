declare namespace Timer {
    /** Creates a running timer object. */
    function new(): object;

    /** Returns elapsed clock ticks, or the frozen value while paused. */
    function getTime(timer: object): number;

    /** Sets the elapsed time in clock ticks. */
    function setTime(timer: object, value: number): void;

    /** Pauses the timer without resetting its elapsed time. */
    function pause(timer: object): void;

    /** Resumes a paused timer. */
    function resume(timer: object): void;

    /** Resets elapsed time to zero. */
    function reset(timer: object): void;

    /** Returns whether the timer is currently running. */
    function isPlaying(timer: object): boolean;

    /** Releases the native timer immediately; the object must not be reused. */
    function destroy(timer: object): void;
}
