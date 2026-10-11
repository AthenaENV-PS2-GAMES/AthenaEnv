// Configure in an isolated checkout with
// --modules=bench,lod,nav,triggers3d,meshbuilder,debug3d,gltf3d,system,screen,usbmass.
// Supply world_systems_bench_config.json on the writable boot device; see
// docs/benchmarks/README.md. A run is evidence only for the recorded build.
const configText = std.loadFile("world_systems_bench_config.json");
if (!configText) throw new Error("Missing world_systems_bench_config.json; see docs/benchmarks/README.md");
const config = JSON.parse(configText);
if (typeof config.path !== "string" || !config.path.length) throw new Error("Benchmark output path is required");
for (const key of ["platform", "revision", "runtime", "buildOptions", "runId", "variant", "pair", "order", "hashes"])
    if (config.metadata[key] === undefined) throw new Error("Missing benchmark metadata: " + key);
const metadata = config.metadata;
metadata.timing = "CPU, native clock, JS/timer overhead included; GS waits declared per task";
metadata.videoMode = Screen.getMode();
metadata.phaseLimitations = "Debug3D transform/clipping/emission and loader read/parse/construction are combined by current APIs";
function memory() {
    return { ram: System.getMemoryStats(), vramUsed: Screen.getMemoryStats(Screen.VRAM_USED_TOTAL),
        vramFree: Screen.getFreeVRAM() };
}
function releaseAsset(asset) {
    if (!asset) return;
    asset.root.dispose();
    for (const node of asset.nodes) node.dispose();
    for (const name in asset.clips) asset.clips[name].dispose();
}
function lineScene() {
    const mode = metadata.videoMode;
    const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
    const camera = new Camera3D.Camera({ aspect: mode.width / height, near: .5, far: 100 });
    camera.setViewport(mode.width, height);
    camera.lookAt(0, 0, -1).setPosition(0, 0, 0);
    const values = new Float32Array(2000 * 6);
    for (let i = 0; i < 2000; i++) {
        const x = (i % 50 - 24.5) * .12, y = (Math.floor(i / 50) - 19.5) * .08;
        values.set([x, y, -5, x + .06, y + .04, -5], i * 6);
    }
    Debug3D.show(true); Debug3D.setCamera(null); Debug3D.clear();
    return { camera, values, color: Color.new(80, 220, 100), drawn: 0 };
}
function queueLines(s) {
    Debug3D.clear();
    if (Debug3D.lines(s.values, s.color) !== 2000 || Debug3D.dropped()) throw new Error("Debug3D queue overflow");
}
function disposeLines(s) { Debug3D.clear(); if (s) s.camera.dispose(); }

