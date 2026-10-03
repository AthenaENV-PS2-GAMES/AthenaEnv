/**
 * Light 2D collision and simple physics, in C. For platformers, top-down
 * games and shooters that want predictable, tile-friendly movement; Box2D
 * remains the choice for rigid bodies, joints and polygons.
 *
 * A `World` holds bodies (axis-aligned rectangles and circles) in a spatial
 * hash, and optionally a grid of tiles: solid tiles, one-way platforms and
 * floor slopes. Bodies move with a sweep along x, then along y: they slide
 * along walls, land on floors, walk up and down slopes and never tunnel
 * through thin walls, whatever their speed.
 *
 * Body types:
 * - `"static"` (default): moved only by you; blocks others.
 * - `"kinematic"`: moves through everything, by its velocity or when you
 *   set `x`/`y` (a tween, a path): moving platforms, elevators, doors. It
 *   carries the dynamic bodies standing on it and pushes those in its way;
 *   one caught against something solid is `crushed`.
 * - `"dynamic"`: gravity, damping, speed limits and velocity, blocked by
 *   what it collides with; bounces with `bounce`.
 *
 * A world steps itself with the Loop's scaled time before the game's
 * `update` (so `Loop.setTimeScale(0)` pauses it); pass `autoStep: false`
 * and call `world.step(dt)` to step it yourself. Any body can also be moved
 * with collisions by `world.move(body, dx, dy)`, like a character
 * controller.
 *
 * Positions are in world units with y pointing down: a rectangle is placed
 * by its top-left corner, a circle by its center. Velocities are in units
 * per second, gravity in units per second squared.
 *
 * Layers and masks are 32-bit flags: two bodies collide when each one's
 * `mask` has a bit of the other's `layer`. Read back, they are signed
 * integers, like the results of `|` and `<<`.
 *
 * Example:
 * ```js
 * const SOLID = 1, PLAYER = 2, ENEMY = 4, COIN = 8;
 * const world = new Collision.World({ gravity: { x: 0, y: 900 } });
 * world.setGrid({
 *     columns: 40, rows: 15, tileWidth: 16, tileHeight: 16,
 *     tiles: levelIds,                        // the ids given to TileMap.setTiles()
 *     solid: [1, 2, 3], oneWay: [4], slopes: { 5: "45r", 6: "45l" },
 * });
 * const player = world.add({ type: "dynamic", x: 32, y: 32, w: 12, h: 24,
 *     layer: PLAYER, mask: SOLID | ENEMY | COIN });
 * const lift = world.add({ type: "kinematic", x: 200, y: 160, w: 48, h: 8 });
 * Tween.to(lift, { x: 320 }, 2, { yoyo: true, repeat: Infinity });   // carries the player
 * world.add({ x: 300, y: 100, r: 6, sensor: true, layer: COIN });
 * world.onEnter = (sensor, body) => { if (body === player) sensor.remove(); };
 *
 * Loop.run({
 *     update() {
 *         player.vx = pad.pressed(Gamepad.RIGHT) ? 120 : pad.pressed(Gamepad.LEFT) ? -120 : 0;
 *         if (pad.justPressed(Gamepad.CROSS) && player.onGround) player.vy = -330;
 *         player.dropThrough = pad.pressed(Gamepad.DOWN);   // fall through one-way platforms
 *     },
 *     draw() {
 *         hero.draw(player.centerX, player.bottom);        // a sprite with origin [0.5, 1]
 *         world.drawDebug();
 *     },
 * });
 * ```
 */
declare namespace Collision {
    type BodyType = "static" | "kinematic" | "dynamic";

    /** -1: blocked moving left, 1: moving right, 0: not blocked. */
    type WallSide = -1 | 0 | 1;

    /** A shape outside a world, or a body. */
    type Shape = Body | { x: number; y: number; w: number; h: number } |
        { x: number; y: number; r: number };

