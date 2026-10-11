/**
 * Spritesheets and animated sprites, advanced in C.
 *
 * A `Sheet` holds frames (rectangles of one texture) and named clips (frame
 * sequences with durations). An `Instance` plays a clip of a sheet and draws
 * its current frame in world space, through the current camera (Camera2D).
 * An `Animator` plays clips on a range of `TileMap` sprites, rewriting their
 * texture coordinates in C, so hundreds of animated sprites cost one
 * `render()` and no JavaScript per sprite.
 *
 * While `Loop.run()` runs, every playing instance and animator advances in C
 * by the Loop's scaled time before the game's `update` (so `setTimeScale`
 * pauses them); without the Loop, call `Sprite.update(dt)`. Events (frame
 * changes, loops, end) are dispatched after that pass, only to instances
 * with listeners.
 *
 * Frame durations are in milliseconds, as Aseprite writes them; `fps` sets
 * one duration for every frame. Rotation is in radians, clockwise on screen.
 *
 * Many instances draw fastest with `Sprite.drawAll(instances, positions)`:
 * one call, and one GS packet per 128 sprites of a texture instead of one
 * per sprite.
 *
 * Example:
 * ```js
 * const sheet = Sprite.Sheet.fromGrid(new Image("hero.png"), {
 *     frameWidth: 32, frameHeight: 32,
 *     clips: {
 *         idle: { frames: "0-3", fps: 6 },
 *         run:  { frames: "4-11", fps: 12 },
 *         hit:  { frames: "12-14", fps: 10, mode: "once", next: "idle" },   // back to idle
 *         die:  { frames: "12-17", durations: [80, 80, 80, 120, 200, 400], mode: "once" },
 *     },
 * });
 * const hero = new Sprite.Instance(sheet, { clip: "idle", origin: [0.5, 1] });
 * hero.on("run:3", () => footstep.play());           // position 3 of the run clip
 * await hero.playAsync("die");                        // true when it ends
 * respawn();
 *
 * Loop.run({
 *     update() {
 *         hero.play(moving ? "run" : "idle");   // no-op while that clip plays
 *         hero.flipX = facingLeft;
 *     },
 *     draw() { hero.draw(player.x, player.y); },
 * });
 *
 * // Aseprite / TexturePacker: tags and animations become clips.
 * const slime = Sprite.Sheet.fromJSON("slime.json");   // loads meta.image
 *
 * // Batches: 200 coins spinning on a TileMap, animated in C.
 * Sprite.Animator.bind(coins, coinSheet, "spin", { randomStart: true });
 * ```
 */
declare namespace Sprite {
    /** A frame rectangle, in texels of the sheet's texture. */
    interface FrameDef {
        x: number;
        y: number;
        w: number;
        h: number;
        /**
         * Trim: where the rectangle sits inside the untrimmed frame (packers
         * cut transparent borders). Default 0.
         */
        offsetX?: number;
        offsetY?: number;
        /** Untrimmed frame size; default the rectangle plus its offset. */
        sourceWidth?: number;
        sourceHeight?: number;
        /** Milliseconds, used by clips that give no fps nor durations. */
        duration?: number;
        /** Unique name, to list the frame in clips by name. */
        name?: string;
    }

    interface Frame extends Required<Omit<FrameDef, "name">> {
        name: string | null;
        /**
         * Stored turned 90 degrees clockwise in the atlas (TexturePacker
         * "rotated"); drawn turned back, as two triangles. Animators refuse it.
         */
        rotated: boolean;
    }

    /**
     * A row or column of a `fromGrid()` sheet: `{ row: 2 }` takes that row,
     * `{ row: 2, from: 1, to: 4 }` columns 1 to 4 (`to < from` plays
     * backwards); `{ column }` takes rows.
     */
    type GridLine = { row: number; from?: number; to?: number } |
        { column: number; from?: number; to?: number };

