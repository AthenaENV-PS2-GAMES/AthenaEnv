// TileMap functional test. Results are drawn on screen (a real PS2 has no
// console) and also logged. Each section reports PASS or the first failure,
// then the script keeps animating a small map under the results.

const ATLAS_PATHS = ["tests/texture.png", "texture.png", "bin/tests/texture.png"];

function check(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

function expectThrow(callback, type, message) {
    let error;
    try {
        callback();
    } catch (caught) {
        error = caught;
    }
    check(error instanceof type, message);
}

function loadAtlas() {
    for (const path of ATLAS_PATHS) {
        try {
            return { image: new Image(path), path };
        } catch (error) {
            // Try the next location.
        }
    }
    throw new Error("texture.png not found; tried " + ATLAS_PATHS.join(", "));
}

const results = [];
function section(name, body) {
    try {
        body();
        results.push({ ok: true, text: `PASS  ${name}` });
    } catch (error) {
        results.push({ ok: false, text: `FAIL  ${name}: ${error.message}` });
    }
    console.log(results[results.length - 1].text);
}

const layout = TileMap.layout;
const atlas = loadAtlas();
const texture = atlas.image;
const TILE = 16;
const ATLAS_COLS = Math.floor(texture.width / TILE);
const ATLAS_ROWS = Math.floor(texture.height / TILE);
const mode = Screen.getMode();
let map;
let replacement;
let sprites;

section("layout", () => {
    check(layout.stride === 64 && layout.offsets.x === 0 &&
        layout.offsets.u1 === 16 && layout.offsets.r === 32 &&
        layout.offsets.zindex === 56,
        "TileMap.layout does not match the native sprite record");
});

section("descriptor validation", () => {
    expectThrow(() => new TileMap.Descriptor(), TypeError,
        "Descriptor accepted no options");
    expectThrow(() => new TileMap.Descriptor({ materials: [] }), RangeError,
        "Descriptor accepted no materials");
    expectThrow(() => new TileMap.Descriptor({ materials: [{}] }), RangeError,
        "Descriptor accepted a material without endOffset");
    expectThrow(() => new TileMap.Descriptor({
        materials: [{ endOffset: 4 }, { endOffset: 2 }],
    }), RangeError, "Descriptor accepted materials out of order");
    expectThrow(() => new TileMap.Descriptor({
        materials: [{ textureIndex: 0, endOffset: 0 }],
    }), RangeError, "Descriptor accepted a textureIndex without textures");
    expectThrow(() => new TileMap.Descriptor({
        textures: [42], materials: [{ endOffset: 0 }],
    }), TypeError, "Descriptor accepted a non-image texture");
    expectThrow(() => new TileMap.Descriptor({
        textures: ["host:/missing-atlas.png"], materials: [{ endOffset: 0 }],
    }), Error, "Descriptor accepted a missing texture file");
    expectThrow(() => new TileMap.Descriptor({
        materials: [{ endOffset: 0 }],
        atlas: { tileWidth: 0, tileHeight: 16, columns: 4 },
    }), RangeError, "Descriptor accepted a zero atlas tile width");
});

section("sprite buffers and instances", () => {
    const tileW = texture.width / 2;
    const tileH = texture.height / 2;
    const descriptor = new TileMap.Descriptor({
        textures: [texture, atlas.path],
        materials: [
            { endOffset: 11 },
            {
                textureIndex: -1,
                blendMode: Screen.alphaEquation(Screen.ZERO_RGB, Screen.SRC_RGB,
                    Screen.SRC_ALPHA, Screen.DST_RGB, 0),
                endOffset: 13,
            },
        ],
    });
    check(descriptor.materialCount === 2 && descriptor.textures.length === 2 &&
        descriptor.textures[0] === texture &&
        descriptor.textures[1] instanceof Image && descriptor.atlas === undefined,
        "Descriptor did not keep its textures");

    expectThrow(() => TileMap.SpriteBuffer.create(0), RangeError,
        "SpriteBuffer.create accepted zero sprites");
    expectThrow(() => TileMap.SpriteBuffer.fromObjects([{ r: 256 }]), RangeError,
        "SpriteBuffer.fromObjects accepted an out-of-range color");
    expectThrow(() => TileMap.SpriteBuffer.fromObjects([{ x: NaN }]), RangeError,
        "SpriteBuffer.fromObjects accepted a non-finite coordinate");

    sprites = [];
    for (let y = 0; y < 3; y++) {
        for (let x = 0; x < 4; x++) {
            const u = (x % 2) * tileW;
            const v = (y % 2) * tileH;
            sprites.push({
                x: x * tileW, y: y * tileH, w: tileW, h: tileH, zindex: 1,
                u1: u, v1: v, u2: u + tileW, v2: v + tileH,
            });
        }
    }
    sprites.push({ x: 0, y: 3 * tileH + 8, w: 32, h: 16, zindex: 1,
        r: 255, g: 64, b: 64, a: 64 });
    sprites.push({ x: 40, y: 3 * tileH + 8, w: 32, h: 16, zindex: 1,
        r: 64, g: 255, b: 64, a: 64 });

    const buffer = TileMap.SpriteBuffer.fromObjects(sprites);
    const view = new DataView(buffer);
    check(buffer.byteLength === sprites.length * layout.stride &&
        view.getFloat32(layout.stride + layout.offsets.x, true) === tileW &&
        view.getUint32(layout.offsets.r, true) === 128,
        "SpriteBuffer.fromObjects wrote unexpected data");

    expectThrow(() => new TileMap.Instance({ descriptor: {} }), TypeError,
        "Instance accepted a non-descriptor");
    expectThrow(() => new TileMap.Instance({
        descriptor, spriteBuffer: new ArrayBuffer(10),
    }), RangeError, "Instance accepted a partial sprite record");

    map = new TileMap.Instance({ descriptor, spriteBuffer: buffer });
    check(map.spriteCount === sprites.length && map.getSpriteBuffer() === buffer &&
        map.descriptor === descriptor && map.grid === undefined,
        "Instance did not keep its buffer");
    expectThrow(() => map.render(0), TypeError,
        "Instance.render accepted a missing y");
    expectThrow(() => map.render(0, 0, 1), TypeError,
        "Instance.render accepted a non-object third argument");

    const empty = new TileMap.Instance({ descriptor });
    check(empty.spriteCount === 0 && empty.getSpriteBuffer() === undefined,
        "Instance without a buffer reported sprites");
    empty.render(0, 0);
    check(empty.lastDrawCount === 0, "Empty instance reported drawn sprites");

    const patch = TileMap.SpriteBuffer.create(1);
    new DataView(patch).setFloat32(layout.offsets.w, 99, true);
    map.updateSprites(12, patch);
    check(view.getFloat32(12 * layout.stride + layout.offsets.w, true) === 99,
        "Instance.updateSprites did not copy the sprite");
    expectThrow(() => map.updateSprites(sprites.length, patch), RangeError,
        "Instance.updateSprites accepted an offset past the buffer");
    map.updateSprites(12, TileMap.SpriteBuffer.fromObjects([sprites[12]]));

    map.render(0, 0);
    check(map.lastDrawCount === sprites.length,
        `render drew ${map.lastDrawCount} of ${sprites.length} sprites`);
    replacement = TileMap.SpriteBuffer.fromObjects(sprites);
    map.replaceSpriteBuffer(replacement);
    check(map.getSpriteBuffer() === replacement,
        "Instance.replaceSpriteBuffer did not switch buffers");

    TileMap.setCamera(12.5, -4);
    check(TileMap.getCamera().x === 12.5 && TileMap.getCamera().y === -4,
        "TileMap camera was not stored");
    TileMap.setCamera(0, 0);
});

// New in this version: atlas, fromGrid, bulk edits, ranges and culling.
const gridDescriptor = new TileMap.Descriptor({
    textures: [texture],
    materials: [{ endOffset: 128 * 128 - 1 }],
    atlas: { tileWidth: TILE, tileHeight: TILE, columns: ATLAS_COLS,
        rows: ATLAS_ROWS },
});

section("fromGrid", () => {
    const atlasInfo = gridDescriptor.atlas;
    check(atlasInfo.tileWidth === TILE && atlasInfo.columns === ATLAS_COLS &&
        atlasInfo.rows === ATLAS_ROWS, "Descriptor.atlas was not stored");
    expectThrow(() => TileMap.Instance.fromGrid({
        descriptor: map.descriptor, columns: 2, rows: 2,
    }), TypeError, "fromGrid accepted a descriptor without an atlas");
    expectThrow(() => TileMap.Instance.fromGrid({
        descriptor: gridDescriptor, columns: 2, rows: 2, tiles: [0, 1, 2],
    }), RangeError, "fromGrid accepted the wrong number of tile ids");
    expectThrow(() => TileMap.Instance.fromGrid({
        descriptor: gridDescriptor, columns: 1, rows: 1,
        tiles: [ATLAS_COLS * ATLAS_ROWS],
    }), RangeError, "fromGrid accepted a tile id outside the atlas");

    const grid = TileMap.Instance.fromGrid({
        descriptor: gridDescriptor, columns: 3, rows: 2,
        tiles: new Uint16Array([0, 1, ATLAS_COLS + 2, TileMap.EMPTY, 4, 5]),
        zindex: 2,
    });
    const f = new Float32Array(grid.getSpriteBuffer());
    const s = layout.stride / 4;
    const o = (i, key) => f[i * s + layout.offsets[key] / 4];
    check(grid.spriteCount === 6 && grid.grid.columns === 3 &&
        grid.grid.rows === 2 && grid.grid.tileWidth === TILE,
        "fromGrid did not record the grid");
    check(o(1, "x") === TILE && o(4, "y") === TILE && o(1, "u1") === TILE &&
        o(2, "u1") === 2 * TILE && o(2, "v1") === TILE && o(2, "v2") === 2 * TILE &&
        o(0, "zindex") === 2, "fromGrid laid out unexpected cells");
    check(o(3, "w") === 0 && o(3, "h") === 0 && o(4, "w") === TILE,
        "TileMap.EMPTY did not hide its cell");
});

section("bulk edits", () => {
    const grid = TileMap.Instance.fromGrid({
        descriptor: gridDescriptor, columns: 4, rows: 1,
    });
    const f = new Float32Array(grid.getSpriteBuffer());
    const u = new Uint32Array(grid.getSpriteBuffer());
    const s = layout.stride / 4;

    grid.translate(1, 2, 5, -3);
    check(f[s + layout.offsets.x / 4] === TILE + 5 &&
        f[s + layout.offsets.y / 4] === -3 &&
        f[3 * s + layout.offsets.x / 4] === 3 * TILE,
        "translate moved the wrong sprites");
    expectThrow(() => grid.translate(3, 2, 1, 1), RangeError,
        "translate accepted a range past the buffer");

    grid.setColor(0, 4, 10, 20, 30);
    check(u[layout.offsets.r / 4] === 10 && u[3 * s + layout.offsets.b / 4] === 30 &&
        u[layout.offsets.a / 4] === 0x80, "setColor wrote unexpected colors");
    expectThrow(() => grid.setColor(0, 1, 300, 0, 0), RangeError,
        "setColor accepted a channel above 255");

    grid.setTiles(2, [ATLAS_COLS + 1, TileMap.EMPTY]);
    check(f[2 * s + layout.offsets.u1 / 4] === TILE &&
        f[2 * s + layout.offsets.v1 / 4] === TILE &&
        f[3 * s + layout.offsets.w / 4] === 0,
        "setTiles wrote unexpected tiles");
    grid.setTiles(3, new Uint16Array([7]));
    check(f[3 * s + layout.offsets.w / 4] === TILE,
        "setTiles did not restore the size of an EMPTY cell");
    const before = f[layout.offsets.u1 / 4];
    expectThrow(() => grid.setTiles(0, [1, 100000]), RangeError,
        "setTiles accepted an id above 65535");
    expectThrow(() => grid.setTiles(0, [1, ATLAS_COLS * ATLAS_ROWS]), RangeError,
        "setTiles accepted an id outside the atlas");
    check(f[layout.offsets.u1 / 4] === before,
        "setTiles wrote sprites before rejecting an id");
    expectThrow(() => map.setTiles(0, [0]), TypeError,
        "setTiles accepted a descriptor without an atlas");
});

// Mirrors the native culling: visible cells plus one cell of margin.
function expectedVisible(columns, rows, left, top) {
    const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, Math.floor(v)));
    const c0 = clamp(-left / TILE - 1, 0, columns);
    const c1 = clamp((mode.width - left) / TILE + 1, -1, columns - 1);
    const r0 = clamp(-top / TILE - 1, 0, rows);
    const r1 = clamp((mode.height - top) / TILE + 1, -1, rows - 1);
    return c0 > c1 || r0 > r1 ? 0 : (c1 - c0 + 1) * (r1 - r0 + 1);
}

