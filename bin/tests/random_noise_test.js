/*
 * Random and Noise modules: seeded generators, distributions, saved state,
 * noise ranges, batch fill and toTiles. Runs on PCSX2/PS2 and on the host
 * (tests/js/run.sh). Ends with the time of a 128x128 fBm fill.
 *
 * It leaves Noise and Random seeded on purpose: run it twice in a row from
 * the launcher to check that a new script starts from fresh module state.
 */
import * as Random from "Random";
import * as Noise from "Noise";

let passed = 0, failed = 0;

// Fingerprint of an integer sequence, from the host runner (i386): the same
// on every platform (see "Determinism fingerprints").
const INT_FINGERPRINT = -1316571021;

function check(name, condition) {
    if (condition) {
        passed++;
    } else {
        failed++;
        console.log("[FAIL] " + name);
    }
}

function throws(name, callback, type) {
    let error = null;
    try { callback(); } catch (e) { error = e; }
    check(name + " throws" + (type ? " " + type.name : ""),
        error !== null && (!type || error instanceof type));
}

check("Random global", globalThis.Random === Random);
check("Noise global", globalThis.Noise === Noise);

// --- Fresh state per context ---------------------------------------------------
// Before anything seeds it, the shared noise is seed 0, even when the previous
// script (or this one, run before) left another seed: fill({ seed: 0 }) builds
// seed 0 tables of its own. Cell 0 samples (x * scale): 0.37 with scale 1.
{
    const expected = Noise.fill(new Float32Array(1), 1, 1, { seed: 0, scale: 1, x: 0.37, y: 0.61, octaves: 1 })[0];
    check("shared noise starts at seed 0 in every script",
        Math.abs((Noise.simplex2(0.37, 0.61) * 0.5 + 0.5) - expected) < 1e-6);
}

// --- Random.Generator ---------------------------------------------------------

{
    const a = new Random.Generator(1234), b = new Random.Generator(1234);
    const c = new Random.Generator(1235);
    let same = true, differ = 0;
    for (let i = 0; i < 200; i++) {
        const va = a.int(0, 1000000);
        same = same && va === b.int(0, 1000000);
        differ += va !== c.int(0, 1000000);
    }
    check("same seed, same sequence", same);
    check("neighbour seeds differ", differ > 195);
    check("Generator instance", a instanceof Random.Generator);
    throws("Generator without new", () => Random.Generator(1), TypeError);
}

{
    const words = new Random.Generator("level-3"), again = new Random.Generator("level-3");
    const other = new Random.Generator("level-4");
    const x = words.float(), y = again.float();
    check("string seeds are reproducible", x === y);
    check("string seeds differ", x !== other.float());
    // Literals: 2 ** 40 + 1 would be computed in float32 by this runtime.
    const big = new Random.Generator(1099511627777), big2 = new Random.Generator(1099511627776);
    check("large integer seeds differ", big.float() !== big2.float());
    throws("NaN seed", () => new Random.Generator(NaN), RangeError);
    throws("object seed", () => new Random.Generator({}), TypeError);
    // Without a seed: two generators created back to back still differ.
    check("clock seeds differ", new Random.Generator().int(0, 2 ** 30) !== new Random.Generator().int(0, 2 ** 30));
}

