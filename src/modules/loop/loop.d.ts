/**
 * Game loop driven by the runtime.
 *
 * `Loop.run()` registers the frame handlers and returns immediately; frames
 * start once the entry script finishes. Each frame clears the screen, runs
 * `update` and `draw`, then flips. Timers, promises and async functions keep
 * running between frames, and the frame rate follows VSync.
 *
 * Variable step: `update(dt)` runs once per frame with the time since the
 * previous frame, in seconds.
 * ```js
 * let x = 0;
 * Loop.run(dt => {
 *     x += 120 * dt; // 120 pixels per second at any frame rate
 *     Draw.rect(x, 200, 32, 32, Color.new(255, 255, 255));
 * });
 * ```
 *
 * Fixed step: `update(step)` runs zero or more times per frame with the same
 * `step`, as physics engines expect, and `draw(alpha)` once per frame.
 * ```js
 * Loop.run({
 *     update(step) { world.step(step, 4); },
 *     draw(alpha) { Box2DDraw.draw(world); },
 * }, { fixedStep: 1 / 60 });
 * ```
 */
declare namespace Loop {
    /** Frame handlers. `this` inside them is the handlers object. */
    interface Handlers {
        /**
         * Advances the game. Receives the scaled frame delta in seconds, or
         * `fixedStep` when set; the first frame's delta is `0`.
         */
        update?(dt: number): void;
        /**
         * Draws the frame after the updates. With `fixedStep`, `alpha` (0..1)
         * is the fraction of a step not simulated yet, to interpolate
         * between the previous and the current state; otherwise it is `1`.
         */
        draw?(alpha: number): void;
    }

    interface Options {
        /** Clears the screen before each frame. Defaults to `true`. */
        clear?: boolean;
        /** Packed RGBA color used to clear. Defaults to opaque black. */
        clearColor?: number;
        /**
         * Longest real frame time counted, in seconds; longer stalls such as
         * loading are cut to it. `0` disables the limit. Defaults to `0.25`.
         */
        maxDelta?: number;
        /**
         * Runs `update` with this constant step, in seconds, as many times as
         * the elapsed time holds. `0`, the default, runs it once per frame
         * with the frame delta.
         */
        fixedStep?: number;
        /**
         * Fixed steps per frame at most; the time beyond it is dropped so a
         * slow frame cannot snowball. Defaults to `5`.
         */
        maxSteps?: number;
        /**
         * Vertical blanks per frame: `2` holds a steady 30 FPS on NTSC and
         * 25 FPS on PAL. Defaults to `1`.
         */
        vsyncInterval?: number;
    }

    interface Stats {
        /** Frames per second, measured over the last second. */
        fps: number;
        /** Real duration of the last frame in milliseconds, capped by `maxDelta`. */
        frameMs: number;
        /**
         * Milliseconds of work in the last frame: from the previous flip up to
         * this one, timers and promises included, without the VSync wait.
         */
        cpuMs: number;
        /** `update` calls in the last frame. */
        steps: number;
        /** Interpolation factor passed to the last `draw`. */
        alpha: number;
    }

    /**
     * Starts the loop. A function is the same as `{ update: fn }`. Called
     * again, even from a handler, it replaces the handlers and options without
     * restarting the frame timing; the rest of the current frame is skipped.
     * An exception thrown by a handler stops the program.
     */
    function run(handlers: ((dt: number) => void) | Handlers, options?: Options): void;
    /** Stops the loop after the current frame; the program ends once no timers remain. */
    function stop(): void;
    /** Returns whether the loop is running. */
    function isRunning(): boolean;
    /**
     * Scales the time passed to `update`: `0.5` is slow motion and `0`
     * pauses the game. At `0`, a variable-step `update` still runs with a
     * delta of `0`, and a fixed-step one does not run. `draw` always runs.
     */
    function setTimeScale(scale: number): void;
    /** Returns the time scale; `1` by default. */
    function getTimeScale(): number;
    /** Returns the scaled delta of the current frame, in seconds. */
    function getDeltaTime(): number;
    /** Returns the scaled time since the loop started, in seconds. */
    function getElapsedTime(): number;
    /** Returns the real time since the loop started, in seconds, ignoring the time scale. */
    function getRealElapsedTime(): number;
    /** Returns the number of frames since the loop started. */
    function getFrameCount(): number;
    /** Returns the frame statistics of the last frame. */
    function getStats(): Stats;
}