section("render ranges and culling", () => {
    const world = TileMap.Instance.fromGrid({
        descriptor: gridDescriptor, columns: 128, rows: 128,
    });
    world.render(0, 0, { cull: false });
    check(world.lastDrawCount === 128 * 128,
        `cull:false drew ${world.lastDrawCount} sprites`);

    world.render(0, 0);
    const visible = expectedVisible(128, 128, 0, 0);
    check(world.lastDrawCount === visible && visible < 128 * 128,
        `culling drew ${world.lastDrawCount}, expected ${visible}`);

    world.render(-1000, -520);
    const scrolled = expectedVisible(128, 128, -1000, -520);
    check(world.lastDrawCount === scrolled,
        `culling at (-1000, -520) drew ${world.lastDrawCount}, expected ${scrolled}`);

    TileMap.setCamera(-100000, 0);
    world.render(0, 0);
    TileMap.setCamera(0, 0);
    check(world.lastDrawCount === 0, "a grid far off screen was drawn");

    world.render(0, 0, { first: 10, count: 5 });
    check(world.lastDrawCount === 5, "a manual range drew the wrong count");
    world.render(0, 0, { first: 128 * 128 - 1 });
    check(world.lastDrawCount === 1, "first alone did not draw to the end");
    expectThrow(() => world.render(0, 0, { first: 10, count: 128 * 128 }),
        RangeError, "a range past the buffer was accepted");
    expectThrow(() => world.render(0, 0, { cull: 1 }), TypeError,
        "a non-boolean cull was accepted");
});

