/**
 * 2D cameras, applied in C by every 2D draw: `Draw`, `Image`, `Font` and
 * `TileMap` go through the camera's transform before reaching the GS, so the
 * game draws in world coordinates and never subtracts the camera by hand.
 *
 * `Camera2D.main` is the current camera from the start. It shows the world
 * from (0, 0) at zoom 1, exactly like the screen, so nothing changes until
 * it moves. While `Loop.run()` runs, the current camera is updated after the
 * game's `update` and applied around its `draw`; `Camera2D.screenSpace()`
 * draws the HUD without it. Cameras use the scaled time of the Loop.
 *
 * A camera's position is the world point shown at its anchor (the center of
 * its viewport by default). Zoom is screen pixels per world unit, rotation is
 * in radians (clockwise on screen) and smoothing speeds are rates per second
 * (frame-rate independent: `1 - exp(-rate * dt)` of the distance each frame;
 * 0 means rigid).
 *
 * Example:
 * ```js
 * const cam = Camera2D.main;
 * cam.follow(player, { lerp: 8, deadzone: { w: 64, h: 32 }, lookahead: 40 });
 * cam.setBounds(0, 0, mapWidth, mapHeight);   // centered if the map is smaller
 * cam.zoom = 1.5;
 *
 * Loop.run({
 *     update(dt) { player.update(dt); if (hit) cam.shake(6, 0.3); },
 *     draw() {
 *         cam.draw(() => sky.draw(0, 0), { parallax: 0.3 });   // slower layer
 *         level.render(0, 0);                    // TileMap, in world space
 *         heroImage.draw(player.x, player.y);    // Image, in world space
 *         Camera2D.screenSpace(() => font.print(10, 10, `HP ${hp}`));
 *     },
 * });
 *
 * // Split screen: one camera per viewport, no current camera.
 * const left = new Camera2D.Camera({ viewport: { x: 0, y: 0, w: 320, h: 448 } });
 * const right = new Camera2D.Camera({ viewport: { x: 320, y: 0, w: 320, h: 448 } });
 * left.follow(p1); right.follow(p2);
 * Camera2D.setCurrent(null);
 * // in draw(): left.draw(drawWorld); right.draw(drawWorld);
 * ```
 *
 * Under a rotation, rectangles (images, glyphs, TileMap sprites) are drawn as
 * two triangles (TileMap sprites, on VU1, as triangle strips). While a camera
 * is applied, what lies entirely outside its viewport is skipped in C before
 * reaching the GS: images, rectangles, circles, each sprite of
 * `Image.drawList()`, whole texts, and the cells of `TileMap` grids.
 */
declare namespace Camera2D {
    interface Point {
        x: number;
        y: number;
    }

    interface Rect {
        x: number;
        y: number;
        w: number;
        h: number;
    }

    /** A number for both axes, `[x, y]`, `{ x, y }` or `{ w, h }`. */
    type Pair = number | [number, number] | { x: number; y: number } | { w: number; h: number };

    /** Anything with a position in world space: a player, an enemy, a point. */
    interface Target {
        x: number;
        y: number;
    }

    interface CameraOptions {
        /** World point at the anchor; defaults to half the viewport (the identity view). */
        x?: number;
        y?: number;
        /** Both axes; `zoomX`/`zoomY` override one. Default 1. */
        zoom?: number;
        zoomX?: number;
        zoomY?: number;
        /** Radians, clockwise on screen. */
        rotation?: number;
        /** Screen rectangle the camera draws into; null (default) is the whole screen. */
        viewport?: Rect | null;
        /** Point of the viewport the position is shown at, 0..1. Default [0.5, 0.5]. */
        anchor?: Pair;
        /** Round the translation to whole pixels (no shimmer on pixel art). Default true. */
        pixelSnap?: boolean;
        /** World bounds the camera never shows past. */
        bounds?: Rect | null;
        /** See `Camera.boundsIgnoreRotation`. Default false. */
        boundsIgnoreRotation?: boolean;
        /** Make it the current camera. */
        current?: boolean;
        /** See `Camera.realTime`. Default false. */
        realTime?: boolean;
        /** See `Camera.debug`. Default false. */
        debug?: boolean;
    }

