/**
 * Block worlds in C, for Minecraft-like games, destructible terrain and
 * voxel-art levels; the game logic stays in JavaScript.
 *
 * One byte per block (0 is air, 1-255 are types described by
 * `setMaterial`). Blocks live in native memory, not in the JavaScript heap.
 * The world is split into chunks (`chunk` blocks per edge, 4 to 32): editing
 * marks the chunk (and neighbours at its borders) dirty, and `rebuild()`
 * meshes dirty chunks within a time budget, nearest to a focus point first.
 * `draw()` culls chunks by distance and frustum and draws them through
 * Render3D in one shared pass.
 *
 * Meshing "naive" (default) emits every visible face with per-vertex
 * ambient occlusion and atlas tiles; "greedy" merges equal coplanar faces
 * into rectangles (far fewer triangles, colors only, no AO). Faces are
 * shaded by direction (`bakedLight`, UNLIT, cheapest) or lit by Render3D
 * (`shading: Model3D.DIFFUSE`).
 *
 * Coordinates are in blocks: block (x, y, z) fills [x, x+1) x [y, y+1) x
 * [z, z+1). Chunks that surround the camera are clipped in C on the EE
 * (Render3D `cpuClipObjects`): for first person, prefer `chunk: 8`.
 *
 * Not in the default build: `node tools/modules.js configure --modules=voxel,...`
 *
 * Example:
 * ```js
 * const world = new Voxel.World({ size: [128, 64, 128], chunk: 16 });
 * world.setMaterial(1, { color: { top: [0.3, 0.7, 0.2], side: [0.45, 0.35, 0.2], bottom: [0.45, 0.35, 0.2] } });
 * world.setMaterial(2, { color: [0.45, 0.33, 0.2] });     // dirt
 * world.setMaterial(3, { color: [0.5, 0.5, 0.5] });       // stone
 * world.generate({ seed: 7, caves: 0.4 });
 * world.rebuild(0);                                      // everything, once, on a loading screen
 *
 * // Each frame:
 * const hit = world.raycast(camera.screenToRay(320, 224), 6);
 * if (hit && digPressed) world.set(hit.x, hit.y, hit.z, Voxel.AIR);
 * world.rebuild(4, player.x, player.y, player.z);
 * world.draw(camera, Render3D.CULL_BACK, undefined, null, 64);
 * ```
 */
