/** Immutable mesh streams. Geometry and material descriptors are copied;
 * instances/batches retain native resources, including textures. */
declare namespace Model3D {
    const MAX_VERTICES: number;
    /** Joint indices must be below MAX_JOINTS (256); the VU1 path accepts 24 joints. */
    const MAX_JOINTS: number;
    /** At most MAX_TARGETS (8) morph targets per mesh. */
    const MAX_TARGETS: number;
    /** UV components must be finite with |u|,|v| <= UV_LIMIT (16): tiled UVs
     * use repeat addressing within the GS texel precision. */
    const UV_LIMIT: number;
    const UNLIT: 0; const DIFFUSE: 1;
    class Texture {
        private constructor();
        static readonly NEAREST: 0; static readonly LINEAR: 1;
        /** Addressing per axis: CLAMP (to edge, the default), REPEAT on both
         * axes, or REPEAT_U / REPEAT_V on one. */
        static readonly CLAMP: 0; static readonly REPEAT_U: 1; static readonly REPEAT_V: 2; static readonly REPEAT: 3;
        /** Copies 0xAABBGGRR pixels; alpha (0..255) matters only to alphaCutoff materials. Power-of-two sizes 1..512;
         * no mipmaps. Honors subarray(); main thread only. */
        static fromPixels(pixels: {width: number; height: number; pixels: Uint32Array; filter?: 0 | 1; wrap?: 0 | 1 | 2 | 3}): Texture;
        /** Synchronous decoding. RGB/RGBA, 16-bit and 4/8-bit palette images
         * (canonical 32-bit CPU copy). VRAM uses lossless T4/T8 for <=16/256
         * distinct GS RGBA colors when texture+CLUT is smaller than CT32.
         * Errors name the path and the reason. */
        static load(path: string, filter?: 0 | 1, wrap?: 0 | 1 | 2 | 3): Texture;
        /** load() from PNG/JPEG/BMP bytes (ArrayBuffer/Uint8Array, borrowed for the
         * call, subarrays honored): no file I/O, so a Thread read job can feed it. */
        static loadMemory(bytes: ArrayBuffer | Uint8Array, filter?: 0 | 1, wrap?: 0 | 1 | 2 | 3): Texture;
        readonly width: number; readonly height: number; readonly wrap: 0 | 1 | 2 | 3;
        /** Makes the texture resident in VRAM now (one synchronous upload and
         * GS wait), e.g. on a loading screen, instead of at the first draw.
         * Throws when VRAM is full. */
        upload(): this;
        /** Existing meshes retain the texture. Final native release defers VRAM
         * and pixel cleanup until the GS has finished reading them. */
        dispose(): void;
    }
    interface Material {
        /** Defaults to UNLIT. DIFFUSE uses world ambient/directional lights. */
        shading?: 0 | 1;
        /** Four finite linear RGBA values in [0,1], multiplied by vertex colors
         * and stored as RGBA8. Defaults to white; alpha is used by alphaCutoff. */
        baseColor?: Float32Array;
        texture?: Texture;
        /** Alpha mask (glTF alphaMode MASK): pixels whose alpha (texture
         * alpha times vertex alpha) is below the cutoff, in [0,1], are
         * discarded by the GS alpha test; the rest stay opaque. */
        alphaCutoff?: number;
    }
    interface Geometry {
        positions: Float32Array; colors?: Float32Array;
        /** Triangle-list corners; copied into compact batches when storage is
         * smaller. The original triangle order and vertexCount are preserved. */
        indices?: Uint32Array;
        /** One nonzero xyz normal per source vertex; normalized during copy.
         * Missing DIFFUSE normals are generated per face, before expansion. */
        normals?: Float32Array;
        /** One finite uv pair per source vertex, |u|,|v| <= UV_LIMIT. Origin
         * top-left; outside [0,1] the texture's wrap mode applies. */
        texcoords?: Float32Array;
        /** Four joint indices and four nonnegative finite weights per source
         * vertex, supplied together. Each vertex needs a positive weight;
         * weights are normalized during copy.
         * Skin data needs a skin/joint palette to deform (e.g. a loaded glTF node). */
        joints?: Uint16Array;
        weights?: Float32Array;
        /** Concatenated xyz position deltas, one complete source-vertex block
         * per target (1..MAX_TARGETS). Scene3D.Node.setWeights controls the blend. */
        targetPositions?: Float32Array;
        /** Matching concatenated xyz normal deltas; requires base normals. */
        targetNormals?: Float32Array;
        material?: Material;
    }
    class Mesh {
        private constructor();
        /** xyz positions, optional normalized rgba, optional triangle-list indices. Honors subarray(). */
        static fromGeometry(geometry: Geometry): Mesh;
        /** Expanded triangle vertex count, at most MAX_VERTICES. */
        readonly vertexCount: number;
        /** Model-space AABB: minX, minY, minZ, maxX, maxY, maxZ. */
        getBounds(): number[]; getBounds(out: Float32Array): Float32Array;
        createInstance(): Instance;
        /** Drops this handle; existing instances retain the native mesh. */
        dispose(): void;
    }
    class Instance {
        private constructor();
        setPosition(x: number, y: number, z: number): this;
        setScale(x: number, y: number, z: number): this;
        /** Radians, XYZ local rotations composed Rz * Ry * Rx. */
        setRotationEuler(x: number, y: number, z: number): this;
        setRotationQuaternion(x: number, y: number, z: number, w: number): this;
        /** Owned snapshot, or fills and returns out; never a borrowed matrix. */
        getTransform(out?: Matrix4): Matrix4;
        /** Local TRS as set (rotation normalized, xyzw): a new Array, or out
         * filled and returned (no allocation per frame). */
        getPosition(): number[]; getPosition(out: Float32Array): Float32Array;
        getRotation(): number[]; getRotation(out: Float32Array): Float32Array;
        getScale(): number[]; getScale(out: Float32Array): Float32Array;
        dispose(): void;
    }
    /** Synchronous static OBJ/glTF/GLB loading; see docs/3D.md for the supported subset.
     * Errors name the file and the exact unsupported feature. */
    function load(path: string, material?: Material): Mesh;
    /** Self-contained GLB, at most 16 MiB. Borrows bytes for the call and
     * copies all native data; honors Uint8Array.subarray(), including unaligned
     * offsets. origin is a nonempty diagnostic name, not an I/O resolver.
     * URI buffers and external images are rejected. No upload; synchronous
     * parse/construction on the main thread, with the same static-mesh subset. */
    function loadMemory(bytes: ArrayBuffer | Uint8Array, origin: string, material?: Material): Mesh;
    type LoaderPhase = 'copy' | 'parse' | 'dependencies' | 'validate' | 'build' | 'done';
    /** Resumable loadMemory() of one static GLB mesh. The first steps ('copy',
     * 256 KiB each) snapshot the bytes into memory the loader owns; do not
     * modify the source before `phase` leaves 'copy'. Then the parse, the
     * glTF validation and the primitive build (texture decode is one unit;
     * vertex/index reads and the mesh stages are small slices). Dispose to
     * abandon; unreferenced loaders are freed by GC. */
    class Loader {
        private constructor();
        /** Phase names by native phase index (the order of phaseStatsInto). */
        static readonly PHASES: readonly string[];
        /** Runs up to `units` (1..64, default 1) consecutive units of the same
         * phase. false while work remains, true once finished. Throws the load
         * error, and keeps throwing it; then dispose(). */
        step(units?: number): boolean;
        /** Runs at least one unit, then more until `budgetMs` (0..1000, default 0 =
         * one unit) has elapsed: a single call for a time slice, timed by the
         * native loader. Same results as step(); false while work remains, true
         * once finished; lastUnits is the number of units that ran. */
        run(budgetMs?: number): boolean;
        /** Units, total time and largest single unit per phase so far. */
        /** Same numbers as `phaseStats`, written into a Float64Array (units, totalMs,
         * maxMs at index 3 * phase index; at least 3 * PHASES.length elements): no allocation. */
        phaseStatsInto(out: Float64Array): void;
        readonly phaseStats: { [phase: string]: { units: number; totalMs: number; maxMs: number } };
        /** The Mesh, after step() returned true. Spends the loader. */
        finish(): Mesh;
        dispose(): void;
        readonly phase: LoaderPhase | 'disposed';
        readonly lastPhase: LoaderPhase | 'disposed';
        /** Native steps the last step() call ran (at most `units`). */
        readonly lastUnits: number;
        /** Files named by relative URI (external buffers and images), listed after
         * the parse step when started with `{ dependencies: true }`; else empty.
         * Read each one yourself (the loader does no I/O) and give it to provide(). */
        readonly dependencies: { uri: string; image: boolean; provided: boolean }[];
        /** Hands over the bytes of dependencies[index] (ArrayBuffer/Uint8Array,
         * 1 byte to 16 MiB, once). They are used, not copied: do not modify the
         * buffer until finish() or dispose(). While files are missing step()
         * returns false and does nothing (phase 'dependencies'). Absolute, scheme,
         * drive and ".." URIs are rejected, at most 64 files. */
        provide(index: number, bytes: ArrayBuffer | Uint8Array): void;
        readonly unitsDone: number;
        readonly unitsTotal: number;
    }
    /** Starts a resumable loadMemory() (same rules and errors, found while stepping). *
     * `options.borrow: true` uses the bytes in place (no 'copy' phase and no snapshot
     * RAM) when they are 4-byte aligned, otherwise the snapshot is made as usual; the
     * buffer is pinned and must not be modified until finish() or dispose(). */
    function beginMemory(bytes: ArrayBuffer | Uint8Array, origin: string, material?: Material, options?: { dependencies?: boolean; borrow?: boolean }): Loader;
    /** Bulk setters, one call per frame instead of one per instance: values holds
     * x, y, z for instances[i] at values[3i..3i+2] (it may be longer). Every
     * value is checked finite before any instance changes. Returns the count. */
    function setPositions(instances: Instance[], values: Float32Array): number;
    /** Radians, composed Rz * Ry * Rx as Instance.setRotationEuler(). */
    function setRotationsEuler(instances: Instance[], values: Float32Array): number;
}
