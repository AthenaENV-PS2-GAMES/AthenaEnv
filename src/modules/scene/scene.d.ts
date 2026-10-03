/**
 * Scenes and their assets.
 *
 * A scene is a screen of the game (title, level, pause menu): a class that
 * extends `Scene`, names the assets it needs in `static assets`, and gets
 * `enter`, `update`, `draw`, `pause`, `resume` and `exit` calls. The static
 * methods of `Scene` manage a stack of them:
 *
 * - `Scene.go(Level)` loads the level's assets in the background (while the
 *   current scene fades out, with `transition: "fade"`), exits the current
 *   scenes, releases what only they used, and enters the level;
 * - `Scene.push(Pause)` puts a scene over the current one, which pauses and
 *   keeps being drawn below; `Scene.pop({ result })` resumes it;
 * - a loading screen shows only when loading outlasts the transition.
 *
 * Assets are reference counted by kind and path: an asset two scenes use is
 * loaded once and survives going from one to the other (the next scene's
 * assets are acquired before the previous scene's are released). Images load
 * through an ImageList (decoded on a worker), sound effects, fonts and sprite
 * sheets through their background jobs, so frames keep coming while loading.
 *
 * Transitions and loading run on real time: a pause menu that sets
 * `Loop.setTimeScale(0)` can still go to another scene.
 *
 * @example
 * ```js
 * class Level1 extends Scene {
 *     static root = "assets/level1";
 *     static assets = {
 *         images: { tiles: { path: "tiles.png", upload: "lock" } },
 *         sheets: { hero: "hero.json" },                 // Aseprite / TexturePacker
 *         sfx:    { jump: "jump.adp" },
 *         music:  { theme: { path: "level1.ogg", loop: true } },
 *         fonts:  { hud: { path: "hud.ttf", size: 20, preload: true } },
 *         data:   { map: "map.json" },
 *     };
 *     enter(assets) {
 *         this.hero = new Sprite.Instance(assets.sheets.hero, { clip: "idle" });
 *         assets.music.theme.play();
 *     }
 *     update(dt) {
 *         if (pad.justPressed(Gamepad.START)) Scene.push(PauseMenu);
 *     }
 *     draw() { this.hero.draw(this.x, this.y); }
 *     exit() { this.assets.music.theme.stop(); }
 * }
 *
 * Scene.run(Title);                                        // starts the Loop
 * // later, from a scene:
 * Scene.go(Level1, { transition: "fade", duration: 0.5 });
 * ```
 */
/**
 * `A` types the loaded assets and `P` the params, for editors:
 * `class Level extends Scene<{ images: { tiles: Image } }, { number: number }>`.
 */
declare class Scene<A = any, P = any> {
    /** What `go()`/`push()` passed as `options.params`. */
    readonly params: P;
    /** The loaded assets, as the manifest names them; set before `enter()`, null after `exit()`. */
    assets: A;

    constructor(params?: P);

    /** The assets are loaded; the scene starts. May return a promise (the loading screen stays). */
    enter(assets: A, params: P): void | Promise<void>;
    update(dt: number): void;
    draw(alpha: number): void;
    /** A scene was pushed over this one. */
    pause(): void;
    /** The scene above was popped, with its `pop({ result })`. */
    resume(result?: any): void;
    /** The scene leaves; its `defer()` clean-ups run and its assets are released after this. */
    exit(): void;

    /**
     * Runs `fn` when the scene leaves, after `exit()`, last registered
     * first: for what the scene started and would outlive it (tweens,
     * sprites, Loop systems, music). Returns `fn`.
     */
    defer<F extends () => void>(fn: F): F;
    /**
     * More assets while the scene runs (the next area of a level): a group,
     * released when the scene leaves. Paths are relative to the class's root
     * unless `options` gives another (a string is the root).
     */
    acquire(manifest: Scene.Manifest, options?: string | Scene.LoadOptions): Scene.AssetGroup;

