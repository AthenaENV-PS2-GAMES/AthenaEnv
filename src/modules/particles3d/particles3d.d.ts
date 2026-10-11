/** Native 3D particles drawn as camera-facing quads (billboards) by VU1:
 * emission and integration in C, depth tested against the 3D scene without
 * writing depth, so transparent particles do not hide each other. Draw after
 * the opaque scene. Screen zbuffering is required. Emitters advance in one
 * Loop system (attachLoop(), POST_UPDATE) or with update(dt). */
declare namespace Particles3D {
    const MAX_PARTICLES: number;
    const LOOP_PRIORITY: number;
    /** A number, or [min, max] picked per particle ([start, end] for size). */
    type Range = number | [number, number];
    type Vector3 = [number, number, number];
    interface Options {
        /** Pool size, 1..MAX_PARTICLES; default 256. */
        capacity?: number;
        /** Particles per second while active; default 0 (bursts only). */
        rate?: number;
        /** Seconds; default 1. */
        life?: Range;
        speed?: Range;
        /** Cone axis (normalized); default [0, 1, 0]. */
        direction?: Vector3;
        /** Cone half angle in radians, 0..PI; default 0. */
        spread?: number;
        gravity?: Vector3;
        /** velocity *= 1 / (1 + drag * dt). */
        drag?: number;
        /** Quad side in world units, [start, end] over life; default 0.5. */
        size?: Range;
        /** Color.new() value or [start, end]; alpha 0..128. */
        color?: number | [number, number];
        /** Spawn box around the position. */
        area?: Vector3;
        /** [u1, v1, u2, v2] in texels; default the whole image. */
        rect?: [number, number, number, number];
        seed?: number;
    }
    class Emitter {
        constructor(image: Image, options?: Options);
        configure(options: Options): this;
        setPosition(x: number, y: number, z: number): this;
        emit(count: number): number;
        clear(): this;
        update(dt: number): this;
        /** Draws what camera sees (near/far tested on the EE) and returns how
         * many particles were sent. Throws without a z-buffer. */
        draw(camera: Camera3D.Camera): number;
        readonly count: number;
        active: boolean;
        dispose(): void;
    }
    function update(dt: number): void;
    function attachLoop(priority?: number): void;
    function detachLoop(): boolean;
    function isAttached(): boolean;
}
