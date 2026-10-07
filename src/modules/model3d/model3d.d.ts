/** Immutable static meshes. Geometry and material descriptors are copied;
 * instances/batches retain native resources, including textures. */
declare namespace Model3D {
    const MAX_VERTICES: number;
    const UNLIT: 0; const DIFFUSE: 1;
    class Texture {
        private constructor();
        static readonly NEAREST: 0; static readonly LINEAR: 1;
        /** Copies 0xAABBGGRR pixels, alpha ignored. Power-of-two sizes 1..512;
         * clamp-to-edge, no mipmaps. Honors subarray(); main thread only. */
        static fromPixels(pixels: {width: number; height: number; pixels: Uint32Array; filter?: 0 | 1}): Texture;
        /** Synchronous RGB/RGBA image decoding. Palette images unsupported. */
        static load(path: string, filter?: 0 | 1): Texture;
        readonly width: number; readonly height: number;
        /** Existing meshes retain the texture. Final native release waits GS. */
        dispose(): void;
    }
    interface Material {
        /** Defaults to UNLIT. DIFFUSE uses world ambient/directional lights. */
        shading?: 0 | 1;
        /** Four finite linear RGBA values in [0,1], multiplied by vertex colors
         * and stored as RGBA8. Defaults to white. Alpha is opaque in this pass. */
        baseColor?: Float32Array;
        texture?: Texture;
    }
    interface Geometry {
        positions: Float32Array; colors?: Float32Array; indices?: Uint32Array;
        /** One nonzero xyz normal per source vertex; normalized during copy.
         * Missing DIFFUSE normals are generated per face, before expansion. */
        normals?: Float32Array;
        /** One finite uv pair in [0,1] per source vertex. Origin top-left. */
        texcoords?: Float32Array;
        material?: Material;
    }
    class Mesh {
        private constructor();
        /** xyz positions, optional normalized rgba, optional triangle-list indices. Honors subarray(). */
        static fromGeometry(geometry: Geometry): Mesh;
        /** Expanded triangle vertex count, at most MAX_VERTICES. */
        readonly vertexCount: number;
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
        dispose(): void;
    }
    /** Synchronous static OBJ/glTF/GLB loading; see docs/3D.md for the supported subset. */
    function load(path: string, material?: Material): Mesh;
    /** Bulk setters, one call per frame instead of one per instance: values holds
     * x, y, z for instances[i] at values[3i..3i+2] (it may be longer). Every
     * value is checked finite before any instance changes. Returns the count. */
    function setPositions(instances: Instance[], values: Float32Array): number;
    /** Radians, composed Rz * Ry * Rx as Instance.setRotationEuler(). */
    function setRotationsEuler(instances: Instance[], values: Float32Array): number;
}