    /**
     * The frames of a clip: `"0-3,5"` (indices and inclusive ranges; a
     * descending range plays backwards), an array of indices or frame names,
     * or a grid line.
     */
    type FrameList = string | Array<number | string> | GridLine;

    interface ClipDef {
        /** May be left out when the definition is a grid line: `{ row: 2, fps: 8 }`. */
        frames?: FrameList;
        row?: number;
        column?: number;
        from?: number;
        to?: number;
        /** Frames per second for every frame. */
        fps?: number;
        /** Milliseconds per frame, one per frame; overrides `fps`. */
        durations?: number[];
        /**
         * `"loop"` (default) repeats, `"once"` stops on the last frame, and
         * `"pingpong"` goes back and forth without repeating the ends.
         */
        mode?: "loop" | "once" | "pingpong";
        /**
         * Cycles before the clip ends (a pingpong cycle goes and returns);
         * 0 plays forever. Default 1 for `"once"`, else 0.
         */
        loops?: number;
        /** Plays the frames in reverse order. */
        reverse?: boolean;
        /**
         * Clip played when this one ends (after its last cycle), with the
         * time left over: `{ mode: "once", next: "idle" }`. It may be added
         * later; a missing one just ends.
         */
        next?: string | null;
    }

    /**
     * Without `fps` nor `durations`, a clip uses its frames' own durations
     * (sheet files) when every frame has one, else the sheet's default fps.
     */
    type Clip = ClipDef | FrameList;

    interface ClipInfo {
        name: string;
        frames: number[];
        /** Milliseconds. */
        durations: number[];
        mode: "loop" | "once" | "pingpong";
        loops: number;
        /** Milliseconds of one pass from the first frame to the last. */
        length: number;
        next: string | null;
    }

    /** A rectangle: texels of an untrimmed frame, or world units. */
    interface Rect {
        x: number;
        y: number;
        w: number;
        h: number;
    }

    interface ClipOptions {
        /** Clips to add, by name. */
        clips?: Record<string, Clip>;
        /** Default frames per second of clips without timing. Default 12. */
        fps?: number;
        /** See `Sheet.inset`. Default 0. */
        inset?: number;
    }

    interface SheetOptions extends ClipOptions {
        frames?: FrameDef[];
    }

    interface GridOptions extends ClipOptions {
        frameWidth: number;
        frameHeight: number;
        /** Texels around the grid. */
        margin?: number;
        /** Texels between cells. */
        spacing?: number;
        /** Default: as many as fit in the texture. */
        columns?: number;
        rows?: number;
        /** First cell, left to right and top to bottom. */
        first?: number;
        /** Cells to take; default all from `first`. */
        count?: number;
        /** Texture size, when the image is not loaded yet (or there is none). */
        textureWidth?: number;
        textureHeight?: number;
    }

    interface JSONOptions extends ClipOptions {
        /**
         * The texture; by default the file's `meta.image`, loaded relative to
         * the JSON file.
         */
        image?: Image | null;
    }

