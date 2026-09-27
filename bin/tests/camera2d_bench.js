// Camera2D benchmark: CPU time per frame of the same scene without a camera,
// with a zoomed camera, turned on VU1, and turned on the EE (TileMap's
// rotatedOnEE diagnostic, the path before the rotated VU1 program).
// Run it on PCSX2 or a console; the results go to the console log and stay
// on screen at the end.
//
// The scene: a 128x64 TileMap grid, 500 Image.drawList sprites, 40 texts and
// 200 Draw.rect, half of it outside the view (culling shows in "culled").

const ATLAS_PATHS = ["tests/texture.png", "texture.png", "bin/tests/texture.png"];
const WARMUP = 30, FRAMES = 120;
const TILE = 16, COLUMNS = 128, ROWS = 64;

function loadAtlas() {
    for (const path of ATLAS_PATHS) {
        try { return new Image(path); } catch (error) { /* next */ }
    }
    throw new Error("texture.png not found; tried " + ATLAS_PATHS.join(", "));
}

const atlas = loadAtlas();
const font = new Font();
const rng = new Random.Generator("bench");
const RECT = Color.new(90, 200, 120, 100);

const atlasCols = Math.floor(atlas.width / TILE), atlasRows = Math.floor(atlas.height / TILE);
const tiles = new Uint16Array(COLUMNS * ROWS);
for (let i = 0; i < tiles.length; i++) tiles[i] = rng.int(0, atlasCols * atlasRows - 1);
const level = TileMap.Instance.fromGrid({
    descriptor: new TileMap.Descriptor({
        textures: [atlas],
        materials: [{ endOffset: COLUMNS * ROWS - 1 }],
        atlas: { tileWidth: TILE, tileHeight: TILE, columns: atlasCols, rows: atlasRows },
    }),
    columns: COLUMNS, rows: ROWS, tiles,
});

// 500 sprites of 64-byte records: x, y, w, h, u1, v1, u2, v2, r, g, b, a.
const SPRITES = 500;
const sprites = new Float32Array(16 * SPRITES);
const channels = new Uint32Array(sprites.buffer);
for (let i = 0; i < SPRITES; i++) {
    const o = 16 * i;
    sprites.set([rng.int(0, COLUMNS * TILE), rng.int(0, ROWS * TILE), 16, 16, 0, 0, 16, 16], o);
    channels.set([128, 128, 128, 128], o + 8);
}
const texts = [], rects = [];
for (let i = 0; i < 40; i++) texts.push({ x: rng.int(0, COLUMNS * TILE), y: rng.int(0, ROWS * TILE) });
for (let i = 0; i < 200; i++) rects.push({ x: rng.int(0, COLUMNS * TILE), y: rng.int(0, ROWS * TILE) });

function scene() {
    level.render(0, 0);
    atlas.drawList(sprites);
    for (const t of texts) font.print(t.x, t.y, "label");
    for (const r of rects) Draw.rect(r.x, r.y, 12, 12, RECT);
}

const cam = new Camera2D.Camera({ x: COLUMNS * TILE / 2, y: ROWS * TILE / 2 });
const scenarios = [
    { name: "no camera", setup() { Camera2D.setCurrent(null); } },
    { name: "zoom 1.5", setup() { cam.zoom = 1.5; cam.rotation = 0; cam.makeCurrent(); } },
    { name: "turned, VU1", setup() { cam.rotation = 0.3; TileMap.setDiagnostics({ rotatedOnEE: false }); } },
    { name: "turned, EE", setup() { TileMap.setDiagnostics({ rotatedOnEE: true }); } },
];

const results = [];
let index = 0, frame = 0, cpu = 0, culled = 0;
scenarios[0].setup();

Loop.run({
    draw() {
        if (index < scenarios.length) {
            scene();
            if (frame >= WARMUP) {
                cpu += Loop.getStats().cpuMs;
                culled += Camera2D.getStats().culled;
            }
            if (++frame === WARMUP + FRAMES) {
                const s = scenarios[index];
                results.push(`${s.name.padEnd(14)} ${(cpu / FRAMES).toFixed(2)} ms  culled ${(culled / FRAMES) | 0}`);
                console.log("camera2d bench: " + results[results.length - 1]);
                frame = cpu = culled = 0;
                if (++index < scenarios.length) scenarios[index].setup();
                else {
                    TileMap.setDiagnostics({ rotatedOnEE: false });
                    Camera2D.setCurrent(null);
                }
            }
            return;
        }
        Camera2D.screenSpace(() => {
            font.print(40, 40, "Camera2D bench (CPU ms per frame)");
            results.forEach((line, i) => font.print(40, 80 + i * 24, line));
            font.print(40, 80 + results.length * 24 + 16, "SELECT+START to leave");
        });
    },
});
