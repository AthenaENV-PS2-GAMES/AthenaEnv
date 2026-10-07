/** Native Camera3D controllers. The script configures a rig and feeds input
 * (rotate/zoom); following, orbiting and smoothing run in C every frame in one
 * Loop system (attachLoop()) after Scene3D updates world transforms, or
 * manually with update(dt). A rig keeps its camera alive, also after the
 * camera handle is disposed, and skips frames whose target node is stale.
 * Rigs stay active until dispose(), even when no variable holds them. */
declare namespace CameraRig3D {
    /** Default attachLoop() priority: after Scene3D.attachLoop() (0). */
    const LOOP_PRIORITY: number;
    interface Rig {
        /** null: Orbit uses its fixed centre; Follow stops moving. */
        setTarget(target: Scene3D.Node | null): this;
        /** Exponential smoothing in 1/s for the eye and the look point; 0 is
         * rigid (default). Frame-rate independent. */
        setSharpness(eye: number, look: number): this;
        /** The next update jumps to the goal without smoothing. */
        snap(): this;
        enabled: boolean;
        /** True when it moved the camera. */
        update(dt: number): boolean;
        dispose(): void;
    }
    class Follow implements Rig {
        constructor(camera: Camera3D.Camera, target: Scene3D.Node);
        /** Eye offset; local (default) turns and scales with the target,
         * otherwise it is added to the target's world position. Default 0, 2, 6. */
        setOffset(x: number, y: number, z: number, local?: boolean): this;
        /** Look point in the target's local space. Default 0, 0, 0. */
        setLookOffset(x: number, y: number, z: number): this;
        setTarget(target: Scene3D.Node | null): this;
        setSharpness(eye: number, look: number): this;
        snap(): this;
        enabled: boolean;
        update(dt: number): boolean;
        dispose(): void;
    }
    class Orbit implements Rig {
        /** Without a target, the centre is the fixed point of setCenter(). */
        constructor(camera: Camera3D.Camera, target?: Scene3D.Node | null);
        /** Offset from the target's world position, or the fixed centre. */
        setCenter(x: number, y: number, z: number): this;
        /** Radians: yaw about +Y (0 sits on +Z looking down -Z), pitch up. */
        setAngles(yaw: number, pitch: number): this;
        /** Input deltas, clamped to the limits. */
        rotate(yaw: number, pitch: number): this;
        zoom(delta: number): this;
        /** Pitch within -1.56..1.56 rad; 0 < distanceMin <= distanceMax.
         * Defaults -1.5, 1.5, 0.1, 1e6. */
        setLimits(pitchMin: number, pitchMax: number, distanceMin: number, distanceMax: number): this;
        readonly yaw: number;
        readonly pitch: number;
        /** Default 6. */
        distance: number;
        /** Yaw speed in rad/s. */
        autoRotate: number;
        setTarget(target: Scene3D.Node | null): this;
        setSharpness(eye: number, look: number): this;
        snap(): this;
        enabled: boolean;
        update(dt: number): boolean;
        dispose(): void;
    }
    /** Updates every enabled rig; returns how many moved their camera. */
    function update(dt: number): number;
    /** One native POST_UPDATE system for every rig; idempotent. */
    function attachLoop(priority?: number): void;
    function detachLoop(): boolean;
    function isAttached(): boolean;
}