    /** Frames and clips of one texture, shared by any number of sprites. */
    class Sheet {
        /**
         * Frames given one by one. `image` may be null for sheets used only
         * by an `Animator` (the TileMap descriptor has the texture).
         */
        constructor(image: Image | null, options?: SheetOptions);
        /** Equal cells of a grid, numbered left to right and top to bottom. */
        static fromGrid(image: Image | null, options: GridOptions): Sheet;
        /**
         * An Aseprite or TexturePacker file (JSON Hash or Array, up to 2 MB),
         * by path or already parsed. Frames keep their names, trim and
         * durations; Aseprite tags (with direction and repeat) and
         * TexturePacker/Pixi `animations` become clips, and Aseprite slices
         * (hitboxes) become slices. Frames rotated in the atlas are drawn
         * turned back.
         */
        static fromJSON(source: string | object, options?: JSONOptions): Sheet;
        /**
         * `fromJSON()` without stalling frames: the file is read and its
         * texture (meta.image, unless `options.image` is given) decoded on
         * a worker; the Sheet is built on the script thread. A Job: await it,
         * or poll it. Rejects when the file or the texture cannot be loaded.
         */
        static fromJSONAsync(path: string, options?: JSONOptions): AthenaJob<Sheet>;
        /**
         * `fromGrid()` on an image file decoded on a worker: for sheets
         * loaded while a loading screen keeps drawing.
         */
        static fromGridAsync(imagePath: string, options: GridOptions): AthenaJob<Sheet>;
        readonly image: Image | null;
        readonly frameCount: number;
        readonly clipNames: string[];
        readonly sliceNames: string[];
        /**
         * Texels cut from every side of each frame's texture rectangle when
         * drawn (instances and animators): 0.5 stops bilinear filtering and
         * camera zoom from showing the neighbor frames of an atlas packed
         * without padding. The drawn size does not change. 0 to 16.
         */
        inset: number;
        /** Adds a frame and returns its index. */
        addFrame(frame: FrameDef): number;
        /**
         * Adds a clip, or replaces the one with that name; instances playing
         * it continue from their position.
         */
        addClip(name: string, clip: Clip): this;
        hasClip(name: string): boolean;
        /** Index of the frame called `name`, or -1. */
        findFrame(name: string): number;
        getFrame(frame: number | string): Frame;
        getClip(name: string): ClipInfo | null;
        /**
         * Sets a named rectangle (a hitbox) in untrimmed frame texels on the
         * given frames (default all); `null` removes it from them.
         */
        setSlice(name: string, rect: Rect | null, frames?: number | FrameList): this;
        /** The slice's rectangle in a frame, or null when the frame has none. */
        getSlice(name: string, frame: number | string): Rect | null;
    }

    interface InstanceOptions {
        /** Clip to play at once. */
        clip?: string;
        /** Still frame shown without a clip; default 0. */
        frame?: number | string;
        /** Position used by `draw()`, `getBounds()` and `drawAll()` without one. */
        x?: number;
        y?: number;
        /**
         * Point of the untrimmed frame placed at the draw position, 0..1:
         * `[0.5, 1]` is the bottom center. Default `[0, 0]`.
         */
        origin?: number | [number, number] | { x: number; y: number };
        scale?: number | [number, number] | { x: number; y: number };
        /** Radians, about the origin. */
        rotation?: number;
        flipX?: boolean;
        flipY?: boolean;
        /** `Color.new()`; 128 per channel is the texture unchanged. */
        color?: number;
        /** Playback speed multiplier. Default 1. */
        speed?: number;
        /**
         * Advanced by the Loop (or `Sprite.update()`). False leaves it to
         * `update(dt)`. Default true.
         */
        autoUpdate?: boolean;
        /** Draws its outline (green), origin (red) and slices (yellow) over it. */
        debug?: boolean;
        /**
         * Plays by the real time, not the Loop's scaled time: menus and HUD
         * keep animating while `Loop.setTimeScale(0)` pauses the game.
         */
        realTime?: boolean;
    }

    interface PlayOptions {
        /** Starts over even when this clip is already playing. */
        restart?: boolean;
        /** Position in the clip to start from. */
        position?: number;
        speed?: number;
    }

    /**
     * `"frame"`: every frame change; `"frame:N"`: position N of any clip
     * becomes current (0 is the first frame, reported when a clip starts);
     * `"run:N"`: position N of the clip "run"; `"loop"`: a cycle ended and
     * another starts; `"end"`: the last cycle ended (once per play).
     * `"loop:run"` and `"end:run"` only for that clip. Clip names must
     * exist when the listener is added.
     */
    type EventName = "frame" | "loop" | "end" | `frame:${number}` | `${string}:${number}` |
        `loop:${string}` | `end:${string}`;

