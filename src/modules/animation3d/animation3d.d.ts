/** Keyframe clips sampled in native code and applied to Scene3D nodes. The
 * script starts and stops players; poses are not computed in JavaScript.
 * Players advance in one Loop system (attachLoop()) that runs before Scene3D
 * systems at the default priorities, or manually with advance(dt). */
declare namespace Animation3D {
    /** Default attachLoop() priority: before Scene3D.attachLoop() (0). */
    const LOOP_PRIORITY: number;
    interface Track {
        /** Index into the player's nodes; default 0. */
        target?: number;
        path: "position" | "rotation" | "scale" | "weights";
        /** Seconds, >= 0 and strictly increasing. */
        times: Float32Array;
        /** 3 floats per key (position, scale), 4 (rotation xyzw; normalized,
         * slerp on the short arc) or 1..8 (morph target weights, see
         * Scene3D.Node.setWeights(); values.length / times.length per key). */
        values: Float32Array;
        /** Default "linear". "cubic" is glTF CUBICSPLINE: Hermite between
         * keys using inTangents/outTangents (rotations normalized after). */
        interpolation?: "linear" | "step" | "cubic";
        /** "cubic" only: tangents per key, laid out like values, in value
         * units per second. */
        inTangents?: Float32Array;
        outTangents?: Float32Array;
    }
    /** Tracks are copied; the arrays can be reused afterwards. */
    class Clip {
        constructor(tracks: Track[]);
        /** Last key time of all tracks, in seconds. */
        readonly duration: number;
        /** Nodes a player needs: highest target + 1. */
        readonly targetCount: number;
        dispose(): void;
    }
    /** Binds a clip to nodes (retained, also after their handles are
     * disposed). Created stopped at time 0, speed 1, not looping. Players
     * stay alive until dispose(), even when no variable holds them. */
    class Player {
        constructor(clip: Clip, nodes: Scene3D.Node[]);
        play(): this;
        pause(): this;
        /** Pauses and rewinds (to the end when speed is negative). */
        stop(): this;
        /** Seconds; setting it seeks and applies the pose. */
        time: number;
        /** Negative plays backwards. */
        speed: number;
        loop: boolean;
        readonly playing: boolean;
        /** Advances by dt * speed seconds; true when it just reached the end. */
        advance(dt: number): boolean;
        dispose(): void;
    }
    /** Advances every playing player; returns how many reached their end. */
    function advance(dt: number): number;
    /** One native POST_UPDATE system for every player; idempotent. */
    function attachLoop(priority?: number): void;
    function detachLoop(): boolean;
    function isAttached(): boolean;
}
