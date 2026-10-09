/**
 * Positional sound: footsteps, engines, rivers, monsters off screen.
 *
 * A Source plays a Sound.Sfx at a world position (or on a Scene3D node);
 * the listener is usually the camera. Every frame a Loop system (after the
 * scene updates) computes, in C, each playing source's volume from its
 * distance (1 up to minDistance, 0 from maxDistance; "inverse" falls like
 * minDistance / distance, "linear" in a straight line) and its stereo pan
 * from the listener's right axis, and applies them to the SPU2 voice. The
 * sample's own volume and pan are left for its other plays.
 *
 * The SPU2 mixes stereo only: sounds behind the listener are not muffled
 * or told apart from those in front. Not in the default build:
 * `node tools/modules.js configure --modules=audio3d,...`
 *
 * Example:
 * ```js
 * Audio3D.setListener(camera);
 * const river = new Audio3D.Source(waterLoop, { x: 10, y: 0, z: -4, minDistance: 2, maxDistance: 25 });
 * river.play();
 * const growl = new Audio3D.Source(growlSfx, { node: monster, maxDistance: 40 });
 * if (aggro) growl.play();
 * ```
 */
declare namespace Audio3D {
    interface SourceOptions {
        x?: number; y?: number; z?: number;
        /** Follow this node's world position (of the last scene update); null stops. */
        node?: Scene3D.Node | null;
        /** Full volume up to here (default 1). */
        minDistance?: number;
        /** Silent from here (default 30). */
        maxDistance?: number;
        rolloff?: "inverse" | "linear";
        /** At full gain, 0-100 (default 100). */
        volume?: number;
        /** 0 (mono) to 1 (hard left/right); default 0.8. */
        panStrength?: number;
    }
    class Source {
        /** Keeps the Sfx alive; a freed Sfx stops the source. */
        constructor(sfx: Sound.Sfx, options?: SourceOptions);
        readonly playing: boolean;
        /** Voice in use, -1 when not playing. */
        readonly channel: number;
        /** Levels applied last. */
        readonly volume: number;
        readonly pan: number;
        /** Starts at the current levels; returns the channel, or -1 when beyond maxDistance (unless force) or no voice is free. */
        play(options?: { force?: boolean }): number;
        stop(): this;
        setPosition(x: number, y: number, z: number): this;
        configure(options: SourceOptions): this;
        /** Stops and releases the source. */
        dispose(): void;
    }
    /** Listens from the camera (retained; null keeps the last position). */
    function setListener(camera: Camera3D.Camera | null): void;
    /** Listens from a point with a right-hand axis. */
    function setListener(x: number, y: number, z: number, rightX: number, rightY: number, rightZ: number): void;
    /** Levels a source at (x, y, z) would get now, without playing. */
    function levels(x: number, y: number, z: number, options?: SourceOptions): { volume: number; pan: number; distance: number };
    /** Applies every source's levels now (the Loop system does it each frame). */
    function update(): void;
}