    /**
     * Called with `this` being the instance: the clip name, the position in
     * the clip and the sheet frame shown.
     */
    type Listener = (this: Instance, clip: string | null, position: number,
        frame: number) => void;

    interface Bounds {
        x: number;
        y: number;
        w: number;
        h: number;
    }

    /** An animated sprite: a sheet, the clip it plays and how it is drawn. */
    class Instance {
        constructor(sheet: Sheet, options?: InstanceOptions);
        readonly sheet: Sheet;
        /** The clip being played or paused, or null. */
        readonly clip: string | null;
        /** Sheet frame shown; setting it shows that frame without a clip. */
        frame: number;
        /** Position in the clip; setting it jumps there (and replays a finished clip). */
        position: number;
        readonly playing: boolean;
        /** Paused by `pause()` or `stop()`: `resume()` plays on. */
        readonly paused: boolean;
        /** The clip ended; it stays on its last shown frame. */
        readonly finished: boolean;
        /** Progress through the whole clip, all cycles when finite, 0..1. */
        readonly progress: number;
        /** Cycles completed. */
        readonly cycles: number;
        speed: number;
        /** Position used by `draw()` without arguments and by `drawAll()`. */
        x: number;
        y: number;
        flipX: boolean;
        flipY: boolean;
        originX: number;
        originY: number;
        scaleX: number;
        scaleY: number;
        rotation: number;
        color: number;
        autoUpdate: boolean;
        realTime: boolean;
        /** See `InstanceOptions.debug`; `Sprite.setDebug()` turns it on for all. */
        debug: boolean;
        /** Untrimmed size of the current frame, scaled. */
        readonly width: number;
        readonly height: number;
        /**
         * Plays a clip from its start. Calling it again for the clip already
         * playing does nothing, so it can be called every frame.
         * @throws RangeError when the sheet has no such clip.
         */
        play(clip: string, options?: PlayOptions): this;
        /**
         * `play()`, and a promise of true when the clip ends, or false when
         * another play, `stop()` or a still frame replaces it first. A clip
         * that loops forever only settles by being replaced. The instance
         * stays alive while the promise is pending.
         */
        playAsync(clip: string, options?: PlayOptions): Promise<boolean>;
        pause(): this;
        resume(): this;
        /**
         * Goes back to the clip's first frame and pauses there, without an
         * end event; `resume()` plays it again.
         */
        stop(): this;
        /**
         * Advances this instance by `dt` seconds now and dispatches its
         * events, for instances with `autoUpdate: false`.
         */
        update(dt: number): this;
        /**
         * Draws the current frame with its origin at (x, y), or at its own
         * x and y, in world space. Nothing is drawn while the image is still
         * loading. For many instances, `Sprite.drawAll()` is much cheaper.
         */
        draw(x?: number, y?: number): void;
        /** Box of the frame drawn at (x, y) (default its own), ignoring rotation. */
        getBounds(x?: number, y?: number): Bounds;
        /**
         * World rectangle of slice `name` in the current frame drawn at
         * (x, y) (default its own): flipped with the sprite, placed by the
         * origin and scaled, ignoring rotation. Null when the frame has none.
         */
        getSlice(name: string, x?: number, y?: number): Rect | null;
        setOrigin(x: number, y?: number): this;
        setScale(x: number, y?: number): this;
        on(event: EventName, listener: Listener): this;
        /** A listener removed after its first call. */
        once(event: EventName, listener: Listener): this;
        /** No argument removes every listener; no listener, those of the event. */
        off(event?: EventName, listener?: Listener): this;
    }

    interface Range {
        /** First sprite, relative to the animator's range. Default 0. */
        first?: number;
        /** Default: up to the end of the range. */
        count?: number;
    }

