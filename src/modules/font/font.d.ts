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
 * A glyph is rasterized the first time it is printed, which can hold that
 * frame: `preload()` (or the `preload` option of `loadAsync`) does it ahead,
 * a few milliseconds per frame. Text printed every frame is cheaper through
 * `render()`, which lays it out once.
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
     * Loads a font without stalling the frame loop. A TrueType file is read
     * on the shared job pool; a bitmap font's image and `.dat` widths are
     * decoded there, and its texture is uploaded when first drawn. The Font
     * is created on the script thread when the job is awaited or polled.
     *
     * With `preload`, the job resolves only once those glyphs are rasterized,
     * `budgetMs` per frame, so a loading screen hands over a font that prints
     * without a stall.
     *
     * @example
     * ```js
     * async function start() {
     *     const title = await Font.loadAsync("fonts/title.ttf", { size: 48, preload: true });
     *     Loop.run(() => title.print(40, 40, "Ready"));
     * }
     * start();
     * ```
     */
    static loadAsync(path?: string | null, options?: Font.AsyncOptions): Font.Job;
    /** Same as `job.poll()`, `job.wait()` and `job.cancel()`. `wait()` also finishes the preloading. */
    static poll(job: Font.Job): AthenaJobStatus<Font>;
    static wait(job: Font.Job, timeoutMs?: number): AthenaJobStatus<Font>;
    static cancel(job: Font.Job): void;

    /** The printable ASCII characters, from space to `~`: what `preload()` rasterizes by default. */
    static readonly ASCII: string;

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
    /**
     * Keeps `text` ready to print repeatedly: its glyphs are placed once and
     * placed again only when `scale`, `align` or the video mode change. The
     * outline or shadow reuse the same placement.
     */
    render(text: string): FontRender;
    /**
     * Rasterizes the glyphs of `chars` (by default `Font.ASCII`) ahead of the
     * first print, spending at most `budgetMs` (default 2) per frame; `0`
     * rasterizes them all now. The first slice runs during the call, the
     * next ones once per frame, also before `Loop.run()` starts. Resolves
     * with the font; rejects if it is freed meanwhile. Bitmap fonts resolve
     * at once.
     *
     * @example
     * ```js
     * await hud.preload("0123456789:/ ", { budgetMs: 1 });
     * ```
     */
    preload(chars?: string, options?: Font.PreloadOptions): Promise<Font>;
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
        /** TrueType rasterization size in pixels, 6 to 128; defaults to 26. Ignored by bitmap fonts. */
        size?: number;
    }

    interface PreloadOptions {
        /** Milliseconds of rasterization per frame at most; `0` does it all at once. Defaults to 2. */
        budgetMs?: number;
    }

    interface AsyncOptions extends Options, PreloadOptions {
        /** Glyphs rasterized before the job resolves: a string of characters, or `true` for `Font.ASCII`. */
        preload?: string | boolean;
    }
}

declare class FontRender {
    print(x: number, y: number): void;
}
