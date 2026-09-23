// TileMap on-screen benchmark for real hardware.
//
// Everything is drawn on screen: there is no console on a real PS2. Each
// scenario ramps the sprite count and records the largest count that held
// 60 fps. The results table stays on screen, and the script ends in a live
// demo that scrolls a 256x256 world with culling toggling every 5 seconds.
//
// Frame times include the HUD itself (a few lines of text).

const ATLAS_PATHS = ["tests/texture.png", "texture.png", "bin/tests/texture.png"];
const TILE = 16;
const COUNTS = [1024, 2048, 4096, 8192, 16384, 32768, 65536];
const WARMUP_FRAMES = 10;
const MEASURE_FRAMES = 60;
const MEASURE_MAX_MS = 2000;
const TARGET_FPS = 59;
const COLLAPSE_FPS = 20;
const WORLD = 256;

// ---------------------------------------------------------------- HUD

const hudFont = new Font();
// Small text so the panel leaves the tiles visible; the line height follows.
hudFont.scale = 0.6;
const HUD_LINE = hudFont.getTextSize("Ag").height + 2;
const HUD_BACK = Color.new(0, 0, 0, 100);

function hud(lines, x, y) {
    let width = 0;
    for (const line of lines) {
        width = Math.max(width, hudFont.getTextSize(line).width);
    }
    Draw.rect(x - 4, y - 2, width + 8, lines.length * HUD_LINE + 4, HUD_BACK);
    for (let i = 0; i < lines.length; i++) {
        hudFont.print(x, y + i * HUD_LINE, lines[i]);
    }
}

// Rolling frame-to-frame timing over the last 60 frames.
const meter = {
    times: [],
    last: System.getMilliseconds(),
    tick() {
        const now = System.getMilliseconds();
        this.times.push(now - this.last);
        if (this.times.length > 60) this.times.shift();
        this.last = now;
    },
    fps() {
        if (!this.times.length) return 0;
        const sum = this.times.reduce((a, b) => a + b, 0);
        return sum > 0 ? 1000 * this.times.length / sum : 0;
    },
    worst() {
        return this.times.length ? Math.max.apply(null, this.times) : 0;
    },
};

function memoryMB() {
    return (System.getUsedMemory() / 1048576).toFixed(1);
}

// Each result is two short lines: text wider than the 640 px screen is cut.
const results = [];
function resultLines() {
    const lines = [];
    for (const r of results) {
        lines.push(r.text, "  " + r.detail);
    }
    return lines;
}

// ---------------------------------------------------------------- setup

function loadAtlas() {
    for (const path of ATLAS_PATHS) {
        try {
            return new Image(path);
        } catch (error) {
            // Try the next location.
        }
    }
    throw new Error("texture.png not found; tried " + ATLAS_PATHS.join(", "));
}

const texture = loadAtlas();
const ATLAS_COLS = Math.floor(texture.width / TILE);
const ATLAS_ROWS = Math.floor(texture.height / TILE);
const ATLAS_TILES = ATLAS_COLS * ATLAS_ROWS;
const mode = Screen.getMode();
const LAYER_COLS = Math.floor(mode.width / TILE);
const LAYER_ROWS = Math.floor(mode.height / TILE);
const LAYER = LAYER_COLS * LAYER_ROWS;
const MAX_LAYERS = Math.floor(COUNTS[COUNTS.length - 1] / LAYER);
const MAX = MAX_LAYERS * LAYER;

const descriptor = new TileMap.Descriptor({
    textures: [texture],
    materials: [{ endOffset: WORLD * WORLD - 1 }],
    atlas: { tileWidth: TILE, tileHeight: TILE, columns: ATLAS_COLS,
        rows: ATLAS_ROWS },
});

const layerTiles = new Uint16Array(MAX);
for (let i = 0; i < MAX; i++) {
    layerTiles[i] = (i * 7 + Math.floor(i / LAYER) * 13) % ATLAS_TILES;
}
const shiftedTiles = new Uint16Array(MAX);
for (let i = 0; i < MAX; i++) {
    shiftedTiles[i] = (layerTiles[i] + 1) % ATLAS_TILES;
}