    /**
     * A floor slope: heights of its surface at the tile's left and right
     * sides, as fractions of the tile height (0 = the tile's bottom, 1 = its
     * top). Presets: `"45r"` rises to the right ([0, 1]), `"45l"` falls
     * ([1, 0]); gentle slopes over two tiles: `"22r1"` then `"22r2"` rise
     * ([0, 0.5], [0.5, 1]), `"22l1"` then `"22l2"` fall.
     */
    type Slope = [number, number] | "45r" | "45l" | "22r1" | "22r2" | "22l1" | "22l2";

    interface WorldOptions {
        /** Side of the spatial hash cells: about the size of a typical body. Default 64. */
        cellSize?: number;
        /** Default { x: 0, y: 0 }. */
        gravity?: { x: number; y: number } | [number, number];
        /**
         * Stepped by the Loop's scaled time before the game's update (default
         * true on the main script; workers step with `step()`).
         */
        autoStep?: boolean;
    }

    interface BodyOptions {
        /** Rectangle: top-left corner. Circle: center. Default 0. */
        x?: number;
        y?: number;
        /** A rectangle: its size. */
        w?: number;
        h?: number;
        /** A circle: its radius (instead of w and h). */
        r?: number;
        /** Default "static". */
        type?: BodyType;
        vx?: number;
        vy?: number;
        /** Default 1. */
        layer?: number;
        /** Default -1: every layer. */
        mask?: number;
        /** Found by queries, pairs and onEnter/onExit, but never blocks nor is blocked. */
        sensor?: boolean;
        /** Blocks only bodies coming from above (jump-through platforms). */
        oneWay?: boolean;
        /** Dynamic bodies: gravity multiplier (default 1). */
        gravityScale?: number;
        /** Dynamic bodies: velocity decay rate in 1/s (default 0). */
        damping?: number;
        /** Dynamic bodies: velocity kept after hitting something, 0 to 1 (default 0). */
        bounce?: number;
        /** Dynamic bodies: speed limits per axis (0, the default, is none). */
        maxSpeedX?: number;
        maxSpeedY?: number;
    }

    interface GridOptions {
        /** At most 4096 each, and 1048576 tiles in all (1024 x 1024, 4096 x 256...). */
        columns: number;
        rows: number;
        tileWidth: number;
        tileHeight: number;
        /** Top-left corner of the grid. Default 0. */
        x?: number;
        y?: number;
        /**
         * Row-major tile ids, `columns * rows` long (copied); default all 0.
         * The same ids as `TileMap.Instance.setTiles()`: a grid instance's
         * `grid` gives columns, rows and the tile size.
         */
        tiles?: Uint16Array | number[];
        /** Ids of solid tiles. */
        solid?: number[];
        /** Ids of one-way platforms (solid from above only). */
        oneWay?: number[];
        /** Floor slopes by id. Below the surface the tile is solid; its high side is a wall. */
        slopes?: { [id: number]: Slope };
        /** Layer of the tiles, for masks. Default 1. */
        layer?: number;
    }

    /**
     * A blocking contact: another body (`tile` is -1) or a tile (`body` is
     * null), and the normal of the surface hit, pointing away from it:
     * { normalX: 0, normalY: -1 } for a floor.
     */
    interface Contact {
        body: Body | null;
        tile: number;
        column: number;
        row: number;
        normalX: number;
        normalY: number;
    }

    interface MoveResult {
        /** Distance actually moved. */
        dx: number;
        dy: number;
        onGround: boolean;
        hitCeiling: boolean;
        hitWall: WallSide;
        /** What stopped the x sweep and the y sweep. */
        contacts: Contact[];
    }

    interface RayHit {
        x: number;
        y: number;
        normalX: number;
        normalY: number;
        /** Of the segment, 0 to 1. */
        fraction: number;
        distance: number;
        /** The body hit, or null for a tile. */
        body: Body | null;
        /** The tile id hit, or -1 for a body. */
        tile: number;
        column: number;
        row: number;
    }

    interface RaycastOptions {
        /** Layers hit (bodies, and the grid when its layer is in it). Default -1. */
        mask?: number;
        /** A body to skip, such as the one casting the ray. */
        ignore?: Body | null;
        /** Also hit sensors. Default false. */
        sensors?: boolean;
    }

