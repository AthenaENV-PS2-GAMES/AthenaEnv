/**
 * Tweens: animate numeric properties of any object over time.
 *
 * Tweens advance by themselves while `Loop.run()` runs, before the game's
 * `update` and `draw` (a Loop system named `"tween"`, present while tweens
 * are active). They follow `Loop.setTimeScale()` unless `realTime` is set.
 * A tween reads the start values when it starts (after its delay), and its
 * last frame sets the exact end values.
 *
 * Tweens are awaitable: `await tween` resolves with `true` when it completes,
 * `false` when it is killed.
 *
 * @example
 * ```js
 * const logo = { x: 320, y: -100, alpha: 0 };
 * async function intro() {   // no top-level await in this QuickJS
 *     await Tween.to(logo, { y: 120, alpha: 128 }, 0.6, { ease: "outBack" });
 *     Tween.to(logo, { y: 130 }, 0.8, { ease: "inOutSine", yoyo: true, repeat: Infinity });
 * }
 * intro();
 *
 * const tint = { color: Color.new(255, 255, 255) };
 * Tween.to(tint, { color: Color.new(255, 0, 0) }, 0.2, { colors: ["color"] });
 *
 * Loop.run(() => {
 *     image.color = tint.color;
 *     image.draw(logo.x, logo.y);
 * });
 * ```
 */
declare namespace Tween {
    interface Options {
        /** Curve or its name; defaults to `"outQuad"`. See `Ease`. */
        ease?: Ease.Easing;
        /** Seconds before the tween starts; defaults to 0. */
        delay?: number;
        /** Extra cycles after the first: an integer, or `Infinity`. Defaults to 0. */
        repeat?: number;
        /** Every other cycle runs backwards, so the tween ends where it started. */
        yoyo?: boolean;
        /** Ignores `Loop.setTimeScale()`: for menus and transitions while paused. */
        realTime?: boolean;
        /** Kills the other tweens of the same target when this one starts. */
        overwrite?: boolean;
        /** Properties holding packed colors (`Color.new()`), interpolated per channel. */
        colors?: string[];
        onStart?(target: any): void;
        /** After every frame of the tween; `progress` is 0..1 within the cycle. */
        onUpdate?(target: any, progress: number): void;
        /** When a new cycle starts; `cycle` counts from 1. */
        onRepeat?(target: any, cycle: number): void;
        onComplete?(target: any): void;
    }

    interface Handle extends PromiseLike<boolean> {
        readonly target: any;
        readonly duration: number;
        readonly realTime: boolean;
        /** Resolves with true when the tween completes, false when it is killed. */
        readonly finished: Promise<boolean>;
        /** False once completed or killed. */
        readonly active: boolean;
        readonly paused: boolean;
        /** Progress of the current cycle, 0..1. */
        readonly progress: number;
        pause(): this;
        resume(): this;
        /** Stops the tween. With `complete`, sets its end values and runs `onComplete`. */
        kill(complete?: boolean): void;
    }

    /** Animates `props` of `target` from their current values to these ones. */
    function to<T extends object>(target: T, props: { [K in keyof T]?: number },
        duration: number, options?: Options): Handle;
    /**
     * Animates `props` of `target` from these values back to the current ones.
     * The start values are applied immediately.
     */
    function from<T extends object>(target: T, props: { [K in keyof T]?: number },
        duration: number, options?: Options): Handle;
    /** A tween without properties, to wait: `await Tween.delay(0.5)`. */
    function delay(seconds: number, options?: Options): Handle;
    /**
     * Calls the functions one after the other, awaiting what each returns.
     * For tweens in parallel, use `Promise.all([...])`.
     */
    function sequence(steps: Array<() => unknown>): Promise<void>;
    /** Active tweens of `target`. */
    function getTweensOf(target: object): Handle[];
    /** Kills the tweens of `target` and returns how many there were. */
    function killTweensOf(target: object, complete?: boolean): number;
    /** Kills every tween and returns how many there were. */
    function killAll(complete?: boolean): number;
    /** Number of active tweens. */
    function count(): number;
    /**
     * Advances the tweens by `dt` seconds (`realDt` for `realTime` tweens).
     * Only for manual `while (true)` loops: under `Loop.run()` this would
     * advance them twice.
     */
    function update(dt: number, realDt?: number): void;
}