{
    const rng = new Random.Generator(42);
    const counts = [0, 0, 0, 0, 0, 0];
    let inRange = true, integers = true;
    for (let i = 0; i < 6000; i++) {
        const v = rng.int(1, 6);
        inRange = inRange && v >= 1 && v <= 6;
        integers = integers && Number.isInteger(v);
        counts[v - 1]++;
    }
    check("int(1, 6) in range", inRange);
    check("int returns integers", integers);
    check("int is uniform", counts.every(n => n > 850 && n < 1150));
    check("int reversed bounds", (() => { const v = rng.int(5, -5); return v >= -5 && v <= 5; })());
    check("int full int32 range", Number.isInteger(rng.int(-2147483648, 2147483647)));
    throws("int fractional bound", () => rng.int(0.5, 3), RangeError);
    throws("int bound beyond int32", () => rng.int(0, 2 ** 32), RangeError);
    throws("int without max", () => rng.int(1), TypeError);

    let floatsOk = true, rangeOk = true, sum = 0;
    for (let i = 0; i < 5000; i++) {
        const f = rng.float();
        floatsOk = floatsOk && f >= 0 && f < 1;
        sum += f;
        const r = rng.float(-3, 3);
        rangeOk = rangeOk && r >= -3 && r < 3;
    }
    check("float() in [0, 1)", floatsOk);
    check("float() mean", Math.abs(sum / 5000 - 0.5) < 0.03);
    check("float(min, max) in range", rangeOk);
    check("float(max) in [0, max)", (() => { for (let i = 0; i < 500; i++) { const f = rng.float(10); if (!(f >= 0 && f < 10)) return false; } return true; })());
    // The EE's FPU has no Infinity/NaN: these must be caught before float conversion.
    throws("float infinite bound", () => rng.float(0, Infinity), RangeError);
    throws("float -Infinity bound", () => rng.float(-Infinity, 0), RangeError);
    throws("float NaN bound", () => rng.float(NaN, 1), RangeError);
    throws("float bound beyond float32", () => rng.float(0, 1e300), RangeError);
    throws("bool(NaN)", () => rng.bool(NaN), RangeError);
    throws("gaussian(0, Infinity)", () => rng.gaussian(0, Infinity), RangeError);

    let trues = 0;
    for (let i = 0; i < 4000; i++) trues += rng.bool(0.25);
    check("bool(0.25)", trues > 800 && trues < 1200);
    check("bool() is boolean", typeof rng.bool() === "boolean");
    check("bool(0) and bool(1)", rng.bool(0) === false && rng.bool(1) === true);

    let gsum = 0, gsum2 = 0;
    for (let i = 0; i < 5000; i++) {
        const g = rng.gaussian(5, 2);
        gsum += g;
        gsum2 += g * g;
    }
    const mean = gsum / 5000, sd = Math.sqrt(gsum2 / 5000 - mean * mean);
    check("gaussian mean and stddev", Math.abs(mean - 5) < 0.15 && Math.abs(sd - 2) < 0.15);
}

{
    const rng = new Random.Generator(7);
    const items = ["sword", "shield", "potion"];
    const seen = new Set();
    for (let i = 0; i < 100; i++) seen.add(rng.pick(items));
    check("pick reaches every item", seen.size === 3);
    check("pick of empty array", rng.pick([]) === undefined);
    check("pick of typed array", [10, 20, 30].includes(rng.pick(new Uint8Array([10, 20, 30]))));
    throws("pick of a number", () => rng.pick(5), TypeError);

    const deck = [];
    for (let i = 0; i < 52; i++) deck.push(i);
    const result = rng.shuffle(deck);
    check("shuffle returns the array", result === deck);
    check("shuffle is a permutation", deck.slice().sort((a, b) => a - b).every((v, i) => v === i));
    check("shuffle moves elements", deck.some((v, i) => v !== i));
    const objects = [{ id: 1 }, { id: 2 }, { id: 3 }];
    rng.shuffle(objects);
    check("shuffle keeps objects", objects.map(o => o.id).sort().join() === "1,2,3");
    const typed = new Uint16Array(64).map((_, i) => i);
    rng.shuffle(typed);
    check("shuffle typed array in place",
        Array.from(typed).sort((a, b) => a - b).every((v, i) => v === i) && typed.some((v, i) => v !== i));
    const big = Array.from({ length: 100 }, (_, i) => i);   // beyond the stack buffer
    rng.shuffle(big);
    check("shuffle long array", big.slice().sort((a, b) => a - b).every((v, i) => v === i));
    check("shuffle empty", rng.shuffle([]).length === 0);
    throws("shuffle of an object", () => rng.shuffle({ length: 2 }), TypeError);

    // Same seed shuffles the same way, for arrays and typed arrays.
    const s1 = new Random.Generator(3).shuffle([0, 1, 2, 3, 4, 5, 6, 7]);
    const s2 = Array.from(new Random.Generator(3).shuffle(new Int32Array([0, 1, 2, 3, 4, 5, 6, 7])));
    check("shuffle order is reproducible", s1.join() === s2.join());

    const counts = [0, 0, 0, 0];
    for (let i = 0; i < 10000; i++) counts[rng.weighted([70, 0, 25, 5])]++;
    check("weighted never draws zero weight", counts[1] === 0);
    check("weighted proportions",
        Math.abs(counts[0] - 7000) < 300 && Math.abs(counts[2] - 2500) < 250 && Math.abs(counts[3] - 500) < 120);
    check("weighted Float32Array", rng.weighted(new Float32Array([0, 1])) === 1);
    check("weighted long list", rng.weighted(Array.from({ length: 50 }, (_, i) => i === 42 ? 1 : 0)) === 42);
    throws("weighted all zero", () => rng.weighted([0, 0]), RangeError);
    throws("weighted negative", () => rng.weighted([1, -1]), RangeError);
    throws("weighted Infinity", () => rng.weighted([1, Infinity]), RangeError);
    throws("weighted NaN", () => rng.weighted([1, NaN]), RangeError);
    throws("weighted empty", () => rng.weighted([]), RangeError);
}

