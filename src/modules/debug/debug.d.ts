/**
 * On-screen diagnostics, for the console where there is no terminal.
 *
 * Everything is drawn after the game's draw by a Loop system that exists only
 * while something is on. Games with their own loop call `Debug.frame(dt)`
 * after drawing. The overlay text refreshes 4 times per second and is laid
 * out once per refresh; the frame-time graph, the console tail and the
 * rects, lines and circles are computed and drawn in C, so a hitbox per
 * entity per frame allocates nothing. The overlay shows its own cost
 * ("debug x ms").
 *
 * It never takes the game down: arguments are checked at the call (a bad
 * color throws there), and an error while drawing turns the module off and
 * is logged once. Shapes are capped at 2048 (texts too); the oldest go and
 * the overlay counts them.
 *
 * The panels stay inside the title-safe area (5% of each edge), which CRT
 * TVs do not cut, and use the built-in font at 16 px.
 *
 * Not in the default build: `node tools/modules.js configure --modules=debug,...`
 *
 * Example:
 * ```js
 * Debug.overlay(true);                          // FPS, CPU, RAM, JS heap, VRAM, graph
 * Debug.console(true, { lines: 6 });            // last lines of console.log
 * Debug.watch("player", () => `${player.x | 0},${player.y | 0} ${player.state}`);
 * Debug.toggleWith(Gamepad.L3 | Gamepad.R3);    // show / hide everything
 *
 * // In update(): hitboxes for a second, in world coordinates.
 * Debug.rect(enemy.x, enemy.y, 16, 16, Color.new(255, 0, 0), { seconds: 1, space: "world" });
 * Debug.text(enemy.x, enemy.y - 10, "hit!", { seconds: 0.5, space: "world" });
 * ```
 */
declare namespace Debug {
    interface ShapeOptions {
        /** How long it stays, in real seconds (default 0: this frame only). */
        seconds?: number;
        /** "screen" (default) or "world", through `setView()`. */
        space?: "screen" | "world";
        /** Filled instead of an outline (rect and circle). */
        filled?: boolean;
    }

    interface TextOptions extends ShapeOptions {
        /** Text color (default white). */
        color?: Color.Value;
    }

    interface ConsoleOptions {
        /** Screen lines shown, 1 to 40 (default 8). */
        lines?: number;
    }

    interface View {
        /** World point at the top-left corner of the screen (default 0). */
        x?: number;
        y?: number;
        /** Screen pixels per world unit (default 1). */
        scale?: number;
    }

    interface Config {
        /**
         * Frame budget in milliseconds for the graph colors. 0 (default)
         * derives it from the video mode (60 Hz, or 50 Hz for PAL and 576p)
         * and `vsyncInterval`.
         */
        budgetMs?: number;
        /** The `vsyncInterval` given to Loop.run(), 1 to 4 (default 1; 2 for 30 fps). */
        vsyncInterval?: number;
        /**
         * Distance from the screen edges in pixels, one number or { x, y }.
         * null (default) is the title-safe area, 5% of each side.
         */
        margin?: number | { x: number; y: number } | null;
        /** Font of every text (default the built-in font at 16 px). */
        font?: Font;
    }

    /** Figures of the frame-time graph. */
    interface FrameStats {
        samples: number;
        frameAvg: number;
        frameMax: number;
        cpuAvg: number;
        cpuMax: number;
    }

    /**
     * Shows or hides the stats panel: FPS, CPU and frame time (average and
     * peak of the last 60 frames) and the frame budget, RAM, free VRAM, the
     * module's own cost, the watches, and a frame-time graph: green under 75% of the budget, yellow up to it, red
     * over it, magenta for a dropped frame. Returns whether it is on.
     * Measured on the PS2: about 1.1 ms per frame with the console (0.95 ms
     * compact; the graph is about 0.45 ms of it). The overlay shows its own
     * cost as "debug x ms".
     */
    function overlay(on?: boolean, options?: OverlayOptions): boolean;

    interface OverlayOptions {
        /** Draw the frame-time graph (default true). */
        graph?: boolean;
        /** Only the FPS line and the watches (default false). */
        compact?: boolean;
        /**
         * Adds the JavaScript heap size and object count (default false).
         * Reading them walks the whole heap: 6.7 ms in one frame on the PS2,
         * so it happens every 5 seconds, and is off by default because a
         * busy game would drop a frame each time.
         */
        heap?: boolean;
    }

    /**
     * Shows or hides the last lines the script printed (console.log, print,
     * errors), wrapped to the screen; lines that look like errors are red.
     * Returns whether it is on.
     */
    function console(on?: boolean, options?: ConsoleOptions): boolean;

    /**
     * Adds `name: read()` to the overlay, evaluated 4 times per second;
     * errors show inline and long values are cut at 48 characters.
     */
    function watch(name: string, read: () => unknown): void;
    /** Removes a watch; returns whether it existed. */
    function unwatch(name: string): boolean;

    /** Rectangle outline (or filled), for this frame or `seconds`. Default color red. */
    function rect(x: number, y: number, width: number, height: number,
        color?: Color.Value, options?: ShapeOptions): void;
    function line(x1: number, y1: number, x2: number, y2: number,
        color?: Color.Value, options?: ShapeOptions): void;
    function circle(x: number, y: number, radius: number,
        color?: Color.Value, options?: ShapeOptions): void;
    /**
     * Many rectangles in one call, from a Float32Array of x, y, width,
     * height groups (length a multiple of 4), checked and queued in C: for
     * the hitboxes of many entities, far cheaper than one `rect()` each.
     * Groups with a value that is not finite are skipped. Returns how many
     * were queued.
     */
    function rects(values: Float32Array, color?: Color.Value, options?: ShapeOptions): number;
    /** Many lines in one call, from x1, y1, x2, y2 groups; see `rects()`. */
    function lines(values: Float32Array, color?: Color.Value, options?: ShapeOptions): number;
    function text(x: number, y: number, text: unknown, options?: TextOptions): void;
    /** Removes every shape and text still on screen. */
    function clear(): void;

    /**
     * Shows and hides everything when the `buttons` combination is pressed
     * on the controller of `port` (0 or 1). The pad is read without
     * Gamepad.update(), so the game's justPressed() is unaffected. `null`
     * removes the shortcut.
     */
    function toggleWith(buttons: number | null, port?: 0 | 1): void;
    /** Shows or hides everything, like the shortcut; returns whether shown. */
    function show(on?: boolean): boolean;

    /** World space of shapes drawn with `space: "world"`: screen = (world - x/y) * scale. */
    function setView(view: View): void;
    function configure(options: Config): void;

    /**
     * For games that do not use Loop.run(): call after drawing, before
     * Screen.flip(). `dt` is the frame time in seconds; `cpuMs` (optional)
     * feeds the graph. Not needed with Loop.run(): there it does nothing and
     * warns once.
     */
    function frame(dt: number, cpuMs?: number): void;

    /** Figures of the graph over the last `frames` frames (default 60). */
    function frameStats(frames?: number): FrameStats;
}
