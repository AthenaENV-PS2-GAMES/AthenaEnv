/**
 * Geometry built in C and turned into Model3D meshes: procedural scenery,
 * terrain from heightmaps, prototypes without model files, and static
 * batching (many props merged into few meshes, so fewer draws).
 *
 * A Builder is a pen: `color()`, `transform()` and `uvRect()`/`uvTile()`
 * apply to what is added next. Triangles are counter-clockwise seen from
 * the front (the side `Render3D.CULL_BACK` keeps); every shape has normals
 * and 0..1 UVs mapped into the UV rectangle. `build()` returns as many
 * meshes as the Model3D limits need (65532 indices and vertices each).
 *
 * Not in the default build: `node tools/modules.js configure --modules=meshbuilder,...`
 *
 * Example:
 * ```js
 * const b = new MeshBuilder.Builder();
 * b.color(0.4, 0.3, 0.2).box(-5, -0.1, -5, 5, 0, 5);                  // floor
 * for (const rock of rocks) b.transform(rock.matrix).merge(rockMesh);   // static batching
 * b.transform(null).color(1, 1, 1).uvTile(3, 4, 4, 0.02).cylinder(0, 0, 0, 0.3, 2);
 * const meshes = b.build({ shading: Model3D.DIFFUSE, texture: atlas });
 * b.dispose();
 * ```
 */
declare namespace MeshBuilder {
    class Builder {
        constructor();
        readonly vertexCount: number;
        readonly triangleCount: number;
        /** Meshes build() will make. */
        readonly partCount: number;
        /** Linear color in [0, 1] of the next vertices (default white). */
        color(r: number, g: number, b: number, a?: number): this;
        /** Transform of the next positions and normals; null for none. */
        transform(matrix: Matrix4 | null): this;
        /** Rectangle the next shapes' 0..1 UVs map into. */
        uvRect(u0: number, v0: number, u1: number, v1: number): this;
        /** uvRect() of tile `index` of an atlas of columns x rows (row by row from the top-left), shrunk by inset (tile fraction, against bleeding). */
        uvTile(index: number, columns: number, rows: number, inset?: number): this;
        /** One vertex; returns its index for triangle(). Normal defaults to +Y. */
        vertex(x: number, y: number, z: number, nx?: number, ny?: number, nz?: number, u?: number, v?: number): number;
        /** Counter-clockwise from the front. */
        triangle(a: number, b: number, c: number): this;
        /** Four corners counter-clockwise from the front (two triangles, flat normal). */
        quad(x0: number, y0: number, z0: number, x1: number, y1: number, z1: number,
            x2: number, y2: number, z2: number, x3: number, y3: number, z3: number): this;
        box(minX: number, minY: number, minZ: number, maxX: number, maxY: number, maxZ: number): this;
        /** segments 3-256 (default 16), rings 2-256 (default 8). */
        sphere(x: number, y: number, z: number, radius: number, segments?: number, rings?: number): this;
        /** Along +Y from the base centre; caps default true. */
        cylinder(x: number, y: number, z: number, radius: number, height: number, segments?: number, caps?: boolean): this;
        /** XZ plane facing +Y, centred, divided into divisionsX x divisionsZ cells. */
        plane(x: number, y: number, z: number, width: number, depth: number, divisionsX?: number, divisionsZ?: number): this;
        /**
         * Terrain from width x depth heights (row-major: x, then z), 2 to
         * 1024 each way: samples `cell` apart (default 1), heights times
         * `scale` (default 1), first sample at (x, y, z). low/high
         * ([r, g, b, a?]) color the vertices by height instead of the pen color.
         */
        heightmap(heights: Float32Array, width: number, depth: number, options?: {
            cell?: number; scale?: number; x?: number; y?: number; z?: number;
            low?: number[]; high?: number[];
        }): this;
        /** Copies a mesh's triangles (colors, normals, UVs) under the transform; an Instance adds its own transform. */
        merge(source: Model3D.Mesh | Model3D.Instance): this;
        /** Drops the geometry, keeping color, transform and UV rectangle. */
        clear(): this;
        /** New meshes with material (normals for DIFFUSE, UVs with a texture). Empty geometry gives []. */
        build(material?: Model3D.Material): Model3D.Mesh[];
        dispose(): void;
    }
}
