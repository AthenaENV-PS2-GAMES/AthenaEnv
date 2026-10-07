/** Light 3D collision in C, for levels and characters; not a rigid body
 * solver. A `World` holds static triangles (level meshes, whole Scene3D or
 * glTF subtrees, boxes) in a bounding volume hierarchy, answers raycasts,
 * sphere casts and overlaps, and is what `Character`s walk on.
 *
 * Characters are upright ellipsoids placed by their feet, moved with
 * collide-and-slide (swept, so they never tunnel through thin walls). Up is
 * +Y. Faces up to `maxSlope` are ground (standing on them does not slide),
 * steeper faces are walls, edges up to `stepHeight` above the feet are
 * climbed, and walking down slopes and steps keeps them on the ground. They
 * move by themselves in one Loop system (`attachLoop()`), after the game's
 * update and before Scene3D, and stay active until `dispose()`.
 *
 * Triangles face their counter-clockwise side: characters and sphere casts
 * collide with front faces only, rays hit both sides. Layers and masks are
 * 32-bit flags (-1: all).
 *
 * ```js
 * const level = GLTF3D.load("models/level.glb");
 * scene.root.add(level.root);
 * const world = new Collision3D.World();
 * world.addNode(level.root);
 * const player = new Collision3D.Character(world, { radius: .4, height: 1.8, position: [0, 1, 0] });
 * player.bind(heroNode);                      // the node follows the feet
 * Collision3D.attachLoop(); scene.attachLoop();
 * Loop.run({ update() {
 *     player.vx = stick.x * 4; player.vz = stick.y * 4;
 *     if (pad.justPressed(Gamepad.CROSS) && player.onGround) player.vy = 5;
 * } });
 * ```
 */
declare namespace Collision3D {
    /** Default attachLoop() priority: after Animation3D (-100), before Scene3D (0). */
    const LOOP_PRIORITY: number;
    const MAX_TRIANGLES: number;
    type Vec3 = [number, number, number] | Float32Array;
    interface ShapeOptions {
        /** Default 1. */
        layer?: number;
    }
    interface QueryOptions {
        /** Layers hit. Default -1 (all). */
        mask?: number;
    }
    /** A hit along a ray or sweep; the normal points toward the query. */
    interface Hit {
        distance: number;
        x: number; y: number; z: number;
        nx: number; ny: number; nz: number;
        /** The shape id returned by add*(). */
        shape: number;
        /** Triangle index within the shape. */
        triangle: number;
    }
    class World {
        constructor();
        /** A mesh's triangles under transform (base pose for skinned or
         * morphed meshes). Returns the shape id. */
        addMesh(mesh: Model3D.Mesh, transform?: Matrix4 | null, options?: ShapeOptions): number;
        /** Every visible mesh of the subtree, with the node's ancestors'
         * transforms composed (no scene update needed). One shape. */
        addNode(node: Scene3D.Node, options?: ShapeOptions): number;
        /** An axis-aligned box, 12 triangles facing out. */
        addBox(min: Vec3, max: Vec3, options?: ShapeOptions): number;
        /** World-space triangles, 9 floats each (copied). */
        addTriangles(positions: Float32Array, options?: ShapeOptions): number;
        /** False for an unknown id. The tree is rebuilt by the next query. */
        remove(shape: number): boolean;
        setLayer(shape: number, layer: number): boolean;
        /** Nearest hit within maxDistance (world units), or null. out is
         * filled and returned instead of a new object. */
        raycast(origin: Vec3, direction: Vec3, maxDistance: number, options?: QueryOptions, out?: Hit): Hit | null;
        /** Many rays in one call: origins 3n floats, directions 3 (shared)
         * or 3n, out 4n floats filled with [distance, nx, ny, nz] per ray
         * (distance -1 on a miss). Returns the number of hits. */
        raycastMany(origins: Float32Array, directions: Float32Array, maxDistance: number,
            out: Float32Array, options?: QueryOptions): number;
        /** A sphere swept along direction: the first front face it touches. */
        sphereCast(center: Vec3, radius: number, direction: Vec3, maxDistance: number,
            options?: QueryOptions, out?: Hit): Hit | null;
        /** Ids of the shapes within radius (both sides; at most 64). */
        overlapSphere(center: Vec3, radius: number, options?: QueryOptions): number[];
        readonly triangleCount: number;
        /** Characters keep their world alive after this. */
        dispose(): void;
    }
    interface CharacterOptions {
        /** Default 0.4. */
        radius?: number;
        /** Default 1.8. */
        height?: number;
        /** Ledges up to this are climbed. Default 0.3. */
        stepHeight?: number;
        /** Degrees; steeper faces are walls. Default 45. */
        maxSlope?: number;
        /** Default [0, -9.81, 0]. */
        gravity?: Vec3;
        /** Default -1. */
        mask?: number;
        /** Feet. Default [0, 0, 0]. */
        position?: Vec3;
    }
    class Character {
        constructor(world: World, options?: CharacterOptions);
        /** Feet position. */
        readonly x: number;
        readonly y: number;
        readonly z: number;
        /** Velocity in units/s; vy is reset on landing and on ceilings. */
        vx: number;
        vy: number;
        vz: number;
        readonly onGround: boolean;
        readonly hitWall: boolean;
        readonly hitCeiling: boolean;
        /** [x, y, z] of the ground under the feet ([0, 1, 0] in the air). */
        readonly groundNormal: number[];
        /** Teleports, without collisions. */
        setPosition(x: number, y: number, z: number): this;
        setVelocity(x: number, y: number, z: number): this;
        /** Moves by a displacement with collisions, without gravity. */
        move(dx: number, dy: number, dz: number): this;
        /** Gravity, then velocity * dt with collisions. */
        step(dt: number): this;
        /** A node whose position follows the feet; null unbinds. */
        bind(node: Scene3D.Node | null): this;
        /** Disabled characters are skipped by the Loop system and step(). */
        enabled: boolean;
        dispose(): void;
    }
    /** Steps every enabled character. */
    function step(dt: number): void;
    /** One native POST_UPDATE system stepping every character; idempotent. */
    function attachLoop(priority?: number): void;
    function detachLoop(): boolean;
    function isAttached(): boolean;
}
