declare namespace Timer {
    /** Creates a running timer and returns its opaque handle. */
    function new(): number;

    /** Returns elapsed clock ticks, or the frozen value while paused. */
    function getTime(timer: number): number;

    /** Sets the elapsed time in clock ticks. */
    function setTime(timer: number, value: number): void;

    /** Pauses the timer without resetting its elapsed time. */
    function pause(timer: number): void;

    /** Resumes a paused timer. */
    function resume(timer: number): void;

    /** Resets elapsed time to zero. */
    function reset(timer: number): void;

    /** Returns whether the timer is currently running. */
    function isPlaying(timer: number): boolean;

    /** Releases the timer handle. */
    function destroy(timer: number): void;
}