{
    const rng = new Random.Generator(99);
    rng.int(0, 10);
    const saved = rng.state();
    check("state is 4 numbers", Array.isArray(saved) && saved.length === 4 &&
        saved.every(n => Number.isInteger(n) && n >= 0 && n < 2 ** 32));
    const json = JSON.parse(JSON.stringify(saved));
    const expected = [rng.int(0, 1e6), rng.float(), rng.int(0, 1e6)];
    rng.setState(json);
    check("setState replays the sequence",
        rng.int(0, 1e6) === expected[0] && rng.float() === expected[1] && rng.int(0, 1e6) === expected[2]);
    const copy = rng.clone();
    check("clone continues the sequence", copy.int(0, 1e6) === rng.int(0, 1e6));
    check("clone is independent", copy !== rng && copy instanceof Random.Generator);
    rng.seed(5);
    const first = rng.float();
    rng.seed(5);
    check("seed() restarts", rng.float() === first);
    throws("setState all zero", () => rng.setState([0, 0, 0, 0]), RangeError);
    throws("setState wrong length", () => rng.setState([1, 2, 3]), RangeError);
    throws("setState out of range", () => rng.setState([1, 2, 3, 2 ** 32]), RangeError);
    throws("method on another object", () => Random.Generator.prototype.int.call({}, 1, 2), TypeError);
}

// --- Shared generator ---------------------------------------------------------

{
    Random.seed(2024);
    const a = [Random.int(1, 100), Random.float(), Random.pick([1, 2, 3])];
    Random.seed(2024);
    const b = [Random.int(1, 100), Random.float(), Random.pick([1, 2, 3])];
    check("Random.seed makes the shared generator reproducible", a.join() === b.join());
    const g = new Random.Generator(2024);
    check("shared generator matches a Generator with the same seed",
        g.int(1, 100) === a[0] && g.float() === a[1]);
    const saved = Random.state();
    const next = Random.int(0, 1e6);
    Random.setState(saved);
    check("shared state round trip", Random.int(0, 1e6) === next);
    check("shared weighted/shuffle/bool/gaussian",
        Random.weighted([0, 1]) === 1 && Random.shuffle([1]).length === 1 &&
        typeof Random.bool() === "boolean" && Number.isFinite(Random.gaussian()));
}

// --- Noise ----------------------------------------------------------------------