    class World {
        constructor(options?: WorldOptions);
        gravityX: number;
        gravityY: number;
        readonly cellSize: number;
        readonly bodyCount: number;
        autoStep: boolean;
        /**
         * Called after each step when a dynamic body begins a contact: it
         * lands or steps onto another body, or starts pushing a wall or a
         * ceiling. Resting contacts (standing, walking along a floor,
         * pushing the same wall) are not repeated every step. `this` is the
         * world, which may be changed from it.
         */
        onContact: ((this: World, body: Body, contact: Contact) => void) | null;
        /**
         * Called after each step when a sensor starts overlapping a body it
         * collides with (layers and masks): pickups, damage zones, triggers.
         * Two static bodies are never a pair; two sensors are one pair. The
         * world may be changed from it (removing the coin, for instance).
         * Overlaps are only tracked while `onEnter` or `onExit` is set.
         */
        onEnter: ((this: World, sensor: Body, other: Body) => void) | null;
        /** As `onEnter`, when the overlap ends. A removed body gets no exit. */
        onExit: ((this: World, sensor: Body, other: Body) => void) | null;
        /** The grid's geometry, or undefined without one. */
        readonly grid: { columns: number; rows: number; tileWidth: number; tileHeight: number;
            x: number; y: number; layer: number } | undefined;

        /** Adds a body: `w` and `h` make a rectangle, `r` a circle. */
        add(options: BodyOptions): Body;
        /** Removes a body of this world; false when it is not in it. */
        remove(body: Body): boolean;
        /** Removes every body. */
        clear(): void;
        bodies(): Body[];

        /**
         * Moves a body by (dx, dy) with collisions: along x, then along y,
         * each time stopping at the first thing in the way. Walking into a
         * slope follows its floor; a body that stood on the ground steps up
         * small ledges and sticks to floors going down, as far as the
         * steepest slope of the grid needs. Fast bodies move in several
         * sweeps of at most half a tile, so they never take a wall for a
         * step. Sensors move freely; kinematic bodies move through
         * everything, carrying and pushing (as setting `x`/`y` does).
         * Updates `onGround`, `onCeiling`, `onWall` and `ground`.
         *
         * `out`, when given, receives the result instead of a new object
         * (its `contacts` array is emptied and reused): moving many bodies
         * every frame then allocates nothing.
         */
        move<T extends object = MoveResult>(body: Body, dx: number, dy: number, out?: T): T & MoveResult;
        /**
         * Advances the world: kinematic bodies move by their velocity,
         * carrying their riders and pushing what is in their way, then
         * dynamic bodies get gravity, damping and speed limits and move; a
         * blocked axis loses its velocity or bounces. Then `onContact`,
         * `onEnter` and `onExit` run. With `autoStep` the Loop calls it.
         */
        step(dt: number): void;

        /** Uses a grid of tiles, replacing the previous one. */
        setGrid(options: GridOptions): void;
        clearGrid(): void;
        /** Tile id at a cell, or -1 outside the grid. */
        getTile(column: number, row: number): number;
        /** Changes a cell (a door opens, a block breaks); false outside the grid. */
        setTile(column: number, row: number, id: number): boolean;
        /** The cell holding a point, or null outside the grid. */
        cellAt(x: number, y: number): { column: number; row: number; tile: number } | null;
        /**
         * Whether a point is inside something solid: a solid tile, a slope
         * below its surface or a body that is not a sensor, on a layer of
         * `mask` (default -1). One-way platforms are not solid. For ledge
         * checks and AI ("is there floor ahead?").
         */
        solidAt(x: number, y: number, mask?: number): boolean;

