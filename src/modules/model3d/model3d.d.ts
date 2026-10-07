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
    /** Bulk setters, one call per frame instead of one per instance: values holds
     * x, y, z for instances[i] at values[3i..3i+2] (it may be longer). Every
     * value is checked finite before any instance changes. Returns the count. */
    function setPositions(instances: Instance[], values: Float32Array): number;
    /** Radians, composed Rz * Ry * Rx as Instance.setRotationEuler(). */
    function setRotationsEuler(instances: Instance[], values: Float32Array): number;
}
