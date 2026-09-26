/*
 * Tweens: animate numeric properties of any object over time.
 *
 * Tweens advance in a Loop system (preUpdate), registered while at least one
 * tween is active, so values are updated before the game's update and draw.
 * Game-time tweens follow Loop.setTimeScale(); realTime tweens do not.
 */
import * as Loop from "Loop";
import * as Ease from "Ease";

const active = [];
let system = null;

function useSystem() {
    if (system) return;
    system = Loop.addSystem({
        name: "tween",
        priority: -100,
        realTime: true,
        preUpdate(realDt) { advance(Loop.getDeltaTime(), realDt); },
    });
}

function releaseSystem() {
    if (system && active.length === 0) {
        Loop.removeSystem(system);
        system = null;
    }
}

/* Advances every tween; tweens created meanwhile start with the next call. */
function advance(dt, realDt) {
    const count = active.length;
    for (let i = 0; i < count; i++) {
        const tween = active[i];
        if (!tween.done) tween._advance(tween.realTime ? realDt : dt);
    }
    let kept = 0;
    for (let i = 0; i < active.length; i++) {
        if (!active[i].done) active[kept++] = active[i];
    }
    active.length = kept;
    releaseSystem();
}

/* Packed RGBA colors (Color.new): each byte interpolated and clamped. */
function mixColor(a, b, t) {
    let out = 0;
    for (let shift = 0; shift < 32; shift += 8) {
        const ca = (a >>> shift) & 255, cb = (b >>> shift) & 255;
        let c = Math.round(ca + (cb - ca) * t);
        c = c < 0 ? 0 : c > 255 ? 255 : c;
        out |= c << shift;
    }
    return out >>> 0;
}

function checkFunction(options, name) {
    const value = options[name];
    if (value !== undefined && typeof value !== "function") {
        throw new TypeError(`Tween ${name} must be a function`);
    }
    return value;
}

class TweenHandle {
    constructor(target, props, duration, options, isFrom) {
        if (target === null || (typeof target !== "object" && typeof target !== "function")) {
            throw new TypeError("Tween target must be an object");
        }
        if (props === null || typeof props !== "object") {
            throw new TypeError("Tween properties must be an object");
        }
        if (typeof duration !== "number" || !(duration >= 0) || duration === Infinity) {
            throw new RangeError("Tween duration must be a finite number of seconds, zero or more");
        }
        if (options === undefined) options = {};
        if (options === null || typeof options !== "object") {
            throw new TypeError("Tween options must be an object");
        }

        const delay = options.delay ?? 0;
        if (typeof delay !== "number" || !(delay >= 0) || delay === Infinity) {
            throw new RangeError("Tween delay must be a finite number of seconds, zero or more");
        }
        const repeat = options.repeat ?? 0;
        if (!(repeat === Infinity || (Number.isInteger(repeat) && repeat >= 0))) {
            throw new RangeError("Tween repeat must be a non-negative integer or Infinity");
        }
        if (repeat === Infinity && duration === 0) {
            throw new RangeError("Tween repeat: Infinity needs a duration above zero");
        }
        for (const name of ["yoyo", "realTime", "overwrite"]) {
            if (options[name] !== undefined && typeof options[name] !== "boolean") {
                throw new TypeError(`Tween ${name} must be a boolean`);
            }
        }

        const keys = Object.keys(props);
        const colors = options.colors ?? [];
        if (!Array.isArray(colors) || !colors.every(key => keys.includes(key))) {
            throw new TypeError("Tween colors must list animated properties");
        }
        for (const key of keys) {
            if (typeof props[key] !== "number" || !Number.isFinite(props[key])) {
                throw new TypeError(`Tween property '${key}' must be a finite number`);
            }
            if (typeof target[key] !== "number") {
                throw new TypeError(`Tween target property '${key}' must hold a number`);
            }
        }

        this.target = target;
        this.duration = duration;
        this.realTime = options.realTime ?? false;
        this.done = false;
        this._keys = keys;
        this._colors = new Set(colors);
        this._ease = Ease.get(options.ease ?? "outQuad");
        this._delay = delay;
        this._repeat = repeat;
        this._yoyo = options.yoyo ?? false;
        this._overwrite = options.overwrite ?? false;
        this._onStart = checkFunction(options, "onStart");
        this._onUpdate = checkFunction(options, "onUpdate");
        this._onRepeat = checkFunction(options, "onRepeat");
        this._onComplete = checkFunction(options, "onComplete");
        this._from = {};
        this._to = {};
        this._elapsed = 0;
        this._cycle = 0;
        this._started = false;
        this._paused = false;
        this.finished = new Promise(resolve => { this._resolve = resolve; });

        if (isFrom) {
            /* Shown from the first frame, so the object does not flash at its end state. */
            for (const key of keys) {
                this._to[key] = target[key];
                this._from[key] = props[key];
                target[key] = props[key];
            }
        } else {
            for (const key of keys) this._to[key] = props[key];
        }
        this._isFrom = isFrom;

        active.push(this);
        useSystem();
    }

