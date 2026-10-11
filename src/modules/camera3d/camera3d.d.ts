/** Independent cameras; right-handed, facing -Z. Projection has reversed depth.
 * Matrix getters return an owned snapshot, or overwrite and return out. */
declare namespace Camera3D {
    interface Projection { fovYDegrees?: number; aspect?: number; near?: number; far?: number; }
    /** Screen position in viewport pixels (origin top-left, y down) and the
     * distance in front of the camera along its view axis. */
    interface ScreenPoint { x: number; y: number; depth: number; }
    /** World-space ray: origin (x, y, z) and unit direction (dx, dy, dz). */
    interface Ray { x: number; y: number; z: number; dx: number; dy: number; dz: number; }
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
        /** Viewport in pixels used by worldToScreen/screenToRay; default 640x448.
         * Match the Screen mode, e.g. `camera.setViewport(mode.width, mode.height)`. */
        setViewport(width: number, height: number): this;
        /** Projects a world point to the viewport: null when it is behind the
         * camera. Points outside the screen or beyond near/far still project
         * (check x/y against the viewport and depth against near/far).
         * Optional `out` is reused and returned. */
        worldToScreen<T extends object = ScreenPoint>(x: number, y: number, z: number, out?: T): (T & ScreenPoint) | null;
        /** The ray from the camera through a viewport pixel, e.g. for
         * Scene3D.Scene.raycast(). Optional `out` is reused and returned. */
        screenToRay<T extends object = Ray>(x: number, y: number, out?: T): T & Ray;
        /** Idempotent; other operations reject a disposed camera. */
        dispose(): void;
    }
}
