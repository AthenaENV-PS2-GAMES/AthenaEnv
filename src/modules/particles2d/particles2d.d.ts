/** Native 2D particles: emission, integration and drawing in C. The script
 * configures an emitter, moves it and calls draw() once per frame; nothing per
 * particle runs in JavaScript. World units through the 2D view (Camera2D),
 * +Y down. Emitters advance in one Loop system (attachLoop(), POST_UPDATE, so
 * they spawn where the game left them) or with update(dt). */
declare namespace Particles2D {
    const MAX_PARTICLES: number;
    const LOOP_PRIORITY: number;
    /** A number, or [min, max] picked per particle ([start, end] for size). */
    type Range = number | [number, number];
    interface Options {
        /** Pool size, 1..MAX_PARTICLES; default 256. */
        capacity?: number;
        /** Particles per second while active; default 0 (bursts only). */
        rate?: number;
        /** Seconds; default 1. */
        life?: Range;
        speed?: Range;
        /** Direction in radians (0 = +X, PI/2 = down) and total spread. */
        angle?: number;
        spread?: number;
        /** [x, y] in units per second squared. */
        gravity?: [number, number];
        /** velocity *= 1 / (1 + drag * dt). */
        drag?: number;
        /** [start, end] over life, square side in units; default 8. */
        size?: Range;
        /** Color.new() value or [start, end]; alpha 0..128. Default white. */
        color?: number | [number, number];
        /** Initial angle and angular speed, radians. Any non-zero value draws
         * rotated quads (two triangles) instead of GS sprites. */
        rotation?: Range;
        spin?: Range;
        /** [width, height] of the spawn rectangle around the position. */
        area?: [number, number];
        /** [u1, v1, u2, v2] in texels; default the whole image. */
        rect?: [number, number, number, number];
        /** Random seed; same seed, same particles. */
        seed?: number;
    }
    class Emitter {
        /** The image is held by the emitter; a freed image draws nothing. */
        constructor(image: Image, options?: Options);
        /** Partial changes; live particles keep their state. */
        configure(options: Options): this;
        setPosition(x: number, y: number): this;
        /** Spawns up to count particles now; returns how many fit. */
        emit(count: number): number;
        clear(): this;
        update(dt: number): this;
        /** One batch per call, oldest particles first. */
        draw(): this;
        readonly count: number;
        /** Rate emission on or off; live particles continue. */
        active: boolean;
        dispose(): void;
    }
    function update(dt: number): void;
    function attachLoop(priority?: number): void;
    function detachLoop(): boolean;
    function isAttached(): boolean;
}