    /**
     * The assets this scene needs: a manifest, or a function of the params
     * that returns one. Paths are relative to `root`.
     */
    static assets?: Scene.Manifest | ((params: any) => Scene.Manifest);
    /** Directory the manifest's paths are relative to. */
    static root?: string;

    /**
     * Replaces every scene with a new one: loads its assets (during the
     * fade-out), exits the old scenes and releases their assets, then enters
     * the new one. Resolves with the new scene once it runs. Requests made
     * meanwhile wait their turn.
     */
    static go<T extends Scene>(SceneClass: new (params?: any) => T, options?: Scene.GoOptions): Promise<T>;
    /** A scene over the current one, which pauses: a pause menu, a dialog. */
    static push<T extends Scene>(SceneClass: new (params?: any) => T, options?: Scene.PushOptions): Promise<T>;
    /** Leaves the top scene; the one below resumes with `options.result`. Resolves with it. */
    static pop(options?: Scene.PopOptions): Promise<Scene | null>;
    /**
     * Replaces only the top scene; the ones below stay (and are not resumed).
     * `drawBelow`/`updateBelow` default to the replaced scene's. With an
     * empty stack it is a `go()`.
     */
    static replace<T extends Scene>(SceneClass: new (params?: any) => T, options?: Scene.PushOptions): Promise<T>;
    /**
     * Starts loading a scene's assets now (the level while the title runs),
     * so `go()`/`push()` find them loaded. The group is released when that
     * scene class enters, by `reset()`, or with `release()`.
     */
    static preload(SceneClass: new (params?: any) => Scene, params?: any): Scene.AssetGroup;
    /**
     * Starts `Loop.run()` with the manager's update and draw, and goes to the
     * first scene. `loopOptions` go to `Loop.run()` (fixedStep, clearColor...).
     * @throws TypeError when the Loop already runs: call `Scene.update()` and
     * `Scene.draw()` from its handlers instead.
     */
    static run<T extends Scene>(SceneClass: new (params?: any) => T, options?: Scene.GoOptions,
        loopOptions?: Loop.Options): Promise<T>;
    /**
     * For a custom loop: updates the running scenes (the top one, and those
     * below it that asked for `updateBelow`). Throws the error of a scene
     * that could not load when there is no `onError`. An exception of a
     * scene's `enter()`, `exit()`, `pause()` or `resume()` during a switch
     * is thrown too (inside `Loop.run()` it stops the Loop); the switch is
     * cancelled, a scene whose `enter()` threw leaves without `exit()`, and
     * the manager goes on with the next request.
     */
    static update(dt: number): void;
    /** For a custom loop: draws the visible scenes, then the transition and loading screen. */
    static draw(alpha?: number): void;
    /** Leaves every scene at once and releases their assets and preloads. */
    static reset(): void;
    /**
     * A named transition for go/push/pop/replace: `draw(amount, info)`
     * covers `amount` (0..1) of the screen, in screen space.
     */
    static defineTransition(name: string, draw: Scene.TransitionDraw): void;

    /** The top scene, or null. */
    static readonly current: Scene | null;
    /** Every scene, bottom first. */
    static readonly stack: Scene[];
    /** A go, push or pop is in progress. */
    static readonly busy: boolean;
    /** Loading progress of the scene being loaded, 0..1. */
    static readonly progress: number;
    /**
     * Draws the loading screen in screen space while a scene loads: `null`
     * shows nothing. The default is a progress bar.
     */
    static loadingScreen: ((progress: number, info: Scene.LoadingInfo) => void) | null;
    /** Seconds of loading before the loading screen shows. Default 0.15. */
    static loadingDelay: number;
    /** Seconds a loading screen that showed stays at least. Default 0.3. */
    static minLoadingTime: number;
    /**
     * Seconds of loading after which the scene's pending assets are logged
     * once, each named: the file that hangs. Default 10; 0 never.
     */
    static slowLoadWarning: number;
    /**
     * Seconds after which a scene that has not loaded fails like a missing
     * file (`onError`, or thrown from the next update) with a
     * `Scene.TimeoutError` listing what was still loading. `timeout` in
     * the request's options overrides it. Default 0: never.
     */
    static loadTimeout: number;
    /**
     * Called when a scene cannot load (a missing file): the current scene
     * stays. The error names the request and scene (`"Scene.go(Level1):
     * cannot load image 'x.png'"`), with the original as `cause`. Without
     * it, the error is thrown from the next `update`, which stops the Loop.
     */
    static onError: ((error: Error, SceneClass: (new (params?: any) => Scene) | null) => void) | null;
}