{
    const kinds = [
        ["perlin2", (x, y, z) => Noise.perlin2(x, y), -1.05, 1.05],
        ["perlin3", (x, y, z) => Noise.perlin3(x, y, z), -1.05, 1.05],
        ["simplex2", (x, y, z) => Noise.simplex2(x, y), -1, 1],
        ["simplex3", (x, y, z) => Noise.simplex3(x, y, z), -1, 1],
        ["worley2", (x, y, z) => Noise.worley2(x, y), 0, 1.5],
        ["worley3", (x, y, z) => Noise.worley3(x, y, z), 0, 1.8],
        ["fbm2", (x, y, z) => Noise.fbm2(x, y), -1, 1],
        ["fbm3", (x, y, z) => Noise.fbm3(x, y, z, { type: "perlin", octaves: 3 }), -1.05, 1.05],
    ];
    for (const [name, f, lo, hi] of kinds) {
        let min = Infinity, max = -Infinity, smooth = true;
        for (let i = 0; i < 40; i++) {
            for (let j = 0; j < 40; j++) {
                const x = i * 0.137, y = j * 0.119, z = (i + j) * 0.05;
                const v = f(x, y, z);
                min = Math.min(min, v);
                max = Math.max(max, v);
                smooth = smooth && Math.abs(v - f(x + 0.001, y, z)) < 0.05;
            }
        }
        check(name + " range", min >= lo && max <= hi && max - min > 0.3);
        check(name + " is continuous", smooth);
    }
    check("perlin2 zero on the lattice", Noise.perlin2(4, -2) === 0);
    check("same input, same output", Noise.simplex2(1.5, 2.5) === Noise.simplex2(1.5, 2.5));
    // A few points: one sample alone can match across seeds (12 gradients).
    const probe = () => [0.3, 1.7, 2.9, 4.1].map(x => Noise.simplex2(x, x * 0.61)).join();
    Noise.seed(1);
    const s1 = probe();
    Noise.seed(2);
    const s2 = probe();
    Noise.seed(1);
    check("Noise.seed changes and restores", s1 !== s2 && probe() === s1);
    Noise.seed("world");
    check("string noise seed", probe() !== s1);
    Noise.seed(0);
    check("fbm one octave is the base noise", Noise.fbm2(0.3, 0.7, { type: "perlin", octaves: 1 }) === Noise.perlin2(0.3, 0.7));
    throws("NaN coordinate", () => Noise.perlin2(NaN, 0), RangeError);
    throws("huge coordinate", () => Noise.simplex3(0, 0, 1e12), RangeError);
    throws("infinite coordinate", () => Noise.worley2(Infinity, 0), RangeError);
    throws("infinite gain", () => Noise.fbm2(0, 0, { gain: Infinity }), RangeError);
    throws("missing coordinate", () => Noise.perlin3(1, 2), TypeError);
    throws("unknown type", () => Noise.fbm2(0, 0, { type: "value" }), RangeError);
    throws("zero octaves", () => Noise.fbm2(0, 0, { octaves: 0 }), RangeError);
    throws("17 octaves", () => Noise.fbm2(0, 0, { octaves: 17 }), RangeError);
    throws("last octave out of range", () => Noise.fbm2(1e6, 0, { octaves: 16 }), RangeError);
    Noise.seed();   // from the clock, like Random.seed()
    check("Noise.seed() without a seed reseeds", Number.isFinite(Noise.simplex2(0.3, 0.4)));
    Noise.seed(0);
}

{
    const W = 32, H = 16;
    const grid = new Float32Array(W * H);
    const options = { type: "simplex", scale: 0.1, octaves: 3, seed: 7 };
    check("fill returns the array", Noise.fill(grid, W, H, options) === grid);
    check("fill values in [0, 1]", grid.every(v => v >= 0 && v <= 1));
    check("fill is not flat", Math.max(...grid) - Math.min(...grid) > 0.3);

    Noise.seed(7);
    const f = Noise.fbm2(3 * 0.1, 5 * 0.1, { type: "simplex", octaves: 3 });
    check("fill matches fbm2 with the same seed", Math.abs(grid[5 * W + 3] - (f * 0.5 + 0.5)) < 1e-5);
    Noise.seed(0);

    const right = new Float32Array(16 * H);
    Noise.fill(right, 16, H, Object.assign({ x: 16 }, options));
    let tiles = true;
    for (let row = 0; row < H; row++)
        for (let col = 0; col < 16; col++)
            tiles = tiles && Math.abs(right[row * 16 + col] - grid[row * W + 16 + col]) < 1e-5;
    check("fill chunks continue each other", tiles);

    const ranged = Noise.fill(new Float32Array(64), 8, 8, { type: "worley", min: -10, max: 10, z: 0.5 });
    check("fill min/max and 3D worley", ranged.every(v => v >= -10 && v <= 10));
    const larger = new Float32Array(100);
    Noise.fill(larger, 8, 8);
    check("fill leaves the rest of the array", larger[64] === 0 && larger[99] === 0);
    const sub = new Float32Array(new ArrayBuffer(4 * 40), 4 * 8, 16);
    Noise.fill(sub, 4, 4, { seed: 1 });
    check("fill a subarray view", sub.some(v => v !== 0));
    throws("fill too small", () => Noise.fill(new Float32Array(10), 4, 4), RangeError);
    throws("fill Float64Array", () => Noise.fill(new Float64Array(16), 4, 4), TypeError);
    throws("fill Int32Array", () => Noise.fill(new Int32Array(16), 4, 4), TypeError);
    throws("fill plain array", () => Noise.fill([], 0, 0), TypeError);
    throws("fill bad options", () => Noise.fill(grid, W, H, 5), TypeError);
    throws("fill infinite scale", () => Noise.fill(grid, W, H, { scale: Infinity }), RangeError);
    throws("fill infinite max", () => Noise.fill(grid, W, H, { max: Infinity }), RangeError);
    throws("fill NaN z", () => Noise.fill(grid, W, H, { z: NaN }), RangeError);
}

