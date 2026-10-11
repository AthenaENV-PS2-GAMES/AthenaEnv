/**
 * World-space debug lines for 3D games: AABBs, rays, hitboxes, AI paths, a
 * second camera's frustum, mesh normals, chunk borders.
 *
 * Shapes become line segments queued in C; each frame they are projected
 * with a Camera3D and clipped (near plane and screen edges) on the EE, then
 * drawn as GS lines over the frame, without depth test (always on top).
 * Far-plane clipping is disabled for distant diagnostics; transformed
 * non-finite endpoints are discarded. A
 * segment lasts one frame (`seconds` omitted or 0) or `seconds` of real
 * time. At most MAX_LINES segments are queued; more are dropped and counted
 * by dropped(). Colors are Color.new() values (default green).
 *
 * With Loop.run(), setCamera() draws the queue after every draw (before
 * the Debug overlay). Games with their own loop call draw(camera, dt) after
 * drawing the scene.
 *
 * Not in the default build: `node tools/modules.js configure --modules=debug3d,...`
 *
 * Example:
 * ```js
 * Debug3D.setCamera(camera);
 * Debug3D.grid(0, 0, 0, 10, 1, Color.new(60, 60, 60));
 * const b = node.getWorldBounds();
 * if (b) Debug3D.box(b.min[0], b.min[1], b.min[2], b.max[0], b.max[1], b.max[2]);
 * Debug3D.line(ray.x, ray.y, ray.z, ray.x + ray.dx * 20, ray.y + ray.dy * 20, ray.z + ray.dz * 20,
 *     Color.new(255, 0, 0), 1);         // stays one second
 * ```
 */
declare namespace Debug3D {
    const MAX_LINES: number;
    /** Queues a segment; returns false when the queue is full. */
    function line(x1: number, y1: number, z1: number, x2: number, y2: number, z2: number,
        color?: Color.Value, seconds?: number): boolean;
    /** Many segments from a Float32Array of x1, y1, z1, x2, y2, z2 groups; returns how many were queued. */
    function lines(values: Float32Array, color?: Color.Value, seconds?: number): number;
    /** Box edges (12 segments), in world space or under `matrix` (an oriented box). Returns segments queued. */
    function box(minX: number, minY: number, minZ: number, maxX: number, maxY: number, maxZ: number,
        color?: Color.Value, seconds?: number, matrix?: Matrix4): number;
    /** Three great circles of `segments` (3 to 64, default 16) segments each. */
    function sphere(x: number, y: number, z: number, radius: number, color?: Color.Value, seconds?: number,
        segments?: number): number;
    /** X (red), Y (green) and Z (blue) axes of a transform, `size` units long (default 1). */
    function axes(matrix: Matrix4, size?: number, seconds?: number): number;
    /** Grid on the XZ plane around (x, y, z): halfExtent units each way, a line every step (128 per side at most). */
    function grid(x: number, y: number, z: number, halfExtent: number, step: number, color?: Color.Value,
        seconds?: number): number;
    /** The view volume of another camera. */
    function frustum(camera: Camera3D.Camera, color?: Color.Value, seconds?: number): number;
    /**
     * Normals of a mesh, `length` units long (default 0.25), from each
     * vertex: an Instance uses its transform unless `matrix` is given.
     * Uses inverse transpose under non-uniform scale. Singular/ill-conditioned
     * linear matrices (scaled determinant <=1e-7) throw before queueing.
     * Meshes without normals queue nothing.
     */
    function normals(target: Model3D.Instance | Model3D.Mesh, length?: number, color?: Color.Value,
        seconds?: number, matrix?: Matrix4): number;
    /** Draws the queue with `camera` after every Loop draw; null stops. The camera is retained. */
    function setCamera(camera: Camera3D.Camera | null): void;
    /** Draws the queue now (games without Loop.run()), then ages it by dt seconds; returns segments drawn. */
    function draw(camera: Camera3D.Camera, dt?: number): number;
    /** Ages the queue by dt seconds, removing expired segments already drawn (setCamera() does it each frame). */
    function age(dt: number): void;
    function clear(): void;
    /** Segments queued. */
    function count(): number;
    /** Segments dropped because the queue was full, since the last clear(). */
    function dropped(): number;
    /** Turns the module on or off (off: queueing does nothing and the queue is emptied). Returns whether on. */
    function show(on?: boolean): boolean;
    /** Enables native phase timers (default off, no clock reads). */
    function profile(enabled: boolean): void;
    /** Latest projection/draw. Transform includes packing and VU0 perspective/
     * viewport mapping; clip includes C clipping/mapping of adjusted endpoints.
     * Emit includes callbacks and GS packet construction
     * without a GS wait. Total excludes 2D-view save/restore and age().
     * Reading creates a JS object outside native phase timers.
     * Clock spans must finish before the EE wrap (~14.5 seconds). */
    function stats(): {
        input: number; emitted: number; rejected: number; batches: number;
        transformTicks: number; clipTicks: number; emitTicks: number; totalTicks: number;
        /** EE batches using C for extreme operands (VU0 overflow/denormal behavior differs). */
        fallbackBatches: number;
        ticksPerMs: number; profiling: boolean; backend: "c" | "vu0";
    };
}