    interface FollowOptions {
        /** Smoothing rate per axis, per second; 0 (default) follows rigidly. Try 5-10. */
        lerp?: Pair;
        /**
         * Screen pixels around the anchor where the target moves without
         * moving the camera: a box of the screen, whatever the zoom and
         * rotation. The target may rest anywhere in it, off the center.
         */
        deadzone?: Pair;
        /**
         * Screen pixels to look ahead of the target's motion, kept once it
         * stops (so the target rests that far off the center, less the dead
         * zone). Same distance on screen at any zoom.
         */
        lookahead?: Pair;
        /** How fast the lookahead turns around, per second. Default 4. */
        lookaheadLerp?: number;
        /** World offset added to the target (e.g. to look a bit above the player). */
        offset?: Pair;
        /**
         * With several targets: zoom to keep them all in view. `margin` is in
         * screen pixels (default 32), `min`/`max` limit the zoom (0.5 and 2),
         * `lerp` smooths it (4 per second).
         */
        autoZoom?: boolean | { min?: number; max?: number; margin?: number; lerp?: number };
        /** Jump to the target now instead of easing from the current position. Default true. */
        snap?: boolean;
        /**
         * For `Loop.run()` with `fixedStep`, when the game draws positions
         * blended by `alpha`: the camera follows the target blended the
         * same way (sampled before each step and after the last), so the
         * target does not jitter against the scenery. Default false.
         */
        interpolate?: boolean;
        /**
         * Zooms out as the target speeds up: `max` (default 1) at rest,
         * `min` (0.8) at `speed` world units per second (300) or faster.
         * In a zone with a zoom, that zoom takes the place of `max`.
         * `lerp` smooths the zoom (4 per second). `autoZoom` wins over it.
         */
        zoomBySpeed?: boolean | { min?: number; max?: number; speed?: number; lerp?: number };
    }

    interface Zone extends Rect {
        /** Zoom while the target is in this zone. */
        zoom?: number;
        /** Follow smoothing while the target is in this zone. */
        lerp?: Pair;
        /** Target offset (world units) while the target is in this zone. */
        offset?: Pair;
    }

    interface ZoneOptions {
        /** Seconds to glide from one zone to the next (0: cut). */
        transition?: number;
        /** Called when the target enters a zone: its index and the previous one (-1 for none). */
        onChange?: (this: Camera, zone: number, previous: number) => void;
    }

    interface TraumaOptions {
        /** Pixels at full trauma. Default 16. */
        intensity?: number;
        /** Radians at full trauma. Default 0. */
        rotation?: number;
        /** Trauma lost per second. Default 1. */
        decay?: number;
        /** Oscillations per second. Default 25. */
        frequency?: number;
    }

    interface RepeatOptions extends DrawOptions {
        /** Where one copy sits in the layer. Default (0, 0). */
        x?: number;
        y?: number;
        /** Repeat along each axis. Default true. */
        repeatX?: boolean;
        repeatY?: boolean;
    }

    /** What `state()` returns and `setState()` takes: JSON-friendly. */
    interface State {
        x: number;
        y: number;
        zoomX: number;
        zoomY: number;
        rotation: number;
        anchor: Point;
        /** null: the whole screen. */
        viewport: Rect | null;
        bounds: Rect | null;
        boundsIgnoreRotation: boolean;
        pixelSnap: boolean;
        realTime: boolean;
    }

    interface TransitionOptions {
        /** Maps 0..1 to the blend. Default smoothstep; e.g. `Ease.inOutCubic`. */
        ease?: (t: number) => number;
        /** Runs on real time, also while the game is paused. */
        realTime?: boolean;
    }

    interface StackOptions extends TransitionOptions {
        /** Seconds of the transition; 0 (default) cuts. */
        duration?: number;
    }

    interface ShakeOptions {
        /** Oscillations per second. Default 25. */
        frequency?: number;
        /** Largest rotation, in radians. Default 0. */
        rotation?: number;
    }

    interface DrawOptions {
        /**
         * 1 (default) draws the world; 0 draws in the viewport at zoom 1 like
         * the screen; between them, a background layer that scrolls (and
         * zooms) slower than the world. Per axis with `[x, y]`.
         */
        parallax?: Pair;
    }