declare namespace Voxel {
    const AIR: 0;
    /** Blocks per world (size x * y * z), 16 Mi. */
    const MAX_BLOCKS: number;
    /** [r, g, b, a?], linear 0..1. */
    type RGBA = [number, number, number] | [number, number, number, number];
    interface Material {
        /** Blocks movement and rays and hides neighbouring faces (default true). */
        solid?: boolean;
        /** Drawn (default true); a solid invisible type is a barrier. */
        visible?: boolean;
        /** One color, or per face group. */
        color?: RGBA | { top?: RGBA; bottom?: RGBA; side?: RGBA };
        /** Atlas tile index (row by row from the top-left), or per face group; -1 for none. */
        tile?: number | { top?: number; bottom?: number; side?: number };
    }
    interface Style {
        meshing?: "naive" | "greedy";
        /** 0 off to 1 strong (default 0.5); naive meshing only. */
        ambientOcclusion?: number;
        /** Shade faces by direction (default true). */
        bakedLight?: boolean;
        /** Model3D.UNLIT (default) or Model3D.DIFFUSE (adds normals; lit by Render3D lights). */
        shading?: 0 | 1;
        /** Texture atlas of square tiles of tileSize pixels (naive meshing only). */
        atlas?: Model3D.Texture | null;
        tileSize?: number;
    }
    interface Terrain {
        seed?: number;
        /** Default 45% and 20% of the world height. */
        baseHeight?: number;
        amplitude?: number;
        /** Cycles per block (default 1/48). */
        frequency?: number;
        /** fBm octaves, 1-8 (default 4). */
        octaves?: number;
        /** Block types of the surface, the layer under it and the rest (defaults 1, 2, 3). */
        top?: number; filler?: number; stone?: number;
        /** Filler layer depth (default 3). */
        fillerDepth?: number;
        /** Tunnel caves, 0 (none, default) to <1 (wider). */
        caves?: number;
        caveFrequency?: number;
        /** A water type fills air below waterLevel above the ground. */
        water?: number; waterLevel?: number;
    }
    interface Hit {
        /** The block hit. */
        x: number; y: number; z: number;
        /** Face normal: a new block goes at (x + nx, y + ny, z + nz). Zero when the ray starts inside. */
        nx: number; ny: number; nz: number;
        type: number;
        distance: number;
        /** Hit point. */
        px: number; py: number; pz: number;
    }
    interface Move {
        /** Movement applied per axis. */
        dx: number; dy: number; dz: number;
        hitX: boolean; hitY: boolean; hitZ: boolean;
        /** Stopped while moving down. */
        onGround: boolean;
    }
    interface Stats {
        chunks: number; meshedChunks: number; meshes: number;
        /** Quads in the meshes. */
        faces: number;
        /** Estimated mesh memory. */
        meshBytes: number;
        /** Duration of the last rebuild() that did work, and its face generation and Model3D mesh creation parts. */
        lastRebuildMs: number; lastMeshMs: number; lastBuildMs: number;
    }
    class World {
        /** size: blocks per axis (1-4096, MAX_BLOCKS in total); chunk: 4-32 (default 16). */
        constructor(options: { size: [number, number, number]; chunk?: number });
        readonly sizeX: number; readonly sizeY: number; readonly sizeZ: number;
        readonly chunkSize: number;
        /** Chunks waiting for rebuild(). */
        readonly dirtyCount: number;
        setMaterial(type: number, material: Material): this;
        setStyle(style: Style): this;
        /** Air (0) outside the world. */
        get(x: number, y: number, z: number): number;
        /** Returns whether the block changed; throws outside the world. */
        set(x: number, y: number, z: number, type: number): boolean;
        /** Fills a box (inclusive corners, clamped); returns blocks changed. */
        fill(x0: number, y0: number, z0: number, x1: number, y1: number, z1: number, type: number): number;
        /** A region's blocks, x fastest, then z, then y (for saving). */
        read(x: number, y: number, z: number, sizeX: number, sizeY: number, sizeZ: number): Uint8Array;
        write(x: number, y: number, z: number, sizeX: number, sizeY: number, sizeZ: number, data: Uint8Array): this;
        /** Replaces the blocks with generated terrain. */
        generate(terrain?: Terrain): this;
        /** y above the topmost solid block of a column (0 when empty). */
        surface(x: number, z: number): number;
        /**
         * Meshes dirty chunks until budgetMs passes (default 4; at least
         * one chunk; 0 rebuilds all), nearest to (x, y, z) first when given.
         * Returns chunks rebuilt.
         */
        rebuild(budgetMs?: number, x?: number, y?: number, z?: number): number;
        stats<T extends object = Stats>(out?: T): T & Stats;
        /** DDA through solid blocks within maxDistance (default 8); a Camera3D.Ray works. */
        raycast<T extends object = Hit>(ray: Camera3D.Ray, maxDistance?: number, out?: T): (T & Hit) | null;
        /** Moves a box against solid blocks (Y, then X, then Z); returns what it could move. */
        moveBox<T extends object = Move>(minX: number, minY: number, minZ: number, maxX: number, maxY: number, maxZ: number,
            dx: number, dy: number, dz: number, out?: T): T & Move;
        /** True when the box overlaps a solid block. */
        boxSolid(minX: number, minY: number, minZ: number, maxX: number, maxY: number, maxZ: number): boolean;
        /**
         * Draws meshed chunks within `distance` (default 0: no limit) that
         * the camera sees. `stats` as in Render3D.draw(): reused, or null
         * (returns undefined); the totals feed Render3D.frameStats().
         */
        draw<T extends object = Render3D.Stats>(camera: Camera3D.Camera, cullMode?: Render3D.CullMode, lights?: Lights.Set,
            stats?: T, distance?: number): T & Render3D.Stats;
        draw(camera: Camera3D.Camera, cullMode: Render3D.CullMode | undefined, lights: Lights.Set | undefined,
            stats: null, distance?: number): undefined;
        /** Frees every chunk mesh (they rebuild on demand). */
        clearMeshes(): this;
        dispose(): void;
    }
}
