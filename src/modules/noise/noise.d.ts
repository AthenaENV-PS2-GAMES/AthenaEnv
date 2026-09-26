/**
 * Procedural noise computed in C, in single precision: Perlin and simplex in
 * 2D and 3D, Worley (cellular), and fractal sums of them (fBm, ridged,
 * billow), optionally domain-warped.
 *
 * The module functions use the script's noise (each script and worker has
 * its own), seed 0 until `Noise.seed()` changes it, so the same coordinates
 * always give the same value. A `Noise.Generator` is an independent noise
 * with the same methods: one per layer (height, moisture...) without
 * reseeding. For whole maps, `fill()` samples a grid into a `Float32Array`
 * in one call and `Noise.toTiles()` turns it into tile ids for
 * `TileMap.Instance.setTiles()`, without a per-cell loop in JavaScript.
 *
 * Determinism: the same seed gives the same noise on the same platform, but
 * not bit for bit between a PC and the PS2 (the EE rounds floats toward
 * zero), so a cell right on a `toTiles()` threshold can differ. Generate
 * maps on the console, or store them, when they must match exactly.
 *
 * Example:
 * ```js
 * const W = 64, H = 64;
 * const height = new Noise.Generator("island-7");
 * const heights = height.fill(new Float32Array(W * H), W, H, { scale: 0.05 });
 *
 * const tiles = new Uint16Array(W * H);
 * Noise.toTiles(tiles, heights, [0.3, 0.5, 0.8], [WATER, SAND, GRASS, ROCK]);
 * map.setTiles(0, tiles);
 *
 * const wind = Noise.perlin2(time * 0.5, 0);  // smooth value in about [-1, 1]
 * const peaks = Noise.fbm2(x, y, { mode: "ridged", warp: 0.5 });
 * ```
 */
declare namespace Noise {
    /** Seed: a number (integers map one-to-one) or a string (hashed). */
    type Seed = number | string;

    type Type = "perlin" | "simplex" | "worley";

    /**
     * How each octave is shaped: "fbm" is the noise itself, "ridged" makes
     * sharp crests (mountains), "billow" rounded bumps (clouds, dunes).
     * Ridged and billow need "perlin" or "simplex".
     */
    type Mode = "fbm" | "ridged" | "billow";

    interface FractalOptions {
        /** Base noise (default "simplex"). */
        type?: Type;
        /** Octave shape (default "fbm"). */
        mode?: Mode;
        /** Number of layers, 1 to 16 (default 4). */
        octaves?: number;
        /** Frequency multiplier per octave (default 2). */
        lacunarity?: number;
        /** Amplitude multiplier per octave (default 0.5). */
        gain?: number;
        /**
         * Domain warp in noise units (default 0, off): moves each point by
         * simplex noise first, twisting straight features into organic ones
         * (coasts, rivers, marble). Costs two or three extra samples.
         */
        warp?: number;
    }

    interface FillOptions extends FractalOptions {
        /** Noise units per cell (default 1/16): smaller is smoother. */
        scale?: number;
        /**
         * Offset in cells. A chunk filled with `x: chunkX * width` continues
         * the one on its left seamlessly.
         */
        x?: number;
        y?: number;
        /** Samples a slice of the 3D noise at this depth, in cells (scaled like x and y). */
        z?: number;
        /** Seed for this fill only; the generator's tables by default. */
        seed?: Seed;
        /** Output range (default 0 to 1); values are clamped to it. */
        min?: number;
        max?: number;
        /**
         * Stretches the values actually produced onto [min, max], instead of
         * the nominal range of the noise (Perlin rarely reaches its ends).
         * Separately filled chunks then no longer match at the seams.
         */
        normalize?: boolean;
    }

    /** Sampling functions shared by the module and every Generator. */
    interface Source {
        /** Rebuilds the tables for `seed`; without one, from the clock. */
        seed(seed?: Seed): void;

        /** Perlin noise, about [-1, 1]; 0 at integer coordinates. */
        perlin2(x: number, y: number): number;
        perlin3(x: number, y: number, z: number): number;

        /** Simplex noise, about [-1, 1]; fewer grid artifacts than Perlin. */
        simplex2(x: number, y: number): number;
        simplex3(x: number, y: number, z: number): number;

        /**
         * Worley (cellular) noise: distance to the nearest of one random
         * point per cell, from 0 up to about 1.2. Stones, cells, caves.
         */
        worley2(x: number, y: number): number;
        worley3(x: number, y: number, z: number): number;

        /**
         * Fractal sum of octaves of `type`, normalized to its range (about
         * [-1, 1], or [0, 1.2] for Worley).
         */
        fbm2(x: number, y: number, options?: FractalOptions): number;
        fbm3(x: number, y: number, z: number, options?: FractalOptions): number;

        /**
         * Samples `width * height` cells row by row into `out` and returns
         * it. Cell (col, row) is at ((col + x) * scale, (row + y) * scale).
         * The noise range is mapped onto [min, max]. Blocks the frame: a
         * 128x128 grid of 4 octaves takes about 60 ms on the PS2.
         */
        fill(out: Float32Array, width: number, height: number,
            options?: FillOptions): Float32Array;
    }

    /** Independent noise with the tables of its own seed. */
    class Generator implements Source {
        /** Tables of `seed`, or from the clock without one. */
        constructor(seed?: Seed);
        seed(seed?: Seed): void;
        perlin2(x: number, y: number): number;
        perlin3(x: number, y: number, z: number): number;
        simplex2(x: number, y: number): number;
        simplex3(x: number, y: number, z: number): number;
        worley2(x: number, y: number): number;
        worley3(x: number, y: number, z: number): number;
        fbm2(x: number, y: number, options?: FractalOptions): number;
        fbm3(x: number, y: number, z: number, options?: FractalOptions): number;
        fill(out: Float32Array, width: number, height: number,
            options?: FillOptions): Float32Array;
    }

    /** The script's noise (seed 0 at start): see `Source`. */
    function seed(seed?: Seed): void;
    function perlin2(x: number, y: number): number;
    function perlin3(x: number, y: number, z: number): number;
    function simplex2(x: number, y: number): number;
    function simplex3(x: number, y: number, z: number): number;
    function worley2(x: number, y: number): number;
    function worley3(x: number, y: number, z: number): number;
    function fbm2(x: number, y: number, options?: FractalOptions): number;
    function fbm3(x: number, y: number, z: number, options?: FractalOptions): number;
    function fill(out: Float32Array, width: number, height: number,
        options?: FillOptions): Float32Array;

    /**
     * Classifies `values` into tile ids and returns `out`: each value gets
     * `tiles[k]`, where k is how many of the ascending `thresholds` are
     * <= the value. `tiles` has one entry more than `thresholds`.
     */
    function toTiles(out: Uint16Array, values: Float32Array,
        thresholds: number[] | Float32Array, tiles: number[] | Uint16Array): Uint16Array;
}