    /** The world-to-screen transform: screen = (xx*x + xy*y + tx, yx*x + yy*y + ty). */
    interface Matrix {
        xx: number;
        xy: number;
        yx: number;
        yy: number;
        tx: number;
        ty: number;
    }

    class Camera {
        constructor(options?: CameraOptions);

        /** World point shown at the anchor. Setting it stops a pan. */
        x: number;
        y: number;
        /** Uniform zoom (reads `zoomX`). Setting it stops a `zoomTo()`. */
        zoom: number;
        zoomX: number;
        zoomY: number;
        /** Radians, clockwise on screen. */
        rotation: number;
        pixelSnap: boolean;
        /** Reads as `{ x, y }`; set with any `Pair`. */
        anchor: Point;
        /** The viewport in screen pixels (the whole screen when none was set); null resets it. */
        viewport: Rect | null;
        /** World bounds, or null. */
        bounds: Rect | null;
        /**
         * Bounds and zones clamp the view as if it were not turned: the
         * camera stays on its target near the edges, and the corners of a
         * turned view may show past the bounds. Default false: nothing past
         * the bounds ever shows, so a turned camera is pushed inward.
         */
        boundsIgnoreRotation: boolean;
        /** Index of the zone in force, or -1. */
        readonly zone: number;
        readonly following: boolean;
        readonly shaking: boolean;
        /** Letterbox bar height, as a fraction of the viewport. */
        readonly letterboxAmount: number;
        /** Opacity of the fade overlay, 0..128. */
        readonly fadeAlpha: number;
        /** Whether this is `Camera2D.getCurrent()`. */
        readonly isCurrent: boolean;
        /**
         * Runs on the real (unscaled) time of the Loop: follows, shakes and
         * fades keep going while `Loop.setTimeScale(0)` pauses the game
         * (pause menus that fade the screen).
         */
        realTime: boolean;
        /**
         * Draws what drives the camera over its viewport: dead zone (cyan),
         * anchor (white), target (green), goal (blue), lookahead (orange),
         * bounds (red) and zones (magenta, the active one brighter).
         */
        debug: boolean;
        /** Current trauma, 0..1 (`addTrauma()`). */
        readonly trauma: number;

        setPosition(x: number, y: number): this;
        /** Moves by (dx, dy), within the bounds. */
        move(dx: number, dy: number): this;
        setZoom(zoomX: number, zoomY?: number): this;
        setViewport(x: number, y: number, w: number, h: number): this;
        setViewport(rect: Rect | null): this;
        /**
         * The camera never shows past these bounds; a smaller area is
         * centered. Turned, the view covers more of the world (its bounding
         * box), so near an edge a rotation pushes the camera inward and a
         * followed target rests off the center (see `boundsIgnoreRotation`).
         */
        setBounds(x: number, y: number, w: number, h: number): this;
        setBounds(rect: Rect | null): this;

        /**
         * Follows a target (read every frame, so moving the object is
         * enough), or several: their center, with `autoZoom` to fit them all.
         * A single target needs numeric x and y now (TypeError otherwise); later
         * frames keep the last position if one goes missing. In an array,
         * targets whose x or y is not a number are skipped that frame.
         */
        follow(target: Target | Target[], options?: FollowOptions): this;
        unfollow(): this;
        /** Jumps to the follow target now: no smoothing. */
        snap(): this;

        /**
         * Rooms: while the target is inside a zone, that zone is the camera's
         * bounds (and zoom). The last zone stays in force between zones.
         * `null` removes them. At most 32.
         */
        setZones(zones: Zone[] | null, options?: ZoneOptions): this;

        /** Shakes by up to `intensity` pixels, fading out over `duration` seconds. */
        shake(intensity: number, duration: number, options?: ShakeOptions): this;
        /**
         * Adds trauma (clamped to 0..1): impacts add up, the view shakes by
         * its square and it decays over time. The options are kept for
         * later calls.
         */
        addTrauma(amount: number, options?: TraumaOptions): this;
        /** Pushes the view by (dx, dy) screen pixels, springing back (default 0.15 s). */
        kick(dx: number, dy: number, duration?: number): this;
        /** Stops the shake, the trauma and the kick. */
        stopShake(): this;