declare namespace Scene {
    /** A path, or the path with options. */
    type Spec<T = {}> = string | ({ path: string } & T);

    interface ImageSpec {
        /** When the texture goes to VRAM: at first draw (default), now, or now and locked. */
        upload?: "draw" | "bind" | "lock";
        /** Same as `upload: "lock"`. */
        lock?: boolean;
        /** ImageList priority. */
        priority?: number;
    }

    /**
     * What a scene needs, by kind and name: `assets.images.hero` and so on.
     * Built-in kinds:
     * - `images`: `Image`, through an ImageList with a decoder thread;
     * - `sheets`: `Sprite.Sheet`, from a `.json` (Aseprite, TexturePacker)
     *   or an image with the options of `Sprite.Sheet.fromGrid()`;
     * - `sfx`: `Sound.Sfx`; `music`: `Sound.Stream` (`{ path, loop }`);
     * - `fonts`: `Font` (`{ path, size, preload }`);
     * - `data`: a JSON file, parsed; `text`: a string; `binary`: an
     *   ArrayBuffer. Read on the job pool with `Thread.readFileAsync()`
     *   (`data` and `text` fall back to `std.loadFile()` without Thread).
     *   JSON is parsed on the script thread (objects can only be built
     *   there): keep data files small, or split a large level.
     * `Scene.Assets.define()` adds kinds.
     */
    interface Manifest {
        images?: Record<string, Spec<ImageSpec>>;
        sheets?: Record<string, Spec<Record<string, any>>>;
        sfx?: Record<string, Spec>;
        music?: Record<string, Spec<{ loop?: boolean }>>;
        fonts?: Record<string, Spec<{ size?: number; preload?: boolean | string }>>;
        data?: Record<string, Spec>;
        text?: Record<string, Spec>;
        binary?: Record<string, Spec>;
        [kind: string]: Record<string, Spec<any>> | undefined;
    }

    /** What a transition's draw gets, besides the amount of screen to cover (0..1). */
    interface TransitionInfo {
        /** "out" while the amount rises, "hold" while loading, "in" while it falls. */
        phase: "out" | "hold" | "in";
        color: number;
        width: number;
        height: number;
        direction: "left" | "right" | "up" | "down";
    }

    type TransitionDraw = (amount: number, info: TransitionInfo) => void;

    interface TransitionOptions {
        /**
         * Default "none". "fade" blends `color` over the switch; "wipe" slides
         * a band of it across (see `direction`); a name from
         * `Scene.defineTransition()`, or `{ draw(amount, info) }` drawn in
         * screen space.
         */
        transition?: "none" | "fade" | "wipe" | string | { draw: TransitionDraw };
        /** Seconds of the whole transition, out and in. Default 0.4. */
        duration?: number;
        /** Transition color (`Color.new()`); default black. */
        color?: number;
        /** The way a wipe's edge moves. Default "left". */
        direction?: "left" | "right" | "up" | "down";
    }

    interface RequestOptions {
        /** Seconds the scene may take to load; see `Scene.loadTimeout`. */
        timeout?: number;
    }

    interface GoOptions extends TransitionOptions, RequestOptions {
        /** Given to the scene's constructor, `static assets(params)` and `enter()`. */
        params?: any;
        /**
         * Exits and releases the old scenes before loading the new one: for
         * scenes that do not fit in memory together (shared assets reload).
         */
        unloadFirst?: boolean;
    }