    interface BindOptions {
        /** First sprite of the TileMap buffer. Default 0. */
        first?: number;
        /** Sprites to animate; default up to the end of the buffer. */
        count?: number;
        /** Each sprite starts at a random point of the clip. */
        randomStart?: boolean;
        /** Seed of `randomStart`, for the same start every run. */
        seed?: number;
        speed?: number;
        flipX?: boolean;
        flipY?: boolean;
        /** Plays by the real time (see `InstanceOptions.realTime`). */
        realTime?: boolean;
    }

    interface AnimatorPlayOptions extends Range {
        /** Starts over the sprites already playing this clip. */
        restart?: boolean;
        randomStart?: boolean;
    }

    /**
     * Clips played on TileMap sprites. Only texture coordinates are
     * written: position, size, color and depth stay the game's, and trim
     * offsets are not applied (use untrimmed frames of one size). Sprites
     * cannot rotate (the VU1 program has no per-sprite rotation), nor show
     * frames turned in the atlas. Animators send no events: poll
     * `isFinished()` or `finishedCount`.
     */
    class Animator {
        private constructor();
        /**
         * Plays `clip` on sprites [first, first + count) of `instance`'s
         * buffer until `unbind()`; the animator keeps itself alive until then.
         * The buffer is looked up every frame, so `replaceSpriteBuffer()` is
         * followed; sprites past a smaller buffer are skipped.
         */
        static bind(instance: TileMap.Instance, sheet: Sheet, clip: string,
            options?: BindOptions): Animator;
        readonly instance: TileMap.Instance;
        readonly sheet: Sheet;
        readonly first: number;
        readonly count: number;
        readonly paused: boolean;
        /** False after `unbind()`. */
        readonly bound: boolean;
        readonly realTime: boolean;
        /** Sprites whose clip ended, counted in C. */
        readonly finishedCount: number;
        /** Speed of every sprite. */
        speed: number;
        play(clip: string, options?: AnimatorPlayOptions): this;
        setFlip(flipX: boolean, flipY: boolean, range?: Range): this;
        pause(): this;
        resume(): this;
        /** Stops animating; the sprites keep their last frame. */
        unbind(): void;
        /** Sheet frame shown by sprite `first + index`. */
        frameAt(index: number): number;
        /** Whether sprite `first + index` is playing. */
        isPlaying(index: number): boolean;
        /** Whether the clip of sprite `first + index` ended. */
        isFinished(index: number): boolean;
    }

    /**
     * Advances every playing instance and bound animator by `dt` seconds and
     * dispatches their events: for games without `Loop.run()`. Inside
     * `Loop.run()` it would advance them twice.
     * @throws RangeError when listeners nest advances more than 8 levels deep.
     */
    function update(dt: number): void;

    /**
     * Draws instances in order, in few GS packets: consecutive instances of
     * one texture share a packet (128 per chunk), and those outside the
     * current camera's viewport are skipped. `positions[2 * i]` and
     * `positions[2 * i + 1]` place instance i (and become its x and y);
     * without positions each draws at its own x and y. Rotated instances are
     * drawn one by one, keeping the order.
     *
     * With `{ stride: 3 }` the values are x, y and rotation per instance: the
     * layout of Box2D's `world.readTransforms()`, so physics sprites need no
     * loop in JavaScript:
     * ```js
     * world.readTransforms(bodies, transforms);
     * Sprite.drawAll(crates, transforms, { stride: 3 });
     * ```
     */
    function drawAll(instances: Instance[], positions?: Float32Array | number[],
        options?: { stride?: 2 | 3 }): void;

    /**
     * Draws the outline, origin and slices of every instance drawn from
     * now on (`instance.debug` does it for one). Returns the previous state.
     */
    function setDebug(on: boolean): boolean;

    function getStats(): {
        /** Instances advanced automatically. */
        playing: number;
        animators: number;
        /** TileMap sprites the animators drive. */
        animatedSprites: number;
        /** Sprites the last `drawAll()` sent and skipped outside the camera. */
        drawn: number;
        culled: number;
    };
}