{
    const WATER = 0, SAND = 1, GRASS = 2, ROCK = 3;
    const values = new Float32Array([0.1, 0.3, 0.45, 0.5, 0.79, 0.8, 1]);
    const out = new Uint16Array(values.length);
    check("toTiles returns out", Noise.toTiles(out, values, [0.3, 0.5, 0.8], [WATER, SAND, GRASS, ROCK]) === out);
    check("toTiles classifies", Array.from(out).join() === "0,1,1,2,2,3,3");
    Noise.toTiles(out, values, new Float32Array([0.5]), new Uint16Array([7, 65535]));
    check("toTiles typed thresholds and tiles", Array.from(out).join() === "7,7,7,65535,65535,65535,65535");
    Noise.toTiles(out, values, [], [9]);
    check("toTiles without thresholds", out.every(v => v === 9));
    throws("toTiles tile count", () => Noise.toTiles(out, values, [0.5], [1]), RangeError);
    throws("toTiles NaN threshold", () => Noise.toTiles(out, values, [NaN], [1, 2]), RangeError);
    throws("toTiles descending", () => Noise.toTiles(out, values, [0.5, 0.2], [1, 2, 3]), RangeError);
    throws("toTiles tile id", () => Noise.toTiles(out, values, [0.5], [1, 70000]), RangeError);
    throws("toTiles out too small", () => Noise.toTiles(new Uint16Array(2), values, [0.5], [1, 2]), RangeError);
    throws("toTiles Int16Array out", () => Noise.toTiles(new Int16Array(7), values, [0.5], [1, 2]), TypeError);
}

// --- Random: batches, angle, sample, weighted pick -------------------------------