        /*
         * Timed changes. Each promise resolves with true when the change ends
         * and false if another one of the same kind replaced it.
         */
        /** Eases the zoom (geometrically: 1 to 4 looks as steady as 4 to 1). */
        zoomTo(zoom: number, duration: number): Promise<boolean>;
        /** Eases to a point; suspends the follow until it arrives. */
        panTo(x: number, y: number, duration: number): Promise<boolean>;
        /**
         * Fades the viewport overlay to `color` (its alpha, 0..128, is the
         * final opacity): `Color.new(0, 0, 0, 128)` fades out to black,
         * `Color.new(0, 0, 0, 0)` fades back in.
         */
        fade(color: number, duration: number): Promise<boolean>;
        /** Shows `color` over the viewport and fades it out (default 0.2 s). */
        flash(color: number, duration?: number): Promise<boolean>;
        /** Black bars, each `amount` (0..0.5) of the viewport's height. */
        letterbox(amount: number, duration?: number): Promise<boolean>;

        worldToScreen(x: number, y: number): Point;
        screenToWorld(x: number, y: number): Point;
        /** World box the viewport shows (its bounding box when rotated), for culling. */
        visibleRect(): Rect;
        isVisible(x: number, y: number, w?: number, h?: number): boolean;
        /**
         * Culls many boxes in one call: `rects` holds (x, y, w, h) per box;
         * `out` gets 1 for each visible box and 0 otherwise. Returns how many
         * are visible.
         */
        cull(rects: Float32Array, out?: Uint8Array): number;

        /** Draws in this camera's world space and viewport until `end()`. Pairs nest (8 deep). */
        begin(options?: DrawOptions): void;
        /** Draws the camera's fade, flash and letterbox, and restores the previous view. */
        end(): void;
        /**
         * `begin()`, `fn()`, `end()`, even if `fn` throws or leaves pairs of
         * its own open (they are closed too). Returns what `fn` returns.
         */
        draw<T>(fn: () => T, options?: DrawOptions): T;
        /** Draws with (0, 0) at the viewport's corner, clipped to it: per-player HUDs. */
        viewportSpace<T>(fn: () => T): T;

        makeCurrent(): this;
        /** Advances this camera alone (without `Loop.run()`, use `Camera2D.update()`). */
        update(dt: number): this;
        getMatrix(options?: DrawOptions): Matrix;
        /**
         * Draws `image` repeated to cover what the camera shows of a layer
         * (`parallax`, as `draw()`): skies, far hills. Returns how many
         * copies were drawn (at most 1024).
         */
        drawRepeat(image: Image, options?: RepeatOptions): number;
        /** The pose and settings (not targets, zones or running effects), for saves. */
        state(): State;
        /** Applies what `state()` returned; missing keys are left as they are. */
        setState(state: Partial<State>): this;
    }

    /** The default camera: current from the start, showing the screen as before. */
    const main: Camera;

    /** The camera applied around `Loop.run()`'s draw, or null for none. */
    function getCurrent(): Camera | null;
    /** Changes it at once (null: draw without a camera, e.g. in split screen). */
    function setCurrent(camera: Camera | null): void;
    /**
     * Makes `camera` current, gliding from the current one's position, zoom,
     * rotation and viewport over `duration` seconds. `ease` maps 0..1 to the
     * blend (default smoothstep; e.g. `Ease.inOutCubic`).
     */
    function transition(camera: Camera, duration: number,
        options?: TransitionOptions): Promise<boolean>;
    /**
     * Makes `camera` current and remembers the one it replaces (or none),
     * so `pop()` goes back to it: cutscenes, map screens. 8 deep.
     */
    function push(camera: Camera, options?: StackOptions): Promise<boolean>;
    /** Goes back to the camera the last `push()` replaced. */
    function pop(options?: StackOptions): Promise<boolean>;
    /**
     * `culled`: draws skipped by culling in the last frame drawn under
     * `Loop.run()` (images, rectangles, circles, drawList sprites, texts,
     * TileMap grid cells); `pushed`: depth of `push()`.
     */
    function getStats(): { culled: number; pushed: number };

    /** Draws with no camera and the whole screen: HUD, menus. */
    function screenSpace<T>(fn: () => T): T;
    /** Updates every camera: for games that do not use `Loop.run()`. */
    function update(dt: number): void;
    /** Drops any open camera: the identity view and the whole screen. */
    function reset(): void;
}
