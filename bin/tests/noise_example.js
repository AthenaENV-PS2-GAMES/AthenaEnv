// Noise example: an island map from two noise layers, drawn as one texture.
//
//   CROSS     new seed
//   CIRCLE    height mode: fbm / ridged / billow
//   SQUARE    domain warp on / off
//   TRIANGLE  generate in the background (fillAsync) / in the frame (fill)
//   START     quit
//
// Height and moisture are separate Noise.Generators (one per layer, no
// reseeding). Noise.toTiles() classifies both grids in C; JavaScript only
// looks up one color per cell, and only when the map changes.

const SIZE = 128;                 // cells per side (a power of two: GS texture)
const CELLS = SIZE * SIZE;
const VIEW = 384;                 // on-screen size in pixels
const MODES = ["fbm", "ridged", "billow"];

const rgb = (r, g, b) => Color.new(r, g, b, 128);
const GREY = rgb(150, 150, 170), BAR = rgb(90, 200, 120);

// Height classes, from Noise.toTiles(): deep water ... snow.
const DEEP = 0, WATER = 1, SAND = 2, LAND = 3, ROCK = 4, SNOW = 5;
const HEIGHT_COLORS = [rgb(20, 40, 110), rgb(40, 90, 170), rgb(215, 200, 140), 0,
    rgb(120, 110, 100), rgb(240, 240, 245)];
// Land by moisture: dry, grass, forest.
const LAND_COLORS = [rgb(190, 170, 90), rgb(90, 160, 60), rgb(35, 100, 45)];

const font = new Font();
const pad = Gamepad.player(0);
const rng = new Random.Generator();

const heights = new Float32Array(CELLS), moisture = new Float32Array(CELLS);
const heightTiles = new Uint16Array(CELLS), moistureTiles = new Uint16Array(CELLS);
const pixels = new ArrayBuffer(CELLS * 4);
const colors = new Uint32Array(pixels);

const image = new Image();
image.texWidth = SIZE;
image.texHeight = SIZE;
image.bpp = 32;
image.width = VIEW;
image.height = VIEW;
image.color = rgb(255, 255, 255);

const state = {
    seed: rng.int(0, 999999), mode: 0, warp: true, async: false,
    jobs: null, fillMs: 0, paintMs: 0, generation: 0,
};

function options(layer) {
    const base = { scale: 0.025, octaves: 5, x: 0, y: 0, normalize: true };
    return layer === "height"
        ? Object.assign(base, { mode: MODES[state.mode], warp: state.warp ? 0.6 : 0 })
        : Object.assign(base, { scale: 0.04, octaves: 3 });
}

// Classifies both grids in C, then one table lookup per cell in JavaScript.
function paint() {
    const start = Date.now();
    Noise.toTiles(heightTiles, heights, [0.35, 0.42, 0.46, 0.72, 0.84], [DEEP, WATER, SAND, LAND, ROCK, SNOW]);
    Noise.toTiles(moistureTiles, moisture, [0.4, 0.62], [0, 1, 2]);
    for (let i = 0; i < CELLS; i++) {
        const h = heightTiles[i];
        colors[i] = h === LAND ? LAND_COLORS[moistureTiles[i]] : HEIGHT_COLORS[h];
    }
    image.pixels = pixels;   // copies the buffer into the texture
    state.paintMs = Date.now() - start;
}

async function generate() {
    const generation = ++state.generation;
    const height = new Noise.Generator("height-" + state.seed);
    const wet = new Noise.Generator("moisture-" + state.seed);
    const start = Date.now();

    if (state.async) {
        // Two jobs on the worker pool; the frame keeps drawing meanwhile.
        const jobs = [height.fillAsync(heights, SIZE, SIZE, options("height")),
            wet.fillAsync(moisture, SIZE, SIZE, options("moisture"))];
        state.jobs = jobs;
        try {
            await Promise.all(jobs);
        } catch (error) {
            return;   // cancelled by a newer generation
        }
        if (generation !== state.generation)
            return;
        state.jobs = null;
    } else {
        height.fill(heights, SIZE, SIZE, options("height"));
        wet.fill(moisture, SIZE, SIZE, options("moisture"));
    }
    state.fillMs = Date.now() - start;
    paint();
}

function regenerate() {
    if (state.jobs)
        state.jobs.forEach(job => job.cancel());
    state.jobs = null;
    generate();
}

Loop.run({
    draw() {
        Gamepad.update();
        if (pad.justPressed(Gamepad.START)) return Loop.stop();
        if (pad.justPressed(Gamepad.CROSS)) { state.seed = rng.int(0, 999999); regenerate(); }
        if (pad.justPressed(Gamepad.CIRCLE)) { state.mode = (state.mode + 1) % MODES.length; regenerate(); }
        if (pad.justPressed(Gamepad.SQUARE)) { state.warp = !state.warp; regenerate(); }
        if (pad.justPressed(Gamepad.TRIANGLE)) { state.async = !state.async; regenerate(); }

        image.draw(24, 32);

        const x = VIEW + 48;
        font.print(x, 32, "Seed " + state.seed);
        font.print(x, 56, "Mode " + MODES[state.mode] + (state.warp ? " + warp" : ""));
        font.print(x, 80, state.async ? "fillAsync (worker)" : "fill (in frame)");
        font.print(x, 120, "fill  " + state.fillMs + " ms");
        font.print(x, 144, "paint " + state.paintMs + " ms");
        font.print(x, 168, Loop.getStats().fps.toFixed(1) + " FPS");
        if (state.jobs) {
            const done = state.jobs.reduce((sum, job) => sum + job.poll().rowsDone, 0);
            const total = SIZE * state.jobs.length;
            const filled = Math.floor(160 * done / total);
            Draw.rect(x, 200, 160, 12, GREY);
            if (filled >= 1)   // Draw.rect needs at least 1 pixel
                Draw.rect(x, 200, filled, 12, BAR);
        }
        font.print(24, 424, "X seed   O mode   [] warp   /\\ async   START quit");
    },
}, { clearColor: rgb(16, 16, 28) });

generate();