{
    const rng = new Random.Generator(77);
    let ok = true;
    for (let i = 0; i < 2000; i++) {
        const a = rng.angle();
        ok = ok && a >= 0 && a < 2 * Math.PI;
    }
    check("angle in [0, 2 pi)", ok);

    const speeds = new Float32Array(500);
    check("fill returns the array", rng.fill(speeds, 40, 90) === speeds);
    check("fill floats in [min, max)", speeds.every(v => v >= 40 && v < 90) && new Set(speeds).size > 400);
    rng.fill(speeds);
    check("fill defaults to [0, 1)", speeds.every(v => v >= 0 && v < 1));
    rng.fill(speeds, 3);
    check("fill with one bound is [0, max)", speeds.every(v => v >= 0 && v < 3));
    const doubles = rng.fill(new Float64Array(100), -1, 1);
    check("fill Float64Array", doubles.every(v => v >= -1 && v < 1));

    const bytes = rng.fill(new Uint8Array(4000));
    check("fill Uint8Array: whole byte range", bytes.every(v => v >= 0 && v <= 255) &&
        bytes.some(v => v < 16) && bytes.some(v => v > 239));
    const dice = rng.fill(new Int8Array(3000), 1, 6);
    check("fill Int8Array in [min, max]", dice.every(v => v >= 1 && v <= 6) &&
        [1, 2, 3, 4, 5, 6].every(face => dice.includes(face)));
    const signed = rng.fill(new Int16Array(1000), -5, 5);
    check("fill Int16Array negative bounds", signed.every(v => v >= -5 && v <= 5) && signed.some(v => v < 0));
    const words = rng.fill(new Uint32Array(1000));
    check("fill Uint32Array full range", words.every(v => v >= 0 && v <= 4294967295) && words.some(v => v > 2147483647));
    const ints = rng.fill(new Int32Array(1000), -2147483648, 2147483647);
    check("fill Int32Array full range", ints.some(v => v < 0) && ints.some(v => v > 0));
    throws("fill Uint8Array beyond the type", () => rng.fill(new Uint8Array(4), 0, 256), RangeError);
    throws("fill fractional int bound", () => rng.fill(new Uint8Array(4), 0.5, 3), RangeError);
    throws("fill min above max", () => rng.fill(new Uint8Array(4), 9, 3), RangeError);
    throws("fill plain array", () => rng.fill([1, 2]), TypeError);

    const spread = rng.fillGaussian(new Float32Array(5001), 2, 0.5);
    const mean = spread.reduce((a, b) => a + b, 0) / spread.length;
    const sd = Math.sqrt(spread.reduce((a, b) => a + (b - mean) * (b - mean), 0) / spread.length);
    check("fillGaussian mean and stddev", Math.abs(mean - 2) < 0.05 && Math.abs(sd - 0.5) < 0.05);
    throws("fillGaussian integer array", () => rng.fillGaussian(new Int32Array(4)), TypeError);
    throws("fillGaussian infinite stddev", () => rng.fillGaussian(new Float32Array(4), 0, Infinity), RangeError);

    // Same seed, same batch; fill and single draws follow the same sequence.
    const a = new Random.Generator(5).fill(new Float32Array(8), 0, 1);
    const b = new Random.Generator(5);
    check("fill matches float() draws", Array.from(a).every(v => v === b.float(0, 1)));

    const players = ["ana", "bia", "caio", "davi", "edu", "fe"];
    const team = rng.sample(players, 3);
    check("sample returns count distinct items", team.length === 3 && new Set(team).size === 3 &&
        team.every(p => players.includes(p)));
    check("sample does not change the source", players.join() === "ana,bia,caio,davi,edu,fe");
    check("sample of all is a permutation", rng.sample(players, 6).slice().sort().join() === players.slice().sort().join());
    check("sample of none", rng.sample(players, 0).length === 0);
    check("sample of a typed array", rng.sample(new Uint8Array([7, 8, 9]), 2).every(v => v >= 7 && v <= 9));
    const counts = {};
    for (let i = 0; i < 3000; i++)
        for (const p of rng.sample(players, 2)) counts[p] = (counts[p] || 0) + 1;
    check("sample is uniform", players.every(p => Math.abs(counts[p] - 1000) < 150));
    const long = Array.from({ length: 100 }, (_, i) => i);   // beyond the stack buffer
    check("sample long array", new Set(rng.sample(long, 50)).size === 50);
    throws("sample count above length", () => rng.sample(players, 7), RangeError);
    throws("sample fractional count", () => rng.sample(players, 1.5), RangeError);

    const loot = { sword: 0, shield: 0, potion: 0 };
    for (let i = 0; i < 9000; i++) loot[rng.pick(["sword", "shield", "potion"], [6, 0, 3])]++;
    check("pick with weights", loot.shield === 0 && Math.abs(loot.sword - 6000) < 250);
    throws("pick weights length", () => rng.pick([1, 2, 3], [1, 1]), RangeError);
    check("pick of empty array ignores weights", rng.pick([], [1]) === undefined);
    check("module functions include the new ones", typeof Random.fill === "function" &&
        typeof Random.sample === "function" && typeof Random.angle === "function" &&
        typeof Random.fillGaussian === "function");
}

// --- Noise.Generator, modes, warp and normalize ------------------------------------

