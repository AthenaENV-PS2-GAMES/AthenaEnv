// TileMap load test.
//
// Part 1 forces edge cases (oversized materials, freed textures, buffer
// swaps every frame, streaming, camera scrolling) and checks nothing breaks.
// Part 2 measures how many 16x16 tiles fit in a frame for several scenarios:
// it doubles the sprite count until the frame rate collapses, then binary
// searches the largest count that still holds 60 fps.

const ATLAS = "tests/texture.png";
const TILE = 16;
const MAX_SPRITES = 65536;          // 4 MB of sprite records
const WARMUP_FRAMES = 10;
const MEASURE_FRAMES = 90;
const TARGET_FPS = 59;              // NTSC vsync caps at ~59.94
const COLLAPSE_FPS = 15;            // stop ramping below this
const SEARCH_STEPS = 5;
const SEARCH_GRANULARITY = 256;

function check(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

const layout = TileMap.layout;
const FLOATS = layout.stride / 4;
const OFF_X = layout.offsets.x / 4;
const OFF_Y = layout.offsets.y / 4;
const OFF_W = layout.offsets.w / 4;
const OFF_H = layout.offsets.h / 4;
const OFF_U1 = layout.offsets.u1 / 4;
const OFF_V1 = layout.offsets.v1 / 4;
const OFF_U2 = layout.offsets.u2 / 4;
const OFF_V2 = layout.offsets.v2 / 4;
const OFF_R = layout.offsets.r / 4;
const OFF_Z = layout.offsets.zindex / 4;

const texture = new Image(ATLAS);
const ATLAS_COLS = Math.floor(texture.width / TILE);
const ATLAS_ROWS = Math.floor(texture.height / TILE);
const ATLAS_TILES = ATLAS_COLS * ATLAS_ROWS;
const mode = Screen.getMode();
const GRID_COLS = Math.floor(mode.width / TILE);
const GRID_ROWS = Math.floor(mode.height / TILE);
const LAYER = GRID_COLS * GRID_ROWS;

console.log(`[TileMap stress] atlas ${texture.width}x${texture.height} ` +
    `(${ATLAS_TILES} tiles), screen grid ${GRID_COLS}x${GRID_ROWS} ` +
    `= ${LAYER} tiles per layer`);

// Fills sprites [first, first + count) as full-screen layers of 16x16 tiles.
// Each layer is shifted a little so stacked layers stay visible.
function fillGrid(buffer, first, count) {
    const f32 = new Float32Array(buffer);
    const u32 = new Uint32Array(buffer);
    for (let i = first; i < first + count; i++) {
        const layer = Math.floor(i / LAYER);
        const cell = i % LAYER;
        const tile = (cell * 7 + layer * 13) % ATLAS_TILES;
        const u = (tile % ATLAS_COLS) * TILE;
        const v = Math.floor(tile / ATLAS_COLS) * TILE;
        const b = i * FLOATS;
        f32[b + OFF_X] = (cell % GRID_COLS) * TILE + (layer % 8);
        f32[b + OFF_Y] = Math.floor(cell / GRID_COLS) * TILE + (layer % 8);
        f32[b + OFF_W] = TILE;
        f32[b + OFF_H] = TILE;
        f32[b + OFF_U1] = u;
        f32[b + OFF_V1] = v;
        f32[b + OFF_U2] = u + TILE;
        f32[b + OFF_V2] = v + TILE;
        u32[b + OFF_R] = 0x80;
        u32[b + OFF_R + 1] = 0x80;
        u32[b + OFF_R + 2] = 0x80;
        u32[b + OFF_R + 3] = 0x80;
        f32[b + OFF_Z] = 1;
    }
}

function frame(draw) {
    Screen.clear(0x80101010);
    draw();
    Screen.flip();
}

// ---------------------------------------------------------------------
// Part 1: robustness under forced edge cases.
// ---------------------------------------------------------------------

const baseDescriptor = new TileMap.Descriptor({
    textures: [texture],
    materials: [{ endOffset: MAX_SPRITES - 1 }],
});

// A material range far past the buffer must be clamped, not read.
{
    const small = TileMap.SpriteBuffer.create(LAYER);
    fillGrid(small, 0, LAYER);
    const oversized = new TileMap.Descriptor({
        textures: [texture],
        materials: [
            { endOffset: 10 },
            { textureIndex: -1, endOffset: 1000000 },
        ],
    });
    const map = new TileMap.Instance({ descriptor: oversized, spriteBuffer: small });
    for (let i = 0; i < 5; i++) {
        frame(() => map.render(0, 0));
    }
}

// Many tiny materials: 1 sprite each, alternating texture and blend.
{
    const count = 600;
    const buffer = TileMap.SpriteBuffer.create(count);
    fillGrid(buffer, 0, count);
    const materials = [];
    const additive = Screen.alphaEquation(Screen.SRC_RGB, Screen.ZERO_RGB,
        Screen.SRC_ALPHA, Screen.DST_RGB, 0);
    for (let i = 0; i < count; i++) {
        materials.push(i % 3 === 0 ? { textureIndex: -1, endOffset: i } :
            i % 3 === 1 ? { blendMode: additive, endOffset: i } :
                { endOffset: i });
    }
    const map = new TileMap.Instance({
        descriptor: new TileMap.Descriptor({ textures: [texture], materials }),
        spriteBuffer: buffer,
    });
    for (let i = 0; i < 5; i++) {
        frame(() => map.render(0, 0));
    }
}

// A freed texture skips its material instead of drawing a dangling surface.
{
    const doomed = new Image(ATLAS);
    const buffer = TileMap.SpriteBuffer.create(LAYER);
    fillGrid(buffer, 0, LAYER);
    const map = new TileMap.Instance({
        descriptor: new TileMap.Descriptor({
            textures: [doomed],
            materials: [{ endOffset: LAYER - 1 }],
        }),
        spriteBuffer: buffer,
    });
    frame(() => map.render(0, 0));
    doomed.free();
    for (let i = 0; i < 5; i++) {
        frame(() => map.render(0, 0));
    }
}

// Swap buffers several times per frame, including after rendering.
{
    const a = TileMap.SpriteBuffer.create(LAYER);
    const b = TileMap.SpriteBuffer.create(LAYER);
    fillGrid(a, 0, LAYER);
    fillGrid(b, 0, LAYER);
    const map = new TileMap.Instance({ descriptor: baseDescriptor, spriteBuffer: a });
    for (let i = 0; i < 30; i++) {
        frame(() => {
            map.render(0, 0);
            map.replaceSpriteBuffer(i % 2 ? a : b);
            map.render(4, 4);
            map.replaceSpriteBuffer(TileMap.SpriteBuffer.create(1));
            map.replaceSpriteBuffer(i % 2 ? b : a);
        });
    }
    check(map.spriteCount === LAYER, "Buffer swaps left an unexpected buffer");
}

// Stream a large map in chunks through updateSprites and a typed-array view.
{
    const total = LAYER * 4;
    const source = TileMap.SpriteBuffer.create(total);
    fillGrid(source, 0, total);
    const target = TileMap.SpriteBuffer.create(total);
    const map = new TileMap.Instance({ descriptor: baseDescriptor, spriteBuffer: target });
    const chunk = 512;
    for (let offset = 0; offset < total; offset += chunk) {
        const count = Math.min(chunk, total - offset);
        map.updateSprites(offset,
            new Uint8Array(source, offset * layout.stride, count * layout.stride));
        frame(() => map.render(0, 0));
    }
    const a = new Uint32Array(source);
    const b = new Uint32Array(target);
    for (let i = 0; i < a.length; i++) {
        check(a[i] === b[i], "updateSprites streaming corrupted sprite data");
    }
}

// Scroll a 128x128 tile world with the camera, wrapping around it.
{
    const worldCols = 128;
    const world = TileMap.SpriteBuffer.create(worldCols * worldCols);
    const f32 = new Float32Array(world);
    const u32 = new Uint32Array(world);
    for (let i = 0; i < worldCols * worldCols; i++) {
        const tile = (i * 5) % ATLAS_TILES;
        const b = i * FLOATS;
        f32[b + OFF_X] = (i % worldCols) * TILE;
        f32[b + OFF_Y] = Math.floor(i / worldCols) * TILE;
        f32[b + OFF_W] = TILE;
        f32[b + OFF_H] = TILE;
        f32[b + OFF_U1] = (tile % ATLAS_COLS) * TILE;
        f32[b + OFF_V1] = Math.floor(tile / ATLAS_COLS) * TILE;
        f32[b + OFF_U2] = f32[b + OFF_U1] + TILE;
        f32[b + OFF_V2] = f32[b + OFF_V1] + TILE;
        u32[b + OFF_R] = u32[b + OFF_R + 1] = u32[b + OFF_R + 2] =
            u32[b + OFF_R + 3] = 0x80;
        f32[b + OFF_Z] = 1;
    }
    const map = new TileMap.Instance({ descriptor: baseDescriptor, spriteBuffer: world });
    for (let i = 0; i < 60; i++) {
        TileMap.setCamera(-((i * 37) % (worldCols * TILE - mode.width)),
            -((i * 23) % (worldCols * TILE - mode.height)));
        frame(() => map.render(0, 0));
    }
    TileMap.setCamera(0, 0);
}

// Instances collected by the GC right after rendering must not free memory
// that queued DMA still reads.
for (let i = 0; i < 20; i++) {
    frame(() => {
        for (let j = 0; j < 4; j++) {
            const temporary = TileMap.SpriteBuffer.create(LAYER);
            fillGrid(temporary, 0, LAYER);
            new TileMap.Instance({
                descriptor: baseDescriptor, spriteBuffer: temporary,
            }).render(j, j);
        }
        if (typeof std.gc === "function") std.gc();
    });
}

console.log("[TileMap stress] robustness checks passed");

// ---------------------------------------------------------------------
// Part 2: throughput.
// ---------------------------------------------------------------------

const master = TileMap.SpriteBuffer.create(MAX_SPRITES);
const pristine = TileMap.SpriteBuffer.create(MAX_SPRITES);
console.log(`[TileMap stress] filling ${MAX_SPRITES} sprites...`);
fillGrid(pristine, 0, MAX_SPRITES);
new Uint8Array(master).set(new Uint8Array(pristine));
const masterF32 = new Float32Array(master);
const pristineF32 = new Float32Array(pristine);

function view(count) {
    return new Uint8Array(master, 0, count * layout.stride);
}

// Materials of 256 sprites cycling textured, additive and untextured draws.
function materialDescriptor(count) {
    const materials = [];
    const additive = Screen.alphaEquation(Screen.SRC_RGB, Screen.ZERO_RGB,
        Screen.SRC_ALPHA, Screen.DST_RGB, 0);
    for (let end = 255, i = 0; end < count + 255; end += 256, i++) {
        const last = Math.min(end, count - 1);
        materials.push(i % 3 === 0 ? { endOffset: last } :
            i % 3 === 1 ? { blendMode: additive, endOffset: last } :
                { textureIndex: -1, endOffset: last });
    }
    return new TileMap.Descriptor({ textures: [texture], materials });
}

const scenarios = [
    {
        name: "static",
        about: "one instance, one material, buffer untouched",
        setup(count) {
            const map = new TileMap.Instance({
                descriptor: baseDescriptor, spriteBuffer: view(count),
            });
            return { draw: () => map.render(0, 0) };
        },
    },
    {
        name: "animated",
        about: "one instance, every sprite moved from JS each frame",
        setup(count) {
            const map = new TileMap.Instance({
                descriptor: baseDescriptor, spriteBuffer: view(count),
            });
            let tick = 0;
            return {
                update: () => {
                    const dx = (tick++ & 15) - 8;
                    for (let i = 0, b = 0; i < count; i++, b += FLOATS) {
                        masterF32[b + OFF_X] = pristineF32[b + OFF_X] + dx;
                    }
                },
                draw: () => map.render(0, 0),
                done: () => new Uint8Array(master, 0, count * layout.stride)
                    .set(new Uint8Array(pristine, 0, count * layout.stride)),
            };
        },
    },
    {
        name: "materials",
        about: "one instance, a material switch every 256 sprites",
        setup(count) {
            const map = new TileMap.Instance({
                descriptor: materialDescriptor(count), spriteBuffer: view(count),
            });
            return { draw: () => map.render(0, 0) };
        },
    },
    {
        name: "instances",
        about: "one instance per full-screen layer, shared descriptor",
        setup(count) {
            const maps = [];
            for (let first = 0; first < count; first += LAYER) {
                const n = Math.min(LAYER, count - first);
                maps.push(new TileMap.Instance({
                    descriptor: baseDescriptor,
                    spriteBuffer: new Uint8Array(master,
                        first * layout.stride, n * layout.stride),
                }));
            }
            return { draw: () => maps.forEach((map) => map.render(0, 0)) };
        },
    },
];

function measure(scenario, count) {
    const run = scenario.setup(count);
    for (let i = 0; i < WARMUP_FRAMES; i++) {
        if (run.update) run.update();
        frame(run.draw);
    }
    let updateTotal = 0;
    let renderTotal = 0;
    let renderWorst = 0;
    let frameWorst = 0;
    let last = System.getMilliseconds();
    const start = last;
    for (let i = 0; i < MEASURE_FRAMES; i++) {
        Screen.clear(0x80101010);
        const t0 = System.getMilliseconds();
        if (run.update) run.update();
        const t1 = System.getMilliseconds();
        run.draw();
        const t2 = System.getMilliseconds();
        Screen.flip();
        const now = System.getMilliseconds();
        updateTotal += t1 - t0;
        renderTotal += t2 - t1;
        renderWorst = Math.max(renderWorst, t2 - t1);
        frameWorst = Math.max(frameWorst, now - last);
        last = now;
    }
    const elapsed = System.getMilliseconds() - start;
    if (run.done) run.done();
    const result = {
        count,
        fps: MEASURE_FRAMES * 1000 / elapsed,
        update: updateTotal / MEASURE_FRAMES,
        render: renderTotal / MEASURE_FRAMES,
        renderWorst,
        frameWorst,
    };
    console.log(`[TileMap stress] ${scenario.name} sprites=${count} ` +
        `fps=${result.fps.toFixed(1)} ` +
        `update=${result.update.toFixed(2)}ms ` +
        `render avg=${result.render.toFixed(2)}ms ` +
        `worst=${result.renderWorst.toFixed(2)}ms ` +
        `frame worst=${result.frameWorst.toFixed(2)}ms`);
    return result;
}

function roundDown(value) {
    return Math.max(SEARCH_GRANULARITY,
        Math.floor(value / SEARCH_GRANULARITY) * SEARCH_GRANULARITY);
}

const summary = [];
for (const scenario of scenarios) {
    console.log(`[TileMap stress] --- ${scenario.name}: ${scenario.about}`);
    let pass = null;
    let fail = null;
    let best30 = null;
    for (let count = 1024; count <= MAX_SPRITES; count *= 2) {
        const result = measure(scenario, count);
        if (result.fps >= 29) best30 = count;
        if (result.fps >= TARGET_FPS) {
            pass = count;
        } else if (fail === null) {
            fail = count;
        }
        if (result.fps < COLLAPSE_FPS) break;
    }
    // Binary search the 60 fps limit between the last pass and first fail.
    if (pass !== null && fail !== null) {
        let low = pass;
        let high = fail;
        for (let step = 0; step < SEARCH_STEPS; step++) {
            const middle = roundDown((low + high) / 2);
            if (middle <= low || middle >= high) break;
            if (measure(scenario, middle).fps >= TARGET_FPS) {
                low = middle;
            } else {
                high = middle;
            }
        }
        pass = low;
    }
    summary.push({ scenario: scenario.name, at60: pass, at30: best30,
        capped: fail === null });
}

console.log("[TileMap stress] ===== summary (16x16 tiles) =====");
for (const entry of summary) {
    const at60 = entry.at60 === null ? "< 1024" :
        `${entry.at60}${entry.capped ? "+ (test cap)" : ""}`;
    const at30 = entry.at30 === null ? "< 1024" :
        `>= ${entry.at30}${entry.capped ? "+" : ""}`;
    console.log(`[TileMap stress] ${entry.scenario}: 60 fps up to ${at60} ` +
        `sprites (${entry.at60 ? (entry.at60 / LAYER).toFixed(1) : 0} ` +
        `screens), 30 fps ${at30}`);
}
console.log("[TileMap stress] done");

// Keep showing the largest static load that held 60 fps.
const showcase = summary[0].at60 || 1024;
const finalMap = new TileMap.Instance({
    descriptor: baseDescriptor, spriteBuffer: view(showcase),
});
while (true) {
    frame(() => finalMap.render(0, 0));
}