    interface PushOptions extends TransitionOptions, RequestOptions {
        params?: any;
        /** The scenes below keep being drawn. Default true. */
        drawBelow?: boolean;
        /** The scenes below keep being updated. Default false. */
        updateBelow?: boolean;
    }

    interface PopOptions extends TransitionOptions {
        /** Given to the `resume()` of the scene below. */
        result?: any;
    }

    interface LoadingInfo {
        scene: (new (params?: any) => Scene) | null;
        loaded: number;
        total: number;
    }

    interface LoadOptions {
        /** Directory the paths are relative to. */
        root?: string;
        /**
         * Seconds to wait: past it the group fails (or `load()` rejects) with
         * a `TimeoutError`. The loads themselves go on, and are freed when
         * they end if nobody holds them.
         */
        timeout?: number;
    }

    /** What a timeout rejects with: `name` "TimeoutError", `code` "TIMEOUT". */
    interface TimeoutError extends Error {
        code: "TIMEOUT";
        /** What was still loading. */
        pending: { kind: string; name: string; path: string; state: string }[];
    }

    /** Assets of a manifest acquired together (`Scene.Assets.acquire()`). */
    interface AssetGroup {
        /** What is still loading. */
        pending(): { kind: string; name: string; path: string; state: string }[];
        /** The manifest's names with the loaded values. */
        readonly assets: any;
        readonly loaded: number;
        readonly total: number;
        /** 0..1. */
        readonly progress: number;
        readonly done: boolean;
        readonly failed: boolean;
        readonly error: Error | null;
        /** Resolves with `assets`, or rejects with the first error. */
        readonly ready: Promise<any>;
        /** Lets go of every asset; each is freed once nobody else holds it. */
        release(): void;
    }

    interface Loader<T = any> {
        /** Applies what a later holder of a cached asset asks for (images: a lock). */
        reuse?(asset: T, spec: any): void;
        /** Loads the asset: the value, or a promise (a Job) of it. */
        load(path: string, spec: any): T | PromiseLike<T>;
        /**
         * Releases it once nobody holds it. Asked for while a frame is being
         * drawn, it runs at the start of the next frame (the frame's GS
         * packet may still use the asset).
         */
        free(asset: T, spec: any): void;
        /** Tells apart loads of one path with different options (e.g. a font size). */
        key?(path: string, spec: any): string;
    }

    namespace Assets {
        /** Milliseconds of ImageList work per frame while images load. Default 4. */
        let budgetMs: number;
        /**
         * Seconds without images to load before the ImageList's decoder
         * thread (and its stack) closes; it opens again when needed. Default 5.
         */
        let imageListIdleTime: number;
        /** Acquires every asset of a manifest; release the group when done. A string option is the root. */
        function acquire(manifest: Manifest, options?: string | LoadOptions): AssetGroup;
        /** One asset; release it with `release(asset)`. A string option is the root. */
        function load<T = any>(kind: string, spec: Spec<any>, options?: string | LoadOptions): Promise<T>;
        /**
         * Releases one hold of an asset from `load()`; false if it is not
         * held. A load released before it ends keeps running and is reused if
         * the asset is asked for again (queued images are cancelled).
         */
        function release(asset: object): boolean;
        /** The same, by the kind and spec it was loaded with: for strings (`text`). */
        function release(kind: string, spec: Spec<any>, root?: string): boolean;
        /** A new kind of asset for manifests. */
        function define(kind: string, loader: Loader): void;
        /**
         * For `Debug.watch()`: held assets, still loading, holders, by kind,
         * and frees waiting for the next frame.
         */
        function stats(): { entries: number; loading: number; refs: number; byKind: Record<string, number>;
            pendingFrees: number; imageListOpen: boolean };
        /** Every held asset, for finding leaks. */
        function list(): { kind: string; path: string; refs: number; state: string }[];
        /**
         * The ImageList work, deferred frees and idle close of a frame `dt`
         * seconds long, for games without `Loop.run()`.
         */
        function update(dt?: number): void;
    }
}