{
    const terrain = new Noise.Generator("terrain"), same = new Noise.Generator("terrain");
    const moisture = new Noise.Generator("moisture");
    const probe = g => [0.3, 1.7, 2.9].map(x => g.simplex2(x, x * 0.61)).join();
    check("Generator instance", terrain instanceof Noise.Generator);
    check("same seed, same noise", probe(terrain) === probe(same));
    check("different generators differ", probe(terrain) !== probe(moisture));
    Noise.seed(9);
    const before = probe(terrain);
    Noise.seed(10);
    check("a Generator ignores Noise.seed", probe(terrain) === before);
    Noise.seed(0);

    const grid = terrain.fill(new Float32Array(64), 8, 8, { scale: 0.2 });
    const viaSeed = Noise.fill(new Float32Array(64), 8, 8, { scale: 0.2, seed: "terrain" });
    check("Generator.fill equals fill with the same seed", grid.every((v, i) => v === viaSeed[i]));
    check("fill and fbm2 default to 4 octaves",
        Math.abs(grid[9] - (terrain.fbm2(1 * 0.2, 1 * 0.2) * 0.5 + 0.5)) < 1e-6);
    throws("Generator without new", () => Noise.Generator(1), TypeError);
    throws("Generator method on another object", () => Noise.Generator.prototype.perlin2.call({}, 0, 0), TypeError);

    for (const mode of ["fbm", "ridged", "billow"]) {
        let min = Infinity, max = -Infinity;
        for (let i = 0; i < 300; i++) {
            const v = terrain.fbm2(i * 0.13, i * 0.07, { mode, warp: 0.4 });
            min = Math.min(min, v);
            max = Math.max(max, v);
        }
        check(mode + " with warp in [-1, 1]", min >= -1.00001 && max <= 1.00001 && max - min > 0.5);
    }
    check("ridged differs from fbm", terrain.fbm2(0.4, 0.8, { mode: "ridged" }) !== terrain.fbm2(0.4, 0.8));
    check("warp changes the value", terrain.fbm2(0.4, 0.8, { warp: 1 }) !== terrain.fbm2(0.4, 0.8));
    throws("ridged worley", () => terrain.fbm2(0, 0, { type: "worley", mode: "ridged" }), RangeError);
    throws("unknown mode", () => terrain.fbm2(0, 0, { mode: "terraced" }), RangeError);
    throws("infinite warp", () => terrain.fbm2(0, 0, { warp: Infinity }), RangeError);
    throws("warp beyond the coordinate range", () => terrain.fbm2(0, 0, { warp: 2e9 }), RangeError);

    const plain = terrain.fill(new Float32Array(400), 20, 20, { type: "perlin", scale: 0.08, min: 10, max: 20 });
    const stretched = terrain.fill(new Float32Array(400), 20, 20,
        { type: "perlin", scale: 0.08, min: 10, max: 20, normalize: true });
    check("normalize spans [min, max]", Math.min(...stretched) === 10 && Math.abs(Math.max(...stretched) - 20) < 1e-3);
    check("without normalize perlin stays inside", Math.min(...plain) > 10 && Math.max(...plain) < 20);

    // z is in cells, scaled like x and y.
    const slice = terrain.fill(new Float32Array(1), 1, 1, { scale: 0.5, z: 3, octaves: 1 })[0];
    check("z is scaled by scale", Math.abs(slice - (terrain.simplex3(0, 0, 1.5) * 0.5 + 0.5)) < 1e-6);
}

// --- No user code while holding buffer pointers --------------------------------
// Type checks read the class of the typed array: overriding instanceof or the
// global constructors changes nothing (they used to run in toTiles between
// taking two buffer pointers).
{
    const RealFloat32Array = Float32Array;
    const values = new RealFloat32Array([0.2, 0.9]);
    const out = new Uint16Array(2);
    let calls = 0;
    Object.defineProperty(RealFloat32Array, Symbol.hasInstance, {
        configurable: true,
        value() { calls++; throw new Error("user code ran"); },
    });
    let threw = null;
    try {
        Noise.toTiles(out, values, [0.5], [1, 2]);
        Noise.fill(new RealFloat32Array(4), 2, 2);
    } catch (e) { threw = e; }
    check("typed array checks run no user code", threw === null && calls === 0 && out.join() === "1,2");
    delete RealFloat32Array[Symbol.hasInstance];

    globalThis.Float32Array = Int32Array;
    throws("fill still rejects Int32Array", () => Noise.fill(new Int32Array(4), 2, 2), TypeError);
    check("fill still accepts the real Float32Array", Noise.fill(new RealFloat32Array(4), 2, 2).length === 4);
    globalThis.Float32Array = RealFloat32Array;
}

// --- Determinism fingerprints -------------------------------------------------------
// Integers must be bit-exact everywhere; the float fingerprint is printed to
// compare PC, PCSX2 and a console (the EE rounds floats toward zero). The
// hash stays in int32 (Math.imul, Int32Array views): this runtime computes
// other arithmetic in float32 and would drop bits.
{
    const fnv = (words) => {
        let h = -2128831035;   // 2166136261 as int32
        for (let i = 0; i < words.length; i++) h = Math.imul(h ^ words[i], 16777619);
        return h;
    };
    const rng = new Random.Generator("fingerprint");
    const ints = new Int32Array(256);
    for (let i = 0; i < 128; i++) ints[i] = rng.int(-1000000, 1000000);
    rng.fill(ints.subarray(128), -2147483648, 2147483647);
    const intPrint = fnv(ints);
    check("integer sequence fingerprint matches the reference", intPrint === INT_FINGERPRINT);

    const grid = new Noise.Generator("fingerprint").fill(new Float32Array(32 * 32), 32, 32,
        { scale: 0.09, octaves: 4, warp: 0.3 });
    const floatPrint = fnv(new Int32Array(grid.buffer));
    const floats = rng.fill(new Float32Array(64), -10, 10);
    console.log(`fingerprints: ints ${intPrint}, noise grid ${floatPrint}, ` +
        `float draws ${fnv(new Int32Array(floats.buffer))}`);
}

