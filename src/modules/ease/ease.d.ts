/**
 * Easing curves and interpolation helpers.
 *
 * A curve maps the progress `t` of an animation to a value, with `f(0) = 0`
 * and `f(1) = 1`; `back` and `elastic` curves overshoot in between. Every
 * curve clamps `t` to [0, 1] first, so a last frame past the end still gives
 * a valid value. Curves have short names (`outBack`) and the long names of
 * easings.net (`easeOutBack`); `Tween` accepts either as a string.
 *
 * @example
 * ```js
 * const y = Ease.lerp(400, 120, Ease.outBack(elapsed / 0.6));
 * camera.x = Ease.damp(camera.x, player.x, 10, dt);   // same speed at 30 and 60 FPS
 * ```
 */
declare namespace Ease {
    /** A curve: progress (clamped to 0..1) to eased value. */
    type Curve = (t: number) => number;
    /** A curve, or the name of one (`"outBack"`, `"easeOutBack"`). */
    type Easing = Curve | string;

    /** The three forms of a curve family. */
    interface Family {
        in: Curve;
        out: Curve;
        inOut: Curve;
    }

    const linear: Curve;
    const inQuad: Curve, outQuad: Curve, inOutQuad: Curve;
    const inCubic: Curve, outCubic: Curve, inOutCubic: Curve;
    const inQuart: Curve, outQuart: Curve, inOutQuart: Curve;
    const inQuint: Curve, outQuint: Curve, inOutQuint: Curve;
    const inSine: Curve, outSine: Curve, inOutSine: Curve;
    const inExpo: Curve, outExpo: Curve, inOutExpo: Curve;
    const inCirc: Curve, outCirc: Curve, inOutCirc: Curve;
    /** Overshoot of 1.70158 (times 1.525 in `inOutBack`, as easings.net); see `back()`. */
    const inBack: Curve, outBack: Curve, inOutBack: Curve;
    /** Amplitude 1, period 0.3; see `elastic()`. */
    const inElastic: Curve, outElastic: Curve, inOutElastic: Curve;
    const inBounce: Curve, outBounce: Curve, inOutBounce: Curve;

    /** Long names (easings.net), the same functions as the short ones. */
    const easeInQuad: Curve, easeOutQuad: Curve, easeInOutQuad: Curve;
    const easeInCubic: Curve, easeOutCubic: Curve, easeInOutCubic: Curve;
    const easeInQuart: Curve, easeOutQuart: Curve, easeInOutQuart: Curve;
    const easeInQuint: Curve, easeOutQuint: Curve, easeInOutQuint: Curve;
    const easeInSine: Curve, easeOutSine: Curve, easeInOutSine: Curve;
    const easeInExpo: Curve, easeOutExpo: Curve, easeInOutExpo: Curve;
    const easeInCirc: Curve, easeOutCirc: Curve, easeInOutCirc: Curve;
    const easeInBack: Curve, easeOutBack: Curve, easeInOutBack: Curve;
    const easeInElastic: Curve, easeOutElastic: Curve, easeInOutElastic: Curve;
    const easeInBounce: Curve, easeOutBounce: Curve, easeInOutBounce: Curve;

    /** Every name `get()` accepts. */
    const names: readonly string[];

    /** Returns the curve named `ease`, or `ease` itself when it is a function. Throws on unknown names. */
    function get(ease: Easing): Curve;

    /** Back curves with another overshoot (times 1.525 in `inOut`); 0 is a cubic, larger values overshoot more. */
    function back(overshoot?: number): Family;
    /** Elastic curves; `amplitude` >= 1 (default 1), `period` > 0 (default 0.3). */
    function elastic(options?: { amplitude?: number; period?: number }): Family;
    /** `count` equal jumps, for frame-by-frame motion; reaches 1 only at t = 1. */
    function steps(count: number): Curve;
    /** CSS `cubic-bezier(x1, y1, x2, y2)`; `x1` and `x2` within [0, 1]. */
    function cubicBezier(x1: number, y1: number, x2: number, y2: number): Curve;
    /** The curve played backwards: `1 - f(1 - t)`. */
    function reverse(ease: Easing): Curve;
    /** The curve forward then back: 0 → 1 → 0, for pulses. */
    function mirror(ease: Easing): Curve;

    /** `a + (b - a) * t`; `t` is not clamped. */
    function lerp(a: number, b: number, t: number): number;
    /** The `t` for which `lerp(a, b, t)` is `value`; 0 when `a === b`. */
    function inverseLerp(a: number, b: number, value: number): number;
    /** Maps `value` from [inMin, inMax] to [outMin, outMax], without clamping. */
    function remap(value: number, inMin: number, inMax: number, outMin: number, outMax: number): number;
    function clamp(value: number, min: number, max: number): number;
    /** 0 below `edge0`, 1 above `edge1`, a smooth S-curve in between. */
    function smoothstep(edge0: number, edge1: number, x: number): number;
    /**
     * Moves `current` towards `target`, closing the same share of the gap per
     * second at any frame rate. `lambda` is the speed (about 5 to 15 for a
     * camera or UI follow); `dt` is the frame delta in seconds.
     */
    function damp(current: number, target: number, lambda: number, dt: number): number;
}
