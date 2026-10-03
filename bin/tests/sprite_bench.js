// Sprite benchmark: CPU time per frame of N animated sprites, three ways:
//   - "JS + Image.draw": the old way (javascript_samples/animation.js),
//     animation state in JavaScript and one Image.draw per sprite;
//   - "Sprite.Instance": one instance per sprite, advanced in C by the Loop,
//     one draw() call per sprite;
//   - "Sprite.drawAll": the same instances drawn by one call, positions in a
//     Float32Array, in one GS packet per 128 sprites;
//   - "Sprite.Animator": one Animator over a TileMap buffer, advanced and
//     written in C, one render() for all.
// Run it on PCSX2 or a console; the results go to the console log and stay
// on screen at the end.

const ATLAS_PATHS = ["tests/texture.png", "texture.png", "bin/tests/texture.png"];
const WARMUP = 30, FRAMES = 120;
const COUNT = 500, CELL = 32, FPS = 12;

function loadAtlas() {
    for (const path of ATLAS_PATHS) {
        try { return new Image(path); } catch (error) { /* next */ }
    }
    throw new Error("texture.png not found; tried " + ATLAS_PATHS.join(", "));
}

const atlas = loadAtlas();
const font = new Font();
const rng = new Random.Generator("sprite-bench");
const { width: SCREEN_W, height: SCREEN_H } = Screen.getMode();
const positions = [];
for (let i = 0; i < COUNT; i++)
    positions.push({ x: rng.int(0, SCREEN_W - CELL), y: rng.int(0, SCREEN_H - CELL) });

const sheet = Sprite.Sheet.fromGrid(atlas, { frameWidth: CELL, frameHeight: CELL,
    clips: { spin: { frames: "0-7", fps: FPS } } });

// 1. Animation state in JavaScript, as the old sample did it.
const jsStates = positions.map(() => ({ frame: rng.int(0, 7), timer: 0 }));
const columns = Math.floor(atlas.width / CELL);
function jsScene(dt) {
    for (let i = 0; i < COUNT; i++) {
        const s = jsStates[i];
        s.timer += dt;
        if (s.timer >= 1 / FPS) {
            s.timer -= 1 / FPS;
            s.frame = (s.frame + 1) % 8;
        }
        const u = (s.frame % columns) * CELL, v = Math.floor(s.frame / columns) * CELL;
        atlas.draw(positions[i].x, positions[i].y, { width: CELL, height: CELL,
            startx: u, starty: v, endx: u + CELL, endy: v + CELL });
    }
}

// 2. One Sprite.Instance per sprite.
let instances = [];
function instanceScene() {
    for (let i = 0; i < COUNT; i++) instances[i].draw(positions[i].x, positions[i].y);
}

// 2b. The same instances, one call.
const packed = new Float32Array(COUNT * 2);
positions.forEach((p, i) => { packed[2 * i] = p.x; packed[2 * i + 1] = p.y; });
function drawAllScene() { Sprite.drawAll(instances, packed); }

// 3. One Animator over a TileMap buffer.
const buffer = TileMap.SpriteBuffer.create(COUNT);
const floats = new Float32Array(buffer), words = new Uint32Array(buffer);
const stride = TileMap.layout.stride / 4;
for (let i = 0; i < COUNT; i++) {
    floats.set([positions[i].x, positions[i].y, CELL, CELL], i * stride);
    words.set([128, 128, 128, 128], i * stride + TileMap.layout.offsets.r / 4);
}
const coins = new TileMap.Instance({
    descriptor: new TileMap.Descriptor({ textures: [atlas], materials: [{ endOffset: COUNT - 1 }] }),
    spriteBuffer: buffer,
});
let animator = null;
function animatorScene() { coins.render(0, 0); }

const scenarios = [
    { name: "JS + Image.draw", scene: jsScene, setup() {} },
    {
        name: "Sprite.Instance", scene: instanceScene, setup() {
            instances = positions.map(() => new Sprite.Instance(sheet, { clip: "spin" }));
            instances.forEach(s => s.position = rng.int(0, 7));
        },
    },
    { name: "Sprite.drawAll", scene: drawAllScene, setup() {} },
    {
        name: "Sprite.Animator", scene: animatorScene, setup() {
            instances.forEach(s => s.stop());
            instances = [];
            animator = Sprite.Animator.bind(coins, sheet, "spin", { randomStart: true });
        },
    },
];

const results = [];
let index = 0, frame = 0, cpu = 0;
scenarios[0].setup();

Loop.run({
    draw(alpha) {
        if (index < scenarios.length) {
            scenarios[index].scene(Loop.getDeltaTime());
            if (frame >= WARMUP) cpu += Loop.getStats().cpuMs;
            if (++frame === WARMUP + FRAMES) {
                results.push(`${scenarios[index].name.padEnd(16)} ${(cpu / FRAMES).toFixed(2)} ms`);
                console.log(`sprite bench (${COUNT} sprites): ` + results[results.length - 1]);
                frame = cpu = 0;
                if (++index < scenarios.length) scenarios[index].setup();
                else if (animator) animator.unbind();
            }
            return;
        }
        font.print(40, 40, `Sprite bench: ${COUNT} animated sprites (CPU ms per frame)`);
        results.forEach((line, i) => font.print(40, 80 + i * 24, line));
        font.print(40, 80 + results.length * 24 + 16, "SELECT+START to leave");
    },
});