// Full-screen layers stacked with a small offset. Built as one tall grid,
// then each layer is moved on screen with the native translate().
function buildLayers() {
    const layers = TileMap.Instance.fromGrid({
        descriptor, columns: LAYER_COLS, rows: LAYER_ROWS * MAX_LAYERS,
        tiles: layerTiles,
    });
    for (let layer = 1; layer < MAX_LAYERS; layer++) {
        layers.translate(layer * LAYER, LAYER, layer % 8,
            -layer * LAYER_ROWS * TILE + (layer % 8));
    }
    return layers;
}

function frame(draw, lines) {
    Screen.clear(0x80101010);
    draw();
    hud(lines, 8, 8);
    Screen.flip();
    meter.tick();
}

// ---------------------------------------------------------------- scenarios

let layers = buildLayers();
const f32 = () => new Float32Array(layers.getSpriteBuffer());
const X = TileMap.layout.offsets.x / 4;
const STRIDE = TileMap.layout.stride / 4;

const scenarios = [
    {
        name: "static",
        about: "one render, buffer untouched",
        setup: (count) => ({ draw: () => layers.render(0, 0, { first: 0, count }) }),
    },
    {
        name: "setTiles",
        about: "every tile id changed per frame (native)",
        setup: (count) => {
            const a = layerTiles.subarray(0, count);
            const b = shiftedTiles.subarray(0, count);
            let tick = 0;
            return {
                update: () => layers.setTiles(0, (tick++ & 8) ? a : b),
                draw: () => layers.render(0, 0, { first: 0, count }),
                done: () => layers.setTiles(0, a),
            };
        },
    },
    {
        name: "translate",
        about: "every sprite moved per frame (native)",
        setup: (count) => {
            let tick = 0;
            return {
                update: () => layers.translate(0, count,
                    (tick++ & 16) ? 1 : -1, 0),
                draw: () => layers.render(0, 0, { first: 0, count }),
                done: () => { layers = buildLayers(); },
            };
        },
    },
    {
        name: "JS move",
        about: "every sprite moved per frame from JS",
        setup: (count) => {
            const pos = f32();
            const base = new Float32Array(count);
            for (let i = 0; i < count; i++) base[i] = pos[i * STRIDE + X];
            let tick = 0;
            return {
                update: () => {
                    const dx = (tick++ & 15) - 8;
                    for (let i = 0, b = X; i < count; i++, b += STRIDE) {
                        pos[b] = base[i] + dx;
                    }
                },
                draw: () => layers.render(0, 0, { first: 0, count }),
                done: () => { layers = buildLayers(); },
            };
        },
    },
];

// `title` is a short first line; `about` goes on its own line.
function measure(title, run, about) {
    const lines = () => [
        "TileMap HUD benchmark",
        title,
        about || "",
        `fps ${meter.fps().toFixed(1)}  worst ${meter.worst().toFixed(1)} ms`,
        `update ${live.update.toFixed(2)} ms  render ${live.render.toFixed(2)} ms`,
        `drawn ${live.drawn}  EE ${memoryMB()} MB`,
        "",
    ].concat(resultLines());
    const live = { update: 0, render: 0, drawn: 0 };
    for (let i = 0; i < WARMUP_FRAMES; i++) {
        if (run.update) run.update();
        frame(run.draw, lines());
    }
    let updateTotal = 0;
    let renderTotal = 0;
    let drawnTotal = 0;
    let worst = 0;
    let frames = 0;
    let last = System.getMilliseconds();
    const start = last;
    while (frames < MEASURE_FRAMES &&
        System.getMilliseconds() - start < MEASURE_MAX_MS) {
        Screen.clear(0x80101010);
        const t0 = System.getMilliseconds();
        if (run.update) run.update();
        const t1 = System.getMilliseconds();
        const drawn = run.draw();
        const t2 = System.getMilliseconds();
        live.update = t1 - t0;
        live.render = t2 - t1;
        live.drawn = drawn;
        updateTotal += live.update;
        renderTotal += live.render;
        drawnTotal += drawn;
        hud(lines(), 8, 8);
        Screen.flip();
        meter.tick();
        const now = System.getMilliseconds();
        worst = Math.max(worst, now - last);
        last = now;
        frames++;
    }
    const elapsed = System.getMilliseconds() - start;
    if (run.done) run.done();
    return {
        fps: frames * 1000 / elapsed,
        update: updateTotal / frames,
        render: renderTotal / frames,
        drawn: Math.round(drawnTotal / frames),
        worst,
    };
}

