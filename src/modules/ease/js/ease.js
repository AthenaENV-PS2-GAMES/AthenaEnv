/*
 * Easing curves and interpolation helpers.
 *
 * Every curve maps a progress t to a value, with f(0) = 0 and f(1) = 1, and
 * clamps t to [0, 1] first: a tween whose last frame overshoots its duration
 * still gets a valid value. Curves are available by short name (outBack) and
 * by the long name used by easings.net (easeOutBack).
 */

const PI = Math.PI;
const BACK = 1.70158;

const clamp01 = t => (t < 0 ? 0 : t > 1 ? 1 : t);

/* Mirror an in-curve into its out and in-out forms. */
const outOf = f => t => 1 - f(1 - t);
const inOutOf = f => t => (t < 0.5 ? f(2 * t) / 2 : 1 - f(2 - 2 * t) / 2);

function bounceOut(t) {
    const n = 7.5625, d = 2.75;
    if (t < 1 / d) return n * t * t;
    if (t < 2 / d) return n * (t -= 1.5 / d) * t + 0.75;
    if (t < 2.5 / d) return n * (t -= 2.25 / d) * t + 0.9375;
    return n * (t -= 2.625 / d) * t + 0.984375;
}

function backIn(overshoot) {
    return t => t * t * ((overshoot + 1) * t - overshoot);
}

function elasticIn(amplitude, period) {
    const a = Math.max(1, amplitude);
    const s = (period / (2 * PI)) * Math.asin(1 / a);
    return t => (t === 0 || t === 1 ? t :
        -(a * Math.pow(2, 10 * (t - 1)) * Math.sin(((t - 1 - s) * 2 * PI) / period)));
}

/* Unclamped in-curves; the families below derive out and in-out from them. */
const inCurves = {
    Quad: t => t * t,
    Cubic: t => t * t * t,
    Quart: t => t * t * t * t,
    Quint: t => t * t * t * t * t,
    Sine: t => 1 - Math.cos((t * PI) / 2),
    Expo: t => (t === 0 ? 0 : Math.pow(2, 10 * t - 10)),
    Circ: t => 1 - Math.sqrt(1 - t * t),
    Elastic: elasticIn(1, 0.3),
    Bounce: outOf(bounceOut),
};

/* Penner scales the overshoot of the in-out back curve by 1.525. */
const BACK_IN_OUT = 1.525;

/*
 * { in, out, inOut } of an in-curve, each clamping its input. The in-out form
 * is built from inOutCurve when the family defines another one (back).
 */
function family(inCurve, inOutCurve = inCurve) {
    const out = outOf(inCurve), inOut = inOutOf(inOutCurve);
    return {
        in: t => inCurve(clamp01(t)),
        out: t => out(clamp01(t)),
        inOut: t => inOut(clamp01(t)),
    };
}

const families = { Back: family(backIn(BACK), backIn(BACK * BACK_IN_OUT)) };
for (const name of Object.keys(inCurves)) families[name] = family(inCurves[name]);

const curves = { linear: t => clamp01(t) };
for (const name of ["Quad", "Cubic", "Quart", "Quint", "Sine", "Expo", "Circ", "Back", "Elastic", "Bounce"]) {
    curves["in" + name] = families[name].in;
    curves["out" + name] = families[name].out;
    curves["inOut" + name] = families[name].inOut;
}

export const linear = curves.linear;
export const inQuad = curves.inQuad, outQuad = curves.outQuad, inOutQuad = curves.inOutQuad;
export const inCubic = curves.inCubic, outCubic = curves.outCubic, inOutCubic = curves.inOutCubic;
export const inQuart = curves.inQuart, outQuart = curves.outQuart, inOutQuart = curves.inOutQuart;
export const inQuint = curves.inQuint, outQuint = curves.outQuint, inOutQuint = curves.inOutQuint;
export const inSine = curves.inSine, outSine = curves.outSine, inOutSine = curves.inOutSine;
export const inExpo = curves.inExpo, outExpo = curves.outExpo, inOutExpo = curves.inOutExpo;
export const inCirc = curves.inCirc, outCirc = curves.outCirc, inOutCirc = curves.inOutCirc;
export const inBack = curves.inBack, outBack = curves.outBack, inOutBack = curves.inOutBack;
export const inElastic = curves.inElastic, outElastic = curves.outElastic, inOutElastic = curves.inOutElastic;
export const inBounce = curves.inBounce, outBounce = curves.outBounce, inOutBounce = curves.inOutBounce;

/* Long names, as in easings.net and javascript_samples/easing.js. */
export const easeInQuad = inQuad, easeOutQuad = outQuad, easeInOutQuad = inOutQuad;
export const easeInCubic = inCubic, easeOutCubic = outCubic, easeInOutCubic = inOutCubic;
export const easeInQuart = inQuart, easeOutQuart = outQuart, easeInOutQuart = inOutQuart;
export const easeInQuint = inQuint, easeOutQuint = outQuint, easeInOutQuint = inOutQuint;
export const easeInSine = inSine, easeOutSine = outSine, easeInOutSine = inOutSine;
export const easeInExpo = inExpo, easeOutExpo = outExpo, easeInOutExpo = inOutExpo;
export const easeInCirc = inCirc, easeOutCirc = outCirc, easeInOutCirc = inOutCirc;
export const easeInBack = inBack, easeOutBack = outBack, easeInOutBack = inOutBack;
export const easeInElastic = inElastic, easeOutElastic = outElastic, easeInOutElastic = inOutElastic;
export const easeInBounce = inBounce, easeOutBounce = outBounce, easeInOutBounce = inOutBounce;