const failures = results.filter((r) => !r.ok).length;
const summary = failures === 0 ?
    `TileMap tests passed (${results.length} sections)` :
    `TileMap tests FAILED: ${failures} of ${results.length} sections`;
console.log(summary);

// ---------------------------------------------------------------- screen

const hudFont = new Font();
// Small text so every section fits on screen; the line height follows.
hudFont.scale = 0.6;
const HUD_LINE = hudFont.getTextSize("Ag").height + 2;
const HUD_BACK = Color.new(0, 0, 0, 100);
const OK_COLOR = Color.new(128, 255, 128);
const FAIL_COLOR = Color.new(255, 96, 96);
const TEXT_COLOR = Color.new(255, 255, 255);

function hud(lines, x, y) {
    let width = 0;
    for (const line of lines) {
        width = Math.max(width, hudFont.getTextSize(line.text).width);
    }
    Draw.rect(x - 4, y - 2, width + 8, lines.length * HUD_LINE + 4, HUD_BACK);
    for (let i = 0; i < lines.length; i++) {
        hudFont.color = lines[i].color;
        hudFont.print(x, y + i * HUD_LINE, lines[i].text);
    }
}

const frameTimes = [];
let last = System.getMilliseconds();
let time = 0;
const replacementView = replacement ? new DataView(replacement) : null;
while (true) {
    time += 0.05;
    if (replacementView) {
        for (let i = 0; i < 12; i++) {
            replacementView.setFloat32(i * layout.stride + layout.offsets.y,
                sprites[i].y + Math.sin(time + i * 0.5) * 4, true);
        }
    }
    Screen.clear(0x80182030);
    if (map) map.render(64, 200);

    const now = System.getMilliseconds();
    frameTimes.push(now - last);
    if (frameTimes.length > 60) frameTimes.shift();
    last = now;
    const avg = frameTimes.reduce((a, b) => a + b, 0) / frameTimes.length;

    hud([{ text: summary, color: failures ? FAIL_COLOR : OK_COLOR }]
        .concat(results.map((r) => ({
            text: r.text, color: r.ok ? TEXT_COLOR : FAIL_COLOR,
        })))
        .concat([{ text: `fps ${(1000 / avg).toFixed(1)}  atlas ${atlas.path}`,
            color: TEXT_COLOR }]), 8, 8);
    Screen.flip();
}
