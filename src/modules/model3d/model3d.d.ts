/** Stage 1: immutable static triangle meshes with colors; native resources are retained by instances/batches.
 * Geometry is copied. No mutation/freeze, textures, skins or async loader in this stage. */
declare namespace Model3D {
    const MAX_VERTICES: number;
    interface Geometry { positions: Float32Array; colors?: Float32Array; indices?: Uint32Array; }
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
    function load(path: string): Mesh;
}