/** Names accepted by get(), short and long. */
export const names = Object.freeze(Object.keys(curves).concat(
    Object.keys(curves).filter(n => n !== "linear").map(n => "ease" + n[0].toUpperCase() + n.slice(1))));

/** Resolves a curve given by name or function; throws on unknown names. */
export function get(ease) {
    if (typeof ease === "function") return ease;
    if (typeof ease === "string") {
        let name = ease;
        if (name.startsWith("ease") && name.length > 4) name = name[4].toLowerCase() + name.slice(5);
        const curve = curves[name];
        if (curve) return curve;
    }
    throw new TypeError(`Ease.get: unknown curve '${ease}'`);
}

/** Back curves with a custom overshoot (default 1.70158); inOut uses it times 1.525. */
export function back(overshoot = BACK) {
    if (typeof overshoot !== "number" || !(overshoot >= 0)) {
        throw new RangeError("Ease.back overshoot must be zero or positive");
    }
    return family(backIn(overshoot), backIn(overshoot * BACK_IN_OUT));
}

/** Elastic curves with a custom amplitude (>= 1) and period (seconds of t, > 0). */
export function elastic({ amplitude = 1, period = 0.3 } = {}) {
    if (!(amplitude >= 1) || !(period > 0)) {
        throw new RangeError("Ease.elastic needs amplitude >= 1 and period > 0");
    }
    return family(elasticIn(amplitude, period));
}

/** Jumps in `count` equal steps, reaching 1 only at t = 1: frame-by-frame motion. */
export function steps(count) {
    if (!Number.isInteger(count) || count < 1) {
        throw new RangeError("Ease.steps count must be a positive integer");
    }
    return t => (t >= 1 ? 1 : Math.floor(clamp01(t) * count) / count);
}

/** CSS cubic-bezier(x1, y1, x2, y2); x1 and x2 must be within [0, 1]. */
export function cubicBezier(x1, y1, x2, y2) {
    if (!(x1 >= 0 && x1 <= 1 && x2 >= 0 && x2 <= 1)) {
        throw new RangeError("Ease.cubicBezier x1 and x2 must be within [0, 1]");
    }
    const cx = 3 * x1, bx = 3 * (x2 - x1) - cx, ax = 1 - cx - bx;
    const cy = 3 * y1, by = 3 * (y2 - y1) - cy, ay = 1 - cy - by;
    const sampleX = s => ((ax * s + bx) * s + cx) * s;
    const sampleY = s => ((ay * s + by) * s + cy) * s;
    const slopeX = s => (3 * ax * s + 2 * bx) * s + cx;

    return t => {
        const x = clamp01(t);
        if (x === 0 || x === 1) return x;
        /* Newton-Raphson on x(s) = x, then bisection if the slope is too flat. */
        let s = x;
        for (let i = 0; i < 6; i++) {
            const error = sampleX(s) - x;
            if (Math.abs(error) < 1e-5) return sampleY(s);
            const slope = slopeX(s);
            if (Math.abs(slope) < 1e-6) break;
            s -= error / slope;
        }
        let lo = 0, hi = 1;
        s = x;
        for (let i = 0; i < 20; i++) {
            const value = sampleX(s);
            if (Math.abs(value - x) < 1e-5) break;
            if (value < x) lo = s; else hi = s;
            s = (lo + hi) / 2;
        }
        return sampleY(s);
    };
}

/** The curve played backwards: reverse(f)(t) = 1 - f(1 - t). */
export function reverse(ease) {
    const f = get(ease);
    return t => 1 - f(1 - clamp01(t));
}

/** Plays the curve forward, then back: 0 -> 1 -> 0 over t in [0, 1]. */
export function mirror(ease) {
    const f = get(ease);
    return t => {
        const x = clamp01(t);
        return x < 0.5 ? f(2 * x) : f(2 - 2 * x);
    };
}

/** a + (b - a) * t, without clamping t. */
export function lerp(a, b, t) {
    return a + (b - a) * t;
}

/** The t for which lerp(a, b, t) is `value`; 0 when a === b. */
export function inverseLerp(a, b, value) {
    return a === b ? 0 : (value - a) / (b - a);
}

/** Maps `value` from [inMin, inMax] to [outMin, outMax], without clamping. */
export function remap(value, inMin, inMax, outMin, outMax) {
    return lerp(outMin, outMax, inverseLerp(inMin, inMax, value));
}

export function clamp(value, min, max) {
    return value < min ? min : value > max ? max : value;
}

/** Hermite interpolation: 0 below edge0, 1 above edge1, smooth in between. */
export function smoothstep(edge0, edge1, x) {
    const t = clamp01(inverseLerp(edge0, edge1, x));
    return t * t * (3 - 2 * t);
}

/*
 * Moves `current` towards `target`, closing the same fraction of the gap per
 * second at any frame rate: current += (target - current) * (1 - e^(-lambda dt)).
 * lambda around 5..15 gives a camera or UI follow; a per-frame `x += (t - x) * k`
 * is slower at 30 FPS than at 60, this is not.
 */
export function damp(current, target, lambda, dt) {
    return lerp(current, target, 1 - Math.exp(-lambda * dt));
}
