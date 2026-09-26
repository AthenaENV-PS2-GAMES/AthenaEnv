/**
 * Font loading and text rendering.
 *
 * The constructor optionally accepts a path to either a TrueType file or a legacy
 * bitmap font (`.bmp`, `.png` or `.jpg`, optionally with a `.dat` width file).
 * With no path, the embedded Quicksand Regular font is used. Text is queued
 * into the current graphics command stream.
 *
 * TrueType glyphs are rasterized once at `size` pixels and cached; `scale`
 * stretches them, so prefer a matching `size` for large text. The same file
 * at the same size is loaded once and shared, and at most 16 different
 * TrueType fonts are loaded at a time: call `free()` on fonts no longer used.
 * Glyphs keep their proportions on NTSC, PAL, 480p and 16:9 modes, and follow
 * `Screen.setMode()`.
 *
 * @example
 * ```js
 * const title = new Font("fonts/title.ttf", { size: 48 });
 * title.outlineColor = Color.new(0, 0, 0);
 * title.outline = 2;
 * title.print(320, 40, "Game Over\nPress START");   // \n starts a new line
 * ```
 */
declare class Font {
    /** Loads `path`, or the embedded font when omitted, undefined or null. */
    constructor(path?: string | null, options?: Font.Options);
    constructor(options: Font.Options);

    /**
     * Loads a TrueType font without stalling the frame loop: the file is read
     * on the shared job pool, then the Font is created on the script thread
     * when the job is awaited or polled. Bitmap fonts load with `new Font()`.
     *
     * @example
     * ```js
     * async function start() {
     *     const title = await Font.loadAsync("fonts/title.ttf", { size: 48 });
     *     Loop.run(() => title.print(40, 40, "Ready"));
     * }
     * start();
     * ```
     */
    static loadAsync(path?: string | null, options?: Font.Options): Font.Job;
    /** Same as `job.poll()`, `job.wait()` and `job.cancel()`. */
    static poll(job: Font.Job): AthenaJobStatus<Font>;
    static wait(job: Font.Job, timeoutMs?: number): AthenaJobStatus<Font>;
    static cancel(job: Font.Job): void;

    static readonly ALIGN_TOP: number;
    static readonly ALIGN_BOTTOM: number;
    static readonly ALIGN_VCENTER: number;
    static readonly ALIGN_LEFT: number;
    static readonly ALIGN_RIGHT: number;
    static readonly ALIGN_HCENTER: number;
    static readonly ALIGN_NONE: number;
    static readonly ALIGN_CENTER: number;

    scale: number;
    color: Color.Value;
    /** Horizontal alignment applies to each line. */
    align: number;
    outline: number;
    outlineColor: Color.Value;
    dropshadow: number;
    dropshadowColor: Color.Value;
    /** @deprecated Use `outlineColor`. */
    outline_color: Color.Value;
    /** @deprecated Use `dropshadowColor`. */
    dropshadow_color: Color.Value;
    /** TrueType rasterization size in pixels (0 for bitmap fonts). */
    readonly size: number;
    /** Distance between two lines at the current `scale`, in pixels. */
    readonly lineHeight: number;

    /** Queues `text`; `\n` starts a new line. */
    print(x: number, y: number, text: string): void;
    /** Width of the widest line and height of all lines, in pixels. */
    getTextSize(text: string): { width: number; height: number };
    /** Keeps `text` ready to print repeatedly. */
    render(text: string): FontRender;
    /**
     * Releases the font now instead of when the collector finds the object.
     * Using it afterwards throws; FontRender objects made from it throw too.
     */
    free(): void;
}

declare namespace Font {
    /** A `Font.loadAsync()` job. */
    interface Job extends AthenaJob<Font> {
        readonly __brand: 'FontJob';
    }

    interface Options {
        /** TrueType rasterization size in pixels, 6 to 128; defaults to 26. */
        size?: number;
    }
}

declare class FontRender {
    print(x: number, y: number): void;
}