// Wraps a draw function so it reports how many sprites it queued.
function counting(run, instance) {
    const draw = run.draw;
    run.draw = () => {
        draw();
        return instance().lastDrawCount;
    };
    return run;
}

for (let s = 0; s < scenarios.length; s++) {
    const scenario = scenarios[s];
    let best = null;
    let fail = null;
    for (const wanted of COUNTS) {
        const count = Math.min(wanted, MAX);
        const title = `${s + 1}/${scenarios.length + 1} ${scenario.name} ` +
            `${count} sprites`;
        const r = measure(title, counting(scenario.setup(count), () => layers),
            scenario.about);
        if (r.fps >= TARGET_FPS) {
            best = { count, r };
        } else {
            fail = { count, r };
            break;
        }
        if (r.fps < COLLAPSE_FPS) break;
    }
    const at60 = best ? `${best.count}${fail ? "" : "+"}` : "<1024";
    const next = fail ? `  ${fail.count}: ${fail.r.fps.toFixed(0)}fps` : "";
    results.push({
        text: `${scenario.name.padEnd(9)} 60fps<=${at60}${next}`,
        detail: best ? `upd ${best.r.update.toFixed(1)}  ` +
            `rnd ${best.r.render.toFixed(1)} ms` : "",
    });
}

// World scenario: a 256x256 grid scrolled by the camera, culled and not.
const worldTiles = new Uint16Array(WORLD * WORLD);
for (let i = 0; i < worldTiles.length; i++) {
    worldTiles[i] = ((i % WORLD) * 5 + Math.floor(i / WORLD) * 3) % ATLAS_TILES;
}
layers = null;
const world = TileMap.Instance.fromGrid({
    descriptor, columns: WORLD, rows: WORLD, tiles: worldTiles,
});
const span = { x: WORLD * TILE - mode.width, y: WORLD * TILE - mode.height };
let scroll = 0;
function scrollCamera() {
    scroll++;
    const phase = scroll * 3;
    TileMap.setCamera(-(phase % span.x), -((phase * 0.6) % span.y));
}
for (const cull of [true, false]) {
    const title = `${scenarios.length + 1}/${scenarios.length + 1} world ` +
        `${WORLD}x${WORLD}, cull ${cull ? "on" : "off"}`;
    const r = measure(title, counting({
        update: scrollCamera,
        draw: () => world.render(0, 0, { cull }),
    }, () => world));
    results.push({
        text: `world cull ${cull ? "on " : "off"} ${r.fps.toFixed(1)}fps`,
        detail: `drawn ${r.drawn}  rnd ${r.render.toFixed(2)} ms`,
    });
}

// ---------------------------------------------------------------- live demo

let cull = true;
let switchAt = System.getMilliseconds() + 5000;
while (true) {
    if (System.getMilliseconds() >= switchAt) {
        cull = !cull;
        switchAt += 5000;
    }
    scrollCamera();
    Screen.clear(0x80101010);
    const t0 = System.getMilliseconds();
    world.render(0, 0, { cull });
    const render = System.getMilliseconds() - t0;
    hud([
        "TileMap HUD benchmark - done",
        `live: world, cull ${cull ? "ON" : "OFF"} (5 s toggle)`,
        `fps ${meter.fps().toFixed(1)}  worst ${meter.worst().toFixed(1)} ms`,
        `drawn ${world.lastDrawCount}/${world.spriteCount}  render ${render.toFixed(2)} ms`,
        `EE ${memoryMB()} MB`,
        "",
    ].concat(resultLines()), 8, 8);
    Screen.flip();
    meter.tick();
}
