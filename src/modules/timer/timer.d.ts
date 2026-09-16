/**
 * Manual native timer objects.
 *
 * Timer values are represented in the module's native clock-tick units.
 * These timers are distinct from the global event-loop functions such as
 * `setTimeout`.
 *
 * Example:
 * ```js
 * const timer = Timer.new();
 * Timer.pause(timer);
 * Timer.setTime(timer, 0);
 * Timer.resume(timer);
 * console.log(Timer.isPlaying(timer));
 * Timer.destroy(timer);
 * ```
 */
declare namespace Timer {
    /** Opaque handle returned by `Timer.new()`. */
    interface Handle {
        readonly __brand: 'Timer';
    }

    /** Creates a running timer. */
    function new(): Handle;

    /** Returns elapsed native clock ticks, frozen while paused. */
    function getTime(timer: Handle): number;

    /** Replaces the elapsed time in native clock ticks. */
    function setTime(timer: Handle, value: number): void;

    /** Pauses without resetting the elapsed time. */
    function pause(timer: Handle): void;

    /** Resumes a paused timer. */
    function resume(timer: Handle): void;

    /** Sets elapsed time to zero while preserving the timer object. */
    function reset(timer: Handle): void;

    /** Returns true when the timer is actively advancing. */
    function isPlaying(timer: Handle): boolean;

    /** Releases the native timer. Do not use `timer` afterwards. */
    function destroy(timer: Handle): void;
}
