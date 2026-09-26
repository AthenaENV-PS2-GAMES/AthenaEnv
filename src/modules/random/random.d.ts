/**
 * Seedable pseudo-random numbers (xoshiro128**, computed in C).
 *
 * Unlike `Math.random()`, a generator created with a seed always produces the
 * same sequence: a generated map, a roguelike run or a bug can be
 * reproduced. Integers, `float()`, `pick()`, `shuffle()` and `sample()` are
 * bit-exact on every platform; floats with bounds and gaussians may differ in
 * the last bits between the PS2 and a PC.
 *
 * The module functions (`Random.int()`, `Random.float()`...) use a generator
 * of the script (each script and worker has its own) seeded from the clock;
 * `Random.seed()` makes it reproducible too. Non-finite numeric arguments
 * (NaN, Infinity) throw a RangeError.
 *
 * Example:
 * ```js
 * const rng = new Random.Generator(1234);    // or a string: "level-3"
 * const die = rng.int(1, 6);
 * const loot = rng.pick(["sword", "shield", "potion"], [5, 3, 1]);
 * const team = rng.sample(players, 3);
 * rng.shuffle(deck);
 *
 * // Particles: one call instead of a loop of 500.
 * rng.fill(speeds, 40, 90);
 * rng.fillGaussian(spread, 0, 0.3);
 *
 * const saved = rng.state();                 // JSON-friendly: save it
 * rng.setState(saved);                       // and continue later
 * ```
 */
declare namespace Random {
    /** Seed: a number (integers map one-to-one) or a string (hashed). */
    type Seed = number | string;

    /** The four 32-bit words of a generator state, as returned by `state()`. */
    type State = [number, number, number, number];

    /** Typed arrays of numbers. */
    type NumberArray = Int8Array | Uint8Array | Uint8ClampedArray |
        Int16Array | Uint16Array | Int32Array | Uint32Array |
        Float32Array | Float64Array;

    /** Array or typed array accepted by `pick()`, `shuffle()` and `sample()`. */
    type List<T> = T[] | NumberArray;

    interface Source {
        /** Restarts the sequence from `seed`; without one, from the clock. */
        seed(seed?: Seed): void;
        /** Integer in [min, max], both inclusive, without modulo bias. Bounds are int32. */
        int(min: number, max: number): number;
        /** Float in [0, 1). */
        float(): number;
        /** Float in [0, max). */
        float(max: number): number;
        /** Float in [min, max). */
        float(min: number, max: number): number;
        /** True with probability `p` (default 0.5). */
        bool(p?: number): boolean;
        /** Normally distributed number (default mean 0, standard deviation 1). */
        gaussian(mean?: number, stddev?: number): number;
        /** Angle in radians, in [0, 2 pi). */
        angle(): number;
        /**
         * Random element, or `undefined` for an empty array. With `weights`
         * (one per item), drawn with probability proportional to its weight.
         */
        pick<T>(items: T[], weights?: number[] | Float32Array): T | undefined;
        pick(items: NumberArray, weights?: number[] | Float32Array): number | undefined;
        /** `count` different elements in random order, as a new array. */
        sample<T>(items: T[], count: number): T[];
        sample(items: NumberArray, count: number): number[];
        /** Shuffles in place (Fisher-Yates) and returns the same array. */
        shuffle<A extends List<any>>(items: A): A;
        /**
         * Index drawn with probability proportional to its weight. Weights
         * must be finite and non-negative, with at least one above zero.
         */
        weighted(weights: number[] | Float32Array): number;
        /**
         * Fills a typed array in one call and returns it. Float arrays get
         * floats in [min, max) (default [0, 1); one bound is the max); integer
         * arrays get integers in [min, max], by default the whole range of
         * the type (random bytes for a Uint8Array).
         */
        fill<A extends NumberArray>(array: A, min?: number, max?: number): A;
        /** Fills a float array with normally distributed numbers and returns it. */
        fillGaussian<A extends Float32Array | Float64Array>(array: A,
            mean?: number, stddev?: number): A;
        /** Copy of the current state, for saving. */
        state(): State;
        /** Restores a state returned by `state()`. */
        setState(state: State): void;
    }

    /** Independent generator. */
    class Generator implements Source {
        /** Seeded with `seed`, or from the clock without one. */
        constructor(seed?: Seed);
        seed(seed?: Seed): void;
        int(min: number, max: number): number;
        float(): number;
        float(max: number): number;
        float(min: number, max: number): number;
        bool(p?: number): boolean;
        gaussian(mean?: number, stddev?: number): number;
        angle(): number;
        pick<T>(items: T[], weights?: number[] | Float32Array): T | undefined;
        pick(items: NumberArray, weights?: number[] | Float32Array): number | undefined;
        sample<T>(items: T[], count: number): T[];
        sample(items: NumberArray, count: number): number[];
        shuffle<A extends List<any>>(items: A): A;
        weighted(weights: number[] | Float32Array): number;
        fill<A extends NumberArray>(array: A, min?: number, max?: number): A;
        fillGaussian<A extends Float32Array | Float64Array>(array: A,
            mean?: number, stddev?: number): A;
        state(): State;
        setState(state: State): void;
        /** New generator at the same point of the sequence. */
        clone(): Generator;
    }

    /** The script's generator: see `Source`. */
    function seed(seed?: Seed): void;
    function int(min: number, max: number): number;
    function float(): number;
    function float(max: number): number;
    function float(min: number, max: number): number;
    function bool(p?: number): boolean;
    function gaussian(mean?: number, stddev?: number): number;
    function angle(): number;
    function pick<T>(items: T[], weights?: number[] | Float32Array): T | undefined;
    function pick(items: NumberArray, weights?: number[] | Float32Array): number | undefined;
    function sample<T>(items: T[], count: number): T[];
    function sample(items: NumberArray, count: number): number[];
    function shuffle<A extends List<any>>(items: A): A;
    function weighted(weights: number[] | Float32Array): number;
    function fill<A extends NumberArray>(array: A, min?: number, max?: number): A;
    function fillGaussian<A extends Float32Array | Float64Array>(array: A,
        mean?: number, stddev?: number): A;
    function state(): State;
    function setState(state: State): void;
}
