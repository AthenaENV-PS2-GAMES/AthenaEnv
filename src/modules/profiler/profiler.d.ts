/**
 * Where the frame time goes, measured in C.
 *
 * Timer scopes read the EE cycle counter (COP0 Count) in C, so a begin/end
 * pair costs far less than two System.getMilliseconds() calls; counters sum
 * values per frame. Each frame closes into a ring of the last `HISTORY`
 * frames, from which `stats()` gives the last value, average, 95th
 * percentile and peak. Timers are inclusive: nested scopes count in their
 * parents too. A single begin/end span must be shorter than about 14 s (the
 * counter wraps).
 *
 * Not in the default build: `node tools/modules.js configure --modules=profiler,...`
 * (brings Loop and Debug).
 *
 * Example:
 * ```js
 * import * as Render3D from "Render3D";
 * Profiler.auto();                       // close frames after every Loop draw
 * Profiler.attachRender3D(Render3D);     // 3d.* counters + C clipper warning
 * Profiler.overlay(true);                // one line per scope in Debug
 *
 * const AI = Profiler.scope("ai");       // ids skip the name lookup
 * Profiler.begin(AI); updateAI(); Profiler.end(AI);
 * Profiler.measure("physics", () => world.step(dt));
 * Profiler.count("chunks.rebuilt", rebuilt);
 * ```
 */
declare namespace Profiler {
    /** Frames kept per scope (120). */
    const HISTORY: number;
    const MAX_SCOPES: number;
    /** Open scopes at once. */
    const MAX_DEPTH: number;
    /** A scope id from scope()/counter(), or its name (1 to 31 printable characters). */
    type Scope = number | string;

    interface Stats {
        name: string;
        kind: "timer" | "counter";
        /** Frames in the window. */
        samples: number;
        /** Per frame: milliseconds for timers, summed values for counters. */
        last: number;
        average: number;
        p95: number;
        peak: number;
        /** begin() (or count()) calls in the last frame and on average. */
        lastCalls: number;
        averageCalls: number;
    }

    /** Id of a timer scope, registered on first use. Scope 0 is "frame". */
    function scope(name: string): number;
    /** Id of a counter, registered on first use. */
    function counter(name: string): number;
    /** Opens a timer scope; a name is registered on first use. */
    function begin(scope: Scope): void;
    /** Closes the innermost scope; when given, it must be that scope (throws otherwise). */
    function end(scope?: Scope): void;
    /** Runs fn inside a timer scope and returns its result; closes on throw too. */
    function measure<R>(scope: Scope, fn: () => R): R;
    /** Adds value (default 1) to a counter for this frame. */
    function count(scope: Scope, value?: number): void;
    /**
     * Closes the frame and returns its length in ms. Not needed with auto().
     * Open scopes are split: their time so far goes to this frame.
     */
    function frame(): number;
    /** Closes the frames after every draw of Loop.run(). Returns whether on. */
    function auto(on?: boolean): boolean;
    /** Stats over the last `frames` frames (default the whole history). Optional `out` is reused. */
    function stats<T extends object = Stats>(scope: Scope, frames?: number, out?: T): T & Stats;
    /** Registered scope names, by id. */
    function names(): string[];
    /** Unmatched end() calls and begin() beyond MAX_DEPTH since the reset. */
    function errors(): number;
    /**
     * Records Render3D.frameStats() at each frame() as the counters
     * 3d.triangles, 3d.objects, 3d.passes, 3d.culled and 3d.cpuClip, and
     * logs (at most every 5 s, unless `warn: false`) when objects are
     * clipped in C on the EE. Pass the Render3D namespace, or null to stop.
     */
    function attachRender3D(render3d: typeof Render3D | null, options?: { warn?: boolean }): void;
    /** Shows a line per scope in the Debug overlay (turning it on), over `frames` frames (default 60). */
    function overlay(on?: boolean, options?: { frames?: number }): boolean;
    /** The overlay line of a scope, e.g. "1.23 ms p95 2.10 max 3.40". */
    function describe(scope: Scope, frames?: number): string;
    /** Stats of every scope (allocates; for logs and tests). */
    function report(frames?: number): Stats[];
    /** Prints report() with console.log. */
    function log(frames?: number): void;
    /** Clears the history; forget = true also drops the names (ids become invalid). */
    function reset(forget?: boolean): void;
}