let report = null;
const tasks = [
    { name: "empty", iterations: 100, metadata: { gsWait: false, phase: "overhead" }, run() {} },
    {
        name: "lod.300", metadata: { groups: 300, gsWait: false, phase: "selection" },
        setup() {
            const scene = new Scene3D.Scene(), camera = new Camera3D.Camera();
            const builder = new MeshBuilder.Builder();
            const mesh = builder.box(-.5, -.5, -.5, .5, .5, .5).build()[0];
            builder.dispose();
            const groups = [], nodes = [], root = scene.root;
            for (let i = 0; i < 300; i++) {
                const node = new Scene3D.Node(mesh).setPosition(i % 20, 0, -(i / 20 | 0));
                root.add(node); nodes.push(node);
                groups.push(new LOD.Group(node, [{ mesh, until: 10 }, { mesh, until: 25 }]));
            }
            scene.update();
            root.dispose();
            return { scene, camera, mesh, groups, nodes, frame: 0 };
        },
        run(s) { s.camera.setPosition(0, 0, 10 + s.frame++ % 10); LOD.update(s.camera); },
        inspect(s) { return s && { groups: s.groups.length, memory: memory() }; },
        teardown(s) { if (s) { for (const g of s.groups) g.dispose(); s.scene.dispose(); for (const n of s.nodes) n.dispose(); s.mesh.dispose(); s.camera.dispose(); } },
    },
    {
        name: "nav.maze128", warmup: 3, samples: 30,
        metadata: { width: 128, height: 128, start: [1.5, 1.5], end: [126.5, 126.5], gsWait: false, phase: "synchronous search including JS result allocation" },
        setup() {
            const grid = new Nav.Grid(128, 128);
            for (let x = 8; x < 128; x += 16) {
                const odd = Math.floor(x / 16) % 2;
                grid.fill(x, odd ? 8 : 0, x, odd ? 127 : 119, 0);
            }
            return grid;
        },
        run(grid) { if (!grid.findPath(1.5, 1.5, 126.5, 126.5)) throw new Error("Expected maze path"); },
        teardown(grid) { if (grid) grid.dispose(); },
    },
    {
        name: "triggers.50x20", metadata: { zones: 50, bodies: 20, gsWait: false, phase: "pairs and dispatch" },
        setup() {
            const world = new Triggers3D.World(), bodies = [];
            for (let i = 0; i < 50; i++) world.sphere(i * 3, 0, 0, 1);
            for (let i = 0; i < 20; i++) bodies.push(world.body(i * 7, 0, 0, { radius: .5 }));
            return { world, bodies, frame: 0 };
        },
        run(s) { const f = s.frame++; s.bodies[f % 20].setPosition((f * 1.3) % 150, 0, 0); s.world.update(); },
        teardown(s) { if (s) s.world.dispose(); },
    },
    {
        name: "debug3d.generate2000", setup: lineScene,
        metadata: { lines: 2000, gsWait: false, phase: "bulk queue from reusable endpoints" },
        run: queueLines, afterBatch() { Debug3D.clear(); }, teardown: disposeLines,
    },
    {
        name: "debug3d.draw2000", setup: lineScene, beforeBatch: queueLines,
        metadata: { lines: 2000, endpointBytes: 48000, gsWait: false,
            phase: "transform + near/screen clipping + GS packet emission; no explicit GS completion wait" },
        run(s) { s.drawn = Debug3D.draw(s.camera, 0); if (s.drawn !== 2000) throw new Error("Unexpected clipping"); },
        inspect(s) { return s && { drawn: s.drawn, dropped: Debug3D.dropped(), memory: memory() }; },
        teardown: disposeLines,
    },
];
if (config.loading) {
    const { model, scene, texture } = config.loading;
    for (const asset of [model, scene, texture]) {
        if (!asset || typeof asset.path !== "string" || !asset.path.length || !Number.isInteger(asset.bytes) || asset.bytes <= 0 || asset.bytes > 16 * 1024 * 1024)
            throw new Error("Loading assets require path and size (1..16 MiB)");
        tasks.push({
            name: "loading.read." + asset.path, metadata: { ...asset, gsWait: false,
                phase: "synchronous open/read/close into preallocated bytes; warm filesystem cache" },
            setup() {
                const file = std.open(asset.path, "rb");
                if (!file) throw new Error("Cannot open " + asset.path);
                try {
                    if (file.seek(0, std.SEEK_END) !== 0 || file.tell() !== asset.bytes) throw new Error("Asset size differs from configuration: " + asset.path);
                } finally { file.close(); }
                return { bytes: new ArrayBuffer(asset.bytes) };
            },
            run(s) {
                const file = std.open(asset.path, "rb");
                if (!file) throw new Error("Cannot read " + asset.path);
                try {
                    if (file.read(s.bytes, 0, asset.bytes) !== asset.bytes || file.error()) throw new Error("Short read " + asset.path);
                } finally { file.close(); }
            },
        });
    }
    tasks.push({
        name: "loading.model", metadata: { ...model, gsWait: false, phase: "combined read + parse + native mesh construction" },
        setup() { return { mesh: null }; },
        run(s) { s.mesh = Model3D.load(model.path); },
        afterBatch(s) { if (s.mesh) { s.mesh.dispose(); s.mesh = null; } },
    }, {
        name: "loading.scene", metadata: { ...scene, gsWait: false, phase: "combined read + parse + native construction + JS handles" },
        setup() { return { asset: null }; },
        run(s) { s.asset = GLTF3D.load(scene.path); },
        afterBatch(s) { releaseAsset(s.asset); s.asset = null; },
    }, {
        name: "loading.texture", metadata: { ...texture, gsWait: false, phase: "combined read + decode + CPU texture construction" },
        setup() { return { texture: null }; },
        run(s) { s.texture = Model3D.Texture.load(texture.path); },
        afterBatch(s) { if (s.texture) { s.texture.dispose(); s.texture = null; } },
    }, {
        name: "loading.upload", metadata: { ...texture, gsWait: true, phase: "texture VRAM upload with synchronous GS completion" },
        setup() { return { texture: null, residentMemory: null }; },
        beforeBatch(s) { s.texture = Model3D.Texture.load(texture.path); },
        run(s) { s.texture.upload(); },
        afterBatch(s) { if (s.texture) { try { s.residentMemory = memory(); } finally { s.texture.dispose(); s.texture = null; } } },
        inspect(s) { return s && { memory: s.residentMemory }; },
    });
}
metadata.memoryBefore = memory();
// Observe the previous complete frame, after flip. Preallocated storage
// covers warmup and measured frames, including setup/teardown/checkpoints.
const capacity = tasks.reduce((n, t) => n + (t.warmup ?? config.warmup ?? 30) + (t.samples ?? config.samples ?? 120), 0);
const frameCpu = new Float32Array(capacity), frameWall = new Float32Array(capacity);
let frameSamples = 0, firstFrame = true;
function summary(values) {
    const sorted = values.slice().sort((a, b) => a - b), n = sorted.length;
    return { averageMs: n ? sorted.reduce((a, b) => a + b, 0) / n : 0,
        p95Ms: n ? sorted[Math.ceil(n * .95) - 1] : 0, maxMs: n ? sorted[n - 1] : 0,
        sampleMs: values, overBudget: values.filter(v => v > (config.frameBudgetMs ?? 1000 / 60)).length };
}
const frameObserver = { priority: -1000000, realTime: true, preUpdate() {
    if (!firstFrame) {
        if (frameSamples === capacity) throw new Error("Frame sample capacity exceeded");
        const stats = Loop.getStats();
        frameCpu[frameSamples] = stats.cpuMs; frameWall[frameSamples++] = stats.frameMs;
    }
    firstFrame = false;
    if (report) {
        report.frames = { scope: "all benchmark frames, including warmup/setup/teardown/checkpoints; CPU excludes flip/VSync wait",
            cpu: summary(Array.from(frameCpu.subarray(0, frameSamples))),
            wall: summary(Array.from(frameWall.subarray(0, frameSamples))) };
        report.metadata.memoryAfter = memory();
        Bench.save(report, config.path);
        Loop.removeSystem(frameObserver); Loop.stop();
        console.log("[Bench] report written: " + config.path + " (" + report.status + ")");
    }
} };
Loop.addSystem(frameObserver);
Bench.run(tasks, { label: "world-systems", metadata, path: config.path,
    warmup: config.warmup ?? 30, samples: config.samples ?? 120, frameBudgetMs: config.frameBudgetMs ?? 1000 / 60,
    onResult(r) { console.log(`[Bench] ${r.name}: ${r.averageMs.toFixed(3)} ms, p95 ${r.p95Ms.toFixed(3)}, max ${r.maxMs.toFixed(3)} (${r.status})`); },
}).then(r => { report = r; })
  .catch(e => { console.log("[FAIL] Bench: " + e); Loop.removeSystem(frameObserver); Loop.stop(); });
Loop.run(() => {}, { maxDelta: 0 });