    /** Awaitable: resolves with true when the tween completes, false when killed. */
    then(onFulfilled, onRejected) {
        return this.finished.then(onFulfilled, onRejected);
    }

    get paused() { return this._paused; }
    get active() { return !this.done; }
    /** Progress of the current cycle, 0..1 (0 during the delay). */
    get progress() {
        if (this.done) return 1;
        return this.duration > 0 ? Math.min(1, this._elapsed / this.duration) : (this._started ? 1 : 0);
    }

    pause() { this._paused = true; return this; }
    resume() { this._paused = false; return this; }

    /** Stops the tween; with `complete`, jumps to its end values and runs onComplete. */
    kill(complete = false) {
        if (this.done) return;
        if (complete) {
            if (!this._started) this._start();
            this._renderEnd();
            this._finish(true);
        } else {
            this._finish(false);
        }
    }

    _start() {
        this._started = true;
        if (this._overwrite) {
            for (const other of active) {
                if (other !== this && other.target === this.target && !other.done) other.kill(false);
            }
        }
        if (!this._isFrom) {
            for (const key of this._keys) this._from[key] = this.target[key];
        }
        if (this._onStart) this._onStart(this.target);
    }

    /* Timeline position q (0..1) of the current cycle: yoyo cycles run backwards. */
    _render(q) {
        const eased = this._ease(q);
        for (const key of this._keys) {
            const a = this._from[key], b = this._to[key];
            this.target[key] = this._colors.has(key) ? mixColor(a, b, eased) : a + (b - a) * eased;
        }
    }

    _backward() {
        return this._yoyo && this._cycle % 2 === 1;
    }

    /* Exact end values of the last cycle, without the curve's rounding. */
    _renderEnd() {
        const end = this._backward() ? this._from : this._to;
        for (const key of this._keys) this.target[key] = end[key];
    }

    _finish(completed) {
        this.done = true;
        if (completed && this._onComplete) this._onComplete(this.target);
        this._resolve(completed);
    }

    _advance(dt) {
        if (this._paused || this.done) return;
        if (this._delay > 0) {
            this._delay -= dt;
            if (this._delay > 0) return;
            dt = -this._delay;
            this._delay = 0;
        }
        if (!this._started) this._start();
        if (this.done) return;   // killed by onStart

        this._elapsed += dt;
        for (;;) {
            if (this._elapsed < this.duration) {
                const p = this._elapsed / this.duration;
                this._render(this._backward() ? 1 - p : p);
                if (this._onUpdate) this._onUpdate(this.target, p);
                return;
            }
            if (this._cycle < this._repeat) {
                this._elapsed -= this.duration;
                this._cycle++;
                if (this._onRepeat) this._onRepeat(this.target, this._cycle);
                if (this.done) return;   // killed by onRepeat
                continue;
            }
            this._elapsed = this.duration;
            this._renderEnd();
            if (this._onUpdate) this._onUpdate(this.target, 1);
            this._finish(true);
            return;
        }
    }
}

/** Animates `props` of `target` from their current values to the given ones. */
export function to(target, props, duration, options) {
    return new TweenHandle(target, props, duration, options, false);
}

/** Animates `props` of `target` from the given values to their current ones. */
export function from(target, props, duration, options) {
    return new TweenHandle(target, props, duration, options, true);
}

/** A tween with no properties: `await Tween.delay(0.5)`. */
export function delay(seconds, options) {
    return new TweenHandle({}, {}, seconds, options, false);
}

/** Runs the functions one after the other, awaiting what each returns. */
export async function sequence(steps) {
    if (!Array.isArray(steps) || !steps.every(step => typeof step === "function")) {
        throw new TypeError("Tween.sequence expects an array of functions");
    }
    for (const step of steps) await step();
}

/** Active tweens of `target`. */
export function getTweensOf(target) {
    return active.filter(tween => !tween.done && tween.target === target);
}

/** Kills the tweens of `target`; returns how many were active. */
export function killTweensOf(target, complete = false) {
    const tweens = getTweensOf(target);
    for (const tween of tweens) tween.kill(complete);
    return tweens.length;
}

/** Kills every tween; returns how many were active. */
export function killAll(complete = false) {
    const tweens = active.filter(tween => !tween.done);
    for (const tween of tweens) tween.kill(complete);
    return tweens.length;
}

/** Number of active tweens. */
export function count() {
    return active.reduce((n, tween) => n + (tween.done ? 0 : 1), 0);
}

/**
 * Advances the tweens by `dt` seconds (`realDt` for realTime tweens). Only
 * for manual `while (true)` loops: with Loop.run() tweens advance by themselves.
 */
export function update(dt, realDt = dt) {
    if (typeof dt !== "number" || !(dt >= 0) || typeof realDt !== "number" || !(realDt >= 0)) {
        throw new RangeError("Tween.update needs non-negative deltas");
    }
    advance(dt, realDt);
}
