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
 * Voices: sources share up to `setMaxVoices(n)` SPU2 channels (default 24). Each update the
 * sources that want to sound are ranked by `priority`, then loudness (gain * volume, after
 * occlusion), then distance, then creation order: deterministic, with no random stealing. The best
 * hold voices; the others are culled by their `cull` policy: "stop" ends the source, "virtual" keeps it
 * pending and restarts it from the beginning when it ranks again (the SPU2 cannot seek). The default
 * is "virtual" for looping samples and "stop" for one-shots. A source remembers the Sound voice it
 * started and only controls that voice: a channel that ended, was muted or reused by another sample
 * (yours or another Sound user's) is never touched. The SPU2 cannot key a voice off, so a channel
 * taken from a source is reusable once its sample has played one pass; a start that finds no free
 * channel counts as culled and a virtual source retries on the next update.
 *
 * Occlusion (opt-in, `setOcclusion`): a provider (a Collision3D.World, or a function) tells how blocked
 * the line from the listener to a voiced source is; at most `maxQueries` sources are asked per
 * update, the stalest first, and the answer is eased in so the gain never jumps. No provider means
 * no cost; Collision3D is not required by this module.
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
        /** -1000 to 1000, default 0: higher wins a voice before loudness is compared. */
        priority?: number;
        /** What happens when the source loses its voice: default "virtual" for loops, "stop" for one-shots. */
        cull?: "default" | "stop" | "virtual";
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
        /** Wants to sound but holds no voice (a "virtual" source that was outranked or out of range). */
        readonly pending: boolean;
        /** Current smoothed occlusion, 0 (clear) to 1 (blocked). */
        readonly occlusion: number;
        /** Starts at the current levels; may take the voice of a lower-ranked source. Returns the channel, or -1
         * when beyond maxDistance (unless force), no voice is free or it was outranked (a "virtual" source then waits). */
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
    const MAX_VOICES: 24;
    /** Voices Audio3D may hold at once, 1 to 24 (default 24); the rest of the channels stay for other Sound users. */
    function setMaxVoices(count: number): void;
    interface Stats {
        /** Sources that want to sound (playing or pending). */
        sources: number;
        /** Of those, the audible ones competing for voices. */
        candidates: number;
        /** Voices held after the update. */
        voices: number;
        /** Voices taken from a lower-ranked source. */
        stolen: number;
        /** Sources that lost or could not get a voice (any reason). */
        culled: number;
        /** Voices started by the update. */
        started: number;
        /** Occlusion queries asked, and how many failed (the previous value is kept). */
        queries: number; queryErrors: number;
        /** Updates since the stalest occlusion answer of a voiced source. */
        oldestResult: number;
    }
    /** Counters of the last update (the Loop system's, or yours). */
    function stats(): Stats;
    interface OcclusionOptions {
        /** Sources asked per update, 1 to 256 (default 4). */
        maxQueries?: number;
        /** Updates before a source is asked again, 1 to 3600 (default 6). */
        interval?: number;
        /** How much a fully blocked source is attenuated, 0 to 1 (default 0.8). */
        strength?: number;
        /** Easing toward the answer per update, (0, 1] (default 0.25). */
        smoothing?: number;
        /** Collision3D layer mask of the world provider (default all). */
        mask?: number;
    }
    /**
     * Turns occlusion on with a provider, or off with null. A Collision3D.World blocks a source when its
     * ray from the listener meets geometry before the source; a function (x0, y0, z0, x1, y1, z1) returns
     * the blocked fraction 0..1 (an exception or a non-finite answer counts as a failed query).
     * Throws TypeError for anything else (or a World when Collision3D is not built).
     */
    function setOcclusion(provider: Collision3D.World | ((x0: number, y0: number, z0: number, x1: number, y1: number, z1: number) => number) | null,
        options?: OcclusionOptions): void;
    /** Applies every source's levels now (the Loop system does it each frame); returns the counters. */
    function update(): Stats;
}