// Leave seeds set: the next script must not see them (see the header).
Noise.seed(12345);
Random.seed(12345);

// --- Speed ------------------------------------------------------------------------

{
    const heights = new Float32Array(128 * 128);
    const start = Date.now();
    Noise.fill(heights, 128, 128, { scale: 0.05, octaves: 4, seed: 7 });
    const fillMs = Date.now() - start;
    const rng = new Random.Generator(1);
    // Per-call cost of the bindings, 10000 calls each.
    const time = (callback) => {
        const start = Date.now();
        for (let i = 0; i < 10000; i++) callback(i);
        return Date.now() - start;
    };
    const empty = time(() => {});
    const intMs = time(() => rng.int(0, 100));
    const floatMs = time(() => rng.float(-1.5, 2.5));
    const simplexMs = time(i => Noise.simplex2(i * 0.1, 0.5));
    const fbmMs = time(i => Noise.fbm2(i * 0.1, 0.5));
    console.log(`fill 128x128, 4 octaves of simplex: ${fillMs} ms`);
    console.log(`10000 calls: empty loop ${empty} ms, rng.int ${intMs} ms, rng.float(min, max) ${floatMs} ms, ` +
        `Noise.simplex2 ${simplexMs} ms, Noise.fbm2 ${fbmMs} ms`);
}

// --- fillAsync: the same grid, computed by a worker -------------------------------

async function asyncTests() {
    const terrain = new Noise.Generator("async");
    const options = { scale: 0.07, octaves: 5, mode: "ridged", warp: 0.3, min: -2, max: 2 };
    const expected = terrain.fill(new Float32Array(40 * 30), 40, 30, options);
    const out = new Float32Array(40 * 30 + 5);
    const job = terrain.fillAsync(out, 40, 30, options);
    const status = job.poll();
    check("fillAsync returns a Job with progress", typeof status.state === "string" &&
        status.rows === 30 && status.rowsDone >= 0 && status.rowsDone <= 30);
    const result = await job;
    check("fillAsync resolves with the array", result === out);
    check("fillAsync matches fill", expected.every((v, i) => v === out[i]));
    check("fillAsync leaves the rest", out[40 * 30] === 0);
    const done = job.poll();
    check("finished job reports every row", done.state === "done" && done.rowsDone === 30);

    // The script's noise and the seed option work too.
    const viaModule = await Noise.fillAsync(new Float32Array(64), 8, 8, { seed: "async", scale: 0.2 });
    const sync = terrain.fill(new Float32Array(64), 8, 8, { scale: 0.2 });
    check("Noise.fillAsync with a seed", viaModule.every((v, i) => v === sync[i]));

    // Checked before starting: nothing runs for a bad call.
    throws("fillAsync too small", () => terrain.fillAsync(new Float32Array(10), 4, 4), RangeError);
    throws("fillAsync wrong array type", () => terrain.fillAsync(new Float64Array(16), 4, 4), TypeError);

    // Cancelled: the promise rejects, the array is untouched.
    const big = new Float32Array(256 * 256);
    const slow = terrain.fillAsync(big, 256, 256, { octaves: 16, type: "worley", z: 1 });
    slow.cancel();
    let reason = null;
    try { await slow; } catch (e) { reason = e; }
    check("cancelled fillAsync rejects", reason instanceof Error && /cancelled/.test(reason.message));
    check("cancelled fillAsync leaves the array", big.every(v => v === 0));
}

asyncTests()
    .catch(error => { failed++; console.log("[FAIL] " + error + "\n" + (error.stack || "")); })
    .then(() => {
        console.log(`Result: ${passed} passed, ${failed} failed`);
        if (!failed) console.log("Random and Noise module test passed");
    });
