/** Independent cameras; right-handed, facing -Z. Projection has reversed depth.
 * Matrix getters return an owned snapshot, or overwrite and return out. */
declare namespace Camera3D {
    interface Projection { fovYDegrees?: number; aspect?: number; near?: number; far?: number; }
    class Camera {
        /** Defaults: position [0,0,5], target [0,0,0], up [0,1,0], FOV 60, aspect 4/3, near .1, far 300. */
        constructor(options?: Projection);
        setProjection(options: Projection): this;
        setPosition(x: number, y: number, z: number): this;
        lookAt(x: number, y: number, z: number): this;
        setUp(x: number, y: number, z: number): this;
        getView(out?: Matrix4): Matrix4;
        getProjection(out?: Matrix4): Matrix4;
        getViewProjection(out?: Matrix4): Matrix4;
        /** Idempotent; other operations reject a disposed camera. */
        dispose(): void;
    }
}
