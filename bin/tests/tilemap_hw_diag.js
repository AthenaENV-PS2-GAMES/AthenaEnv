// TileMap hardware diagnostic.
//
// Draws the same full-screen grid of 16x16 tiles under several renderer
// configurations, switching automatically every few seconds. The current
// mode is printed at the top of the screen and to the console. Note in which
// modes tiles are missing and whether they are always the same ones.
//
// The background is magenta, so a missing tile shows as a magenta square.

// Paths tried in order, relative to the boot directory. On a console the
// script is often copied as main.js next to the atlas, not under tests/.
const ATLAS_PATHS = ["tests/texture.png", "texture.png", "bin/tests/texture.png"];
const TILE = 16;
const TOP = 32;                     // leave room for the label
const SECONDS_PER_MODE = 8;
const BACKGROUND = Color.new(160, 0, 160);

const layout = TileMap.layout;
const FLOATS = layout.stride / 4;
const OFF = {};
for (const key of Object.keys(layout.offsets)) {
    OFF[key] = layout.offsets[key] / 4;
}

function loadAtlas() {
    for (const path of ATLAS_PATHS) {
        try {
            const image = new Image(path);
            console.log(`[TileMap diag] atlas loaded from ${path}`);
            return image;
        } catch (error) {
            // Try the next location.
        }
    }
    throw new Error("texture.png not found; tried " + ATLAS_PATHS.join(", ") +
        ". Copy it next to the script or edit ATLAS_PATHS.");
}

const texture = loadAtlas();
const ATLAS_COLS = Math.floor(texture.width / TILE);
const ATLAS_TILES = ATLAS_COLS * Math.floor(texture.height / TILE);
const mode = Screen.getMode();
const COLS = Math.floor(mode.width / TILE);
const ROWS = Math.floor((mode.height - TOP) / TILE);
const COUNT = COLS * ROWS;

// Six distinct colors, one per VU1 batch of 50 sprites, cycling.
const BATCH_COLORS = [
    [255, 64, 64], [64, 255, 64], [64, 64, 255],
    [255, 255, 64], [64, 255, 255], [255, 255, 255],
];

function tileUV(cell) {
    const tile = cell % ATLAS_TILES;
    return [(tile % ATLAS_COLS) * TILE, Math.floor(tile / ATLAS_COLS) * TILE];
}

function buildGrid(colorByBatch) {
    const buffer = TileMap.SpriteBuffer.create(COUNT);
    const f32 = new Float32Array(buffer);
    const u32 = new Uint32Array(buffer);
    for (let i = 0; i < COUNT; i++) {
        const b = i * FLOATS;
        const [u, v] = tileUV(i);
        f32[b + OFF.x] = (i % COLS) * TILE;
        f32[b + OFF.y] = TOP + Math.floor(i / COLS) * TILE;
        f32[b + OFF.w] = TILE;
        f32[b + OFF.h] = TILE;
        f32[b + OFF.u1] = u;
        f32[b + OFF.v1] = v;
        f32[b + OFF.u2] = u + TILE;
        f32[b + OFF.v2] = v + TILE;
        const color = colorByBatch ?
            BATCH_COLORS[Math.floor(i / 50) % BATCH_COLORS.length] :
            [128, 128, 128];
        // Untextured sprites use the color as is; textured ones modulate,
        // where 0x80 means 1.0.
        u32[b + OFF.r] = colorByBatch ? color[0] : 0x80;
        u32[b + OFF.g] = colorByBatch ? color[1] : 0x80;
        u32[b + OFF.b] = colorByBatch ? color[2] : 0x80;
        u32[b + OFF.a] = 0x80;
        f32[b + OFF.zindex] = 1;
    }
    return buffer;
}

const textured = new TileMap.Instance({
    descriptor: new TileMap.Descriptor({
        textures: [texture],
        materials: [{ endOffset: COUNT - 1 }],
    }),
    spriteBuffer: buildGrid(false),
});
const batches = new TileMap.Instance({
    descriptor: new TileMap.Descriptor({
        materials: [{ endOffset: COUNT - 1 }],
    }),
    spriteBuffer: buildGrid(true),
});

const DEFAULTS = { flushEachBatch: false, fullCacheFlush: false, batchSize: 50 };
const drawImages = () => {
    for (let i = 0; i < COUNT; i++) {
        const [u, v] = tileUV(i);
        texture.draw((i % COLS) * TILE, TOP + Math.floor(i / COLS) * TILE, {
            width: TILE, height: TILE,
            startx: u, starty: v, endx: u + TILE, endy: v + TILE,
        });
    }
};

const modes = [
    { name: "default pipeline, textured", draw: () => textured.render(0, 0) },
    { name: "default pipeline, color = VU1 batch (50)",
        draw: () => batches.render(0, 0) },
    { name: "flushEachBatch", diag: { flushEachBatch: true },
        draw: () => textured.render(0, 0) },
    { name: "fullCacheFlush", diag: { fullCacheFlush: true },
        draw: () => textured.render(0, 0) },
    { name: "batchSize 32", diag: { batchSize: 32 },
        draw: () => textured.render(0, 0) },
    { name: "all safe switches",
        diag: { flushEachBatch: true, fullCacheFlush: true, batchSize: 32 },
        draw: () => textured.render(0, 0) },
    { name: "reference: Image.draw (no VU1)", draw: drawImages },
];

const font = new Font();
font.scale = 0.6;
console.log(`[TileMap diag] ${COLS}x${ROWS} grid = ${COUNT} sprites, ` +
    `${Math.ceil(COUNT / 50)} batches of up to 50`);

let current = -1;
let framesLeft = 0;
while (true) {
    if (framesLeft-- <= 0) {
        current = (current + 1) % modes.length;
        framesLeft = SECONDS_PER_MODE * 60;
        TileMap.setDiagnostics(Object.assign({}, DEFAULTS,
            modes[current].diag || {}));
        console.log(`[TileMap diag] mode ${current + 1}/${modes.length}: ` +
            `${modes[current].name}`);
    }
    Screen.clear(BACKGROUND);
    modes[current].draw();
    font.print(8, 4, `${current + 1}/${modes.length} ${modes[current].name}`);
    Screen.flip();
}
