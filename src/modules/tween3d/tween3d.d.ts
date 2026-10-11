/** Tweens of native 3D objects advanced in C, with the semantics of Tween:
 * start values are read when the tween starts (after its delay), the last
 * frame sets the exact end values, repeat adds cycles and yoyo runs every
 * other cycle backwards. No JavaScript runs per frame: tweens advance in one
 * native Loop system (attachLoop(), PRE_UPDATE like Tween) or with advance(dt).
 * For plain JavaScript objects keep using Tween. */
declare namespace Tween3D {
    const LOOP_PRIORITY: number;
    type Vector3 = ArrayLike<number>;
    interface Props {
        /** Node / Instance local position, Camera eye. */
        position?: Vector3;
        /** Node / Instance only. */
        scale?: Vector3;
        /** Node / Instance only: Euler goal in radians (Rz·Ry·Rx), reached by
         * slerp on the short arc; overshooting curves extrapolate the arc. */
        rotation?: Vector3;
        /** Camera only: look target. */
        target?: Vector3;
    }
    interface Options {
        /** Curve name, short or long (`"outBack"`, `"easeOutBack"`); default `"outQuad"`. */
        ease?: string;
        delay?: number;
        /** Extra cycles: an integer or Infinity. */
        repeat?: number;
        yoyo?: boolean;
        /** Kills the other tweens of the same target when this one starts. */
        overwrite?: boolean;
    }
    /** Awaitable: resolves with true when the tween completes, false when killed. */
    interface Handle extends PromiseLike<boolean> {
        readonly active: boolean;
        readonly paused: boolean;
        /** Progress of the current cycle, 0..1. */
        readonly progress: number;
        readonly finished: Promise<boolean>;
        pause(): this;
        resume(): this;
        /** Stops it; with complete, sets the end values first. */
        kill(complete?: boolean): void;
    }
    type Target = Scene3D.Node | Model3D.Instance | Camera3D.Camera;
    /** The tween retains its target (cameras too, after dispose). */
    function to(target: Target, props: Props, duration: number, options?: Options): Handle;
    function killTweensOf(target: Target, complete?: boolean): number;
    function isTweening(target: Target): boolean;
    /** Advances every tween; returns how many ended. */
    function advance(dt: number): number;
    function attachLoop(priority?: number): void;
    function detachLoop(): boolean;
    function isAttached(): boolean;
}