        /** Bodies overlapping a rectangle whose layer is in `mask` (default -1). Sensors included. */
        query(x: number, y: number, w: number, h: number, mask?: number): Body[];
        queryCircle(x: number, y: number, r: number, mask?: number): Body[];
        queryPoint(x: number, y: number, mask?: number): Body[];
        /** Bodies overlapping `body` that it collides with (layers and masks), sensors too. */
        overlapping(body: Body): Body[];
        /**
         * Overlapping pairs [a, b] with `a` on a layer of `layerA` and `b` on
         * a layer of `layerB`, found in C: bullets against enemies without a
         * loop over every pair in JavaScript. Masks are not used; each pair
         * is reported once. More than 65536 pairs throw a RangeError.
         */
        pairs(layerA: number, layerB: number): Array<[Body, Body]>;
        /**
         * The same pairs, passed to `callback` instead of built into arrays;
         * returns how many there were. Pairs of a body removed by an earlier
         * call are skipped.
         */
        pairs(layerA: number, layerB: number, callback: (a: Body, b: Body) => void): number;
        /**
         * First body or tile hit by the segment from (x1, y1) to (x2, y2).
         * Shapes and solid tiles holding the start are ignored; one-way tiles
         * and bodies are only hit from above.
         */
        raycast(x1: number, y1: number, x2: number, y2: number,
            options?: RaycastOptions): RayHit | null;

        /**
         * Outlines the bodies and tiles in view, through the current camera:
         * static bodies white, kinematic blue, dynamic green, sensors yellow,
         * tiles red, one-way platforms orange. Call it in `draw`.
         */
        drawDebug(options?: { bodies?: boolean; tiles?: boolean }): void;
    }

    /**
     * A body of a world, made by `world.add()`. After `remove()` it is
     * detached: `valid` is false and other properties throw.
     */
    class Body {
        private constructor();
        /** The world, or null once removed. */
        readonly world: World | null;
        readonly valid: boolean;
        readonly shape: "rect" | "circle";
        type: BodyType;
        /**
         * Setting them teleports the body, without collisions, and clears its
         * contact state. A kinematic body moves instead, carrying its riders
         * and pushing what is in its way: animate platforms with Tween.
         */
        x: number;
        y: number;
        /** Size: a circle's is 2 r and read-only. */
        w: number;
        h: number;
        /** Radius of a circle; undefined for a rectangle. */
        r: number | undefined;
        /** Center of the body. */
        readonly centerX: number;
        readonly centerY: number;
        /** Right and bottom edges of its bounds: `bottom` is where the feet are. */
        readonly right: number;
        readonly bottom: number;
        vx: number;
        vy: number;
        layer: number;
        mask: number;
        sensor: boolean;
        oneWay: boolean;
        /** Falls through one-way platforms while set. */
        dropThrough: boolean;
        gravityScale: number;
        damping: number;
        bounce: number;
        maxSpeedX: number;
        maxSpeedY: number;
        /** Contact state after the last move or step of this body. */
        readonly onGround: boolean;
        readonly onCeiling: boolean;
        readonly onWall: WallSide;
        /** The body stood on, or null (in the air or on a tile). */
        readonly ground: Body | null;
        /**
         * A kinematic body pushed it into something solid during the last
         * step (or since, by setting its `x`/`y`): it is caught between them.
         * The game decides what that means; the kinematic body keeps moving.
         */
        readonly crushed: boolean;

        /** `world.move(this, dx, dy, out)`. */
        move<T extends object = MoveResult>(dx: number, dy: number, out?: T): T & MoveResult;
        /** Teleports the body, without collisions, and clears its contact state. */
        setPosition(x: number, y: number): void;
        /** Axis-aligned bounds (a circle's box). */
        getBounds(): { x: number; y: number; w: number; h: number };
        /** Removes the body from its world; false if it was already removed. */
        remove(): boolean;
    }

    /** Whether two shapes overlap (touching is not overlapping). */
    function overlaps(a: Shape, b: Shape): boolean;
    /** The shortest move that takes `a` out of `b`, or null when they do not overlap. */
    function resolve(a: Shape, b: Shape): { x: number; y: number } | null;
    /** Where the segment enters the shape, or null when it misses or starts inside. */
    function segment(x1: number, y1: number, x2: number, y2: number, shape: Shape):
        { fraction: number; x: number; y: number; normalX: number; normalY: number } | null;
}
