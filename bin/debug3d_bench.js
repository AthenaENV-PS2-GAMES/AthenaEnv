// Same endpoints/camera as E00. Run paired builds with profile=false;
// a separate profile=true run diagnoses native phases without mixing timers.
const config = JSON.parse(std.loadFile("world_systems_bench_config.json"));
const metadata = { ...config.metadata, videoMode: Screen.getMode(),
    profile: config.profile === true, timing: "CPU ticks include JS/binding; no explicit GS wait",
    scene: "E00 grid: 50 x 40 short green lines, z=-5; camera origin facing -Z",
    frameScope: "all task frames including warmup, queue, age, teardown and intermediate report conversion" };
if (metadata.profile && typeof Debug3D.stats !== "function") throw new Error("Native phase diagnostics required");
let report = null, visualLogged = false;
function setup() {
    const mode = metadata.videoMode;
    const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
    const camera = new Camera3D.Camera({ aspect: mode.width / height, near: .5, far: 100 });
    camera.setViewport(mode.width, height); camera.lookAt(0, 0, -1).setPosition(0, 0, 0);
    const values = new Float32Array(12000);
    for (let i = 0; i < 2000; i++) {
        const x = (i % 50 - 24.5) * .12, y = (Math.floor(i / 50) - 19.5) * .08;
        values.set([x, y, -5, x + .06, y + .04, -5], i * 6);
    }
    Debug3D.show(true); Debug3D.setCamera(null); Debug3D.clear();
    if (typeof Debug3D.profile === "function") Debug3D.profile(metadata.profile);
    return { camera, values, color: Color.new(80, 220, 100), phases: [], drawn: 0 };
}
function queue(s) {
    Debug3D.clear();
    if (Debug3D.lines(s.values, s.color) !== 2000 || Debug3D.dropped()) throw new Error("Unexpected queue overflow");
}
function teardown(s) { Debug3D.clear(); if (s) s.camera.dispose(); }
const tasks = [
    { name: "debug3d.generate2000", setup, run: queue, afterBatch() { Debug3D.clear(); }, teardown,
        metadata: { lines: 2000, endpointBytes: 48000, phase: "queue from prebuilt endpoints", gsWait: false } },
    { name: "debug3d.draw2000", setup, beforeBatch: queue,
        metadata: { lines: 2000, endpointBytes: 48000, phase: "projection/clipping + GS packets + age; no explicit GS wait", gsWait: false },
        run(s) {
            s.drawn = Debug3D.draw(s.camera, 0);
            if (s.drawn !== 2000) throw new Error("Unexpected clipping");
        },
        afterBatch(s) {
            if (metadata.profile) {
                const stats = Debug3D.stats();
                if (stats.input !== 2000 || stats.emitted !== 2000 || stats.rejected) throw new Error("Invalid native counts");
                s.phases.push(stats); // includes warmup; position preserved, no filtering
            }
            if (config.visual && !visualLogged) { console.log("[Debug3DBench] overlay ready"); visualLogged = true; }
        },
        inspect(s) { return { drawn: s.drawn, phases: s.phases }; }, teardown },
];
const warmup = config.warmup ?? 30, samples = config.samples ?? 120;
metadata.warmup = warmup; metadata.samples = samples; metadata.memoryBefore = System.getMemoryStats();
const capacity = tasks.length * (warmup + samples);
const cpu = new Float32Array(capacity), wall = new Float32Array(capacity);
let first = true, count = 0;
function summarize(values) {
    const sorted = values.slice().sort((a,b) => a-b), n = sorted.length;
    return { averageMs: sorted.reduce((a,b) => a+b, 0)/n, p95Ms: sorted[Math.ceil(n*.95)-1],
        maxMs: sorted[n-1], sampleMs: values, overBudget: values.filter(v => v > 1000/60).length };
}
const observer = { priority: -1000000, realTime: true, preUpdate() {
    if (!first) {
        if (count >= capacity) throw new Error("Frame capacity exceeded");
        const s = Loop.getStats(); cpu[count] = s.cpuMs; wall[count++] = s.frameMs;
    }
    first = false;
    if (report) {
        report.frames = { scope: metadata.frameScope, cpu: summarize(Array.from(cpu.subarray(0,count))),
            wall: summarize(Array.from(wall.subarray(0,count))) };
        report.metadata.memoryAfter = System.getMemoryStats(); Bench.save(report, config.path);
        Loop.removeSystem(observer); Loop.stop(); console.log("[Debug3DBench] completed");
    }
} };
Loop.addSystem(observer);
Bench.run(tasks, { label: "debug3d-e02", metadata, path: config.path, warmup, samples,
    onResult(r) { console.log(`[Debug3DBench] ${r.name}: ${r.averageMs.toFixed(3)} ms, p95 ${r.p95Ms.toFixed(3)}`); },
}).then(r => { report=r; }).catch(e => { console.log("[FAIL] "+e); Loop.removeSystem(observer); Loop.stop(); });
Loop.run(() => {}, { maxDelta: 0 });
