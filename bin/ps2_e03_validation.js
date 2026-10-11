/* E03 validation for a physical PS2 (also runs on PCSX2).
 *
 * Writes `e03_ps2_validation.log` (human/parseable text) next to the ELF, rewritten
 * after every section so a crash or a power cycle still leaves the finished parts.
 * Sections: SYSTEM, IO (device read speed), FUNCTIONAL (PASS/FAIL checks),
 * MEMORY (RAM of copy vs borrow, leak check), BENCH (atomic vs stepped, budgets 0 and 8).
 * Optional `ps2_e03_config.json`: { "quick": true } for a short run (about 2-3 minutes),
 * { "skip": ["bench"] } to skip sections, { "samples": N, "warmup": N }.
 * Return the .log file (and e03_ps2_results.json when present). */
import * as Loop from "Loop";
import * as Assets3D from "Assets3D";
import { Assets } from "Scene";
import { ticks, ticksToMilliseconds } from "ProfilerNative";

const LOG = "e03_ps2_validation.log", JSON_OUT = "e03_ps2_results.json";
let config = {};
try { const text = std.loadFile("ps2_e03_config.json"); if (text) config = JSON.parse(text); } catch (_) { /* optional */ }
const quick = !!config.quick, skip = new Set(config.skip ?? []);
const D0 = Date.now(), lines = [], results = { version: 1, checks: [], io: {}, memory: {}, bench: [] };
/* Timestamps use Date.now(): the COP0 tick counter used for the spans wraps every 14.5 s. */
const sinceMs = () => Date.now() - D0;
function log(text) { const line = `[${(sinceMs() / 1000).toFixed(3)}] ${text}`; lines.push(line); console.log(line); }
let flushFailed = false;
function writeFile(path, text) {
    const f = std.open(path, "w");
    if (!f) throw new Error(`cannot open ${path}`);
    try { f.puts(text); f.flush(); if (f.error()) throw new Error(`cannot write ${path}`); } finally { f.close(); }
}
function flush() {
    try { writeFile(LOG, lines.join("\n") + "\n"); writeFile(JSON_OUT, JSON.stringify(results)); }
    catch (e) { if (!flushFailed) console.log("LOG WRITE FAILED: " + e); flushFailed = true; }
}
const ms = start => ticksToMilliseconds((ticks() - start) >>> 0);
/* Interval from a { t, d } stamp: COP0 ticks when short (precise), Date.now() once ticks may have wrapped. */
const stamp = () => ({ t: ticks(), d: Date.now() });
const since = st => { const wall = Date.now() - st.d; return wall > 12000 ? wall : ms(st.t); };
let failures = 0;
function check(name, ok, detail = "") { if (!ok) failures++; results.checks.push({ name, ok: !!ok }); log(`${ok ? "PASS" : "FAIL"} ${name}${detail ? " (" + detail + ")" : ""}`); }
const mem = () => { try { return System.getMemoryStats(); } catch (_) { return {}; } };
const wait = n => new Promise(r => setTimeout(r, n));
const GL = GLTF3D, MD = Model3D; // keep the real modules: the atomic mode shadows the globals
const realGLTF3D = { load: GL.load, loadMemory: GL.loadMemory };
const realModel3D = { load: MD.load, loadMemory: MD.loadMemory, Texture: MD.Texture, Mesh: MD.Mesh };
const gltfStepped = GL, modelStepped = MD;

async function readTimed(path) {
    const start = stamp(), buffer = await Thread.readFileAsync(path);
    return { buffer, bytes: new Uint8Array(buffer), ms: since(start) };
}
function free(asset) {
    for (const c of Object.values(asset.clips || {})) c.dispose();
    for (const n of asset.nodes || []) n.dispose();
    asset.root.dispose();
}
function sameAsset(a, b) {
    return a.nodes.length === b.nodes.length && a.names.join() === b.names.join() &&
        Object.keys(a.clips).join() === Object.keys(b.clips).join() && a.root.childCount === b.root.childCount;
}
function throws(fn) { try { fn(); } catch (_) { return true; } return false; }

async function systemSection() {
    log("SECTION SYSTEM");
    const m = mem();
    log(`modules: Assets3D=${typeof Assets3D.update} GLTF3D.beginMemory=${typeof gltfStepped.beginMemory} Model3D.beginMemory=${typeof modelStepped.beginMemory} Thread=${typeof Thread}`);
    log(`ticksPerMs=${(1 / ticksToMilliseconds(1)).toFixed(1)} videoMode=${typeof Screen !== "undefined" ? JSON.stringify(Screen.getMode()) : "n/a"} quick=${quick}`);
    log(`memory: ${JSON.stringify(m)}`);
    log(`cwd files: models/lit_cube.glb=${!!std.loadFile("models/lit_cube.glb")}`);
    results.system = { memory: m, quick };
    flush();
}

async function ioSection() {
    log("SECTION IO (Thread.readFileAsync wall time; a mass-storage device is much slower than PCSX2)");
    for (const file of ["lit_cube.glb", "bend_hi.glb", "grid_mesh.glb", "arm_read.glb", "checker.png", "ext_bend.gltf", "ext bend.bin"]) {
        try {
            const r = await readTimed("models/" + file);
            const mbps = r.bytes.length / 1048576 / (r.ms / 1000);
            log(`IO file=${file} bytes=${r.bytes.length} ms=${r.ms.toFixed(2)} MiB/s=${mbps.toFixed(2)}`);
            results.io[file] = { bytes: r.bytes.length, ms: r.ms };
        } catch (e) { check("read " + file, false, String(e)); }
    }
    flush();
}

async function functionalSection() {
    log("SECTION FUNCTIONAL");
    const arm = await readTimed("models/arm.glb"), cube = await readTimed("models/lit_cube.glb");
    const whole = realGLTF3D.loadMemory(arm.bytes, "arm.glb"), disk = realGLTF3D.load("models/arm.glb");
    check("GLTF3D.loadMemory equals load(path)", sameAsset(whole, disk));
    // stepped variants
    for (const [label, options, budget] of [["snapshot copy, step()", undefined, null], ["borrow, run(0)", { borrow: true }, 0], ["borrow, run(8)", { borrow: true }, 8]]) {
        const l = gltfStepped.beginMemory(arm.bytes, "arm.glb", undefined, options); let calls = 0;
        if (budget === null) { while (!l.step()) calls++; } else { while (!l.run(budget)) calls++; }
        const a = l.finish(); check(`GLTF3D.Loader ${label} equals one-shot`, sameAsset(a, whole), `calls=${calls}`); free(a);
    }
    free(whole); free(disk);
    const meshWhole = realModel3D.loadMemory(cube.bytes, "cube.glb"), ml = modelStepped.beginMemory(cube.bytes, "cube.glb", undefined, { borrow: true });
    while (!ml.run(8)); const mesh = ml.finish();
    check("Model3D.Loader equals loadMemory", mesh.vertexCount === meshWhole.vertexCount, `vertices=${mesh.vertexCount}`); mesh.dispose(); meshWhole.dispose();
    // explicit resolver
    const files = {};
    async function resolve(loader, dir) {
        let guard = 0;
        while (!loader.step()) {
            if (loader.phase !== "dependencies") { if (++guard > 100000) throw new Error("no progress"); continue; }
            const deps = loader.dependencies, next = deps.findIndex(d => !d.provided);
            if (next < 0) continue;
            const bytes = (await readTimed(dir + deps[next].uri)).bytes; files[deps[next].uri] = bytes; loader.provide(next, bytes);
        }
    }
    for (const name of ["ext_bend.gltf", "memory_external_image.glb"]) {
        try {
            const source = await readTimed("models/" + name), l = gltfStepped.beginMemory(source.bytes, name, undefined, { dependencies: true, borrow: true });
            await resolve(l, "models/"); const a = l.finish(), d = realGLTF3D.load("models/" + name);
            check(`resolver: ${name} equals disk loader`, sameAsset(a, d), `files=${Object.keys(files).length}`); free(a); free(d);
        } catch (e) { check(`resolver: ${name}`, false, String(e)); }
    }
    try {
        const source = await readTimed("models/ext_textured.gltf"), l = modelStepped.beginMemory(source.bytes, "ext_textured.gltf", undefined, { dependencies: true });
        await resolve(l, "models/"); const m = l.finish(), d = realModel3D.load("models/ext_textured.gltf");
        check("resolver: Model3D ext_textured.gltf equals disk loader", m.vertexCount === d.vertexCount); m.dispose(); d.dispose();
    } catch (e) { check("resolver: Model3D ext_textured.gltf", false, String(e)); }
    // errors
    check("truncated GLB is rejected", throws(() => realGLTF3D.loadMemory(arm.bytes.subarray(0, arm.bytes.length - 3), "cut.glb")));
    const bad = arm.bytes.slice(); bad[0] ^= 0xff; const bl = gltfStepped.beginMemory(bad, "bad.glb");
    check("bad magic fails and keeps failing", throws(() => { while (!bl.step()); }) && throws(() => bl.step())); bl.dispose();
    const strict = gltfStepped.beginMemory((await readTimed("models/ext_bend.gltf")).bytes, "x.gltf");
    check("strict loader refuses external files", throws(() => { while (!strict.step()); })); strict.dispose();
    flush();
    // Assets3D integration
    Assets3D.setBudget(8);
    async function acquire(spec, label) {
        const g = Assets.acquire(spec); let frames = 0;
        const start = stamp();
        while (!g.done && !g.failed && frames < 20000) { Assets3D.update(); Assets.update(0); await wait(1); frames++; }
        const stat = g.failed ? "failed" : g.done ? "ok" : "timeout";
        log(`ASSETS ${label}: ${stat} wall=${since(start).toFixed(1)}ms updates=${frames}`);
        return g;
    }
    let g = await acquire({ gltf: { a: "models/bend_hi.glb" } }, "gltf stepped");
    check("Assets3D gltf stepped", g.done && Assets3D.stats().lastLoad.mode === "job-memory-steps"); g.release(); await wait(20); Assets.update(0);
    g = await acquire({ gltf: { a: { path: "models/bend_hi.glb", maxBytes: 8 << 20, stepped: false } } }, "gltf stepped:false");
    check("Assets3D stepped:false", g.done && Assets3D.stats().lastLoad.mode === "job-memory"); g.release(); await wait(20); Assets.update(0);
    g = await acquire({ gltf: { a: "models/ext_bend.gltf" }, meshes: { b: "models/ext_textured.gltf" } }, "external .gltf + mesh");
    check("Assets3D external dependencies", g.done && Assets3D.stats().lastLoad.dependencies >= 1); g.release(); await wait(20); Assets.update(0);
    Assets3D.setBudget(0); // one unit per update(): the load is still running when it is released
    g = Assets.acquire({ gltf: { a: { path: "models/arm_read.glb", maxBytes: 3 << 20 } } });
    for (let i = 0; i < 4000 && Assets3D.pending()[0]?.phase !== "construct"; i++) { Assets3D.update(); await wait(1); }
    const cancelled = Assets3D.stats().cancelled; g.release(); await wait(30); Assets.update(0);
    Assets3D.setBudget(8);
    check("Assets3D cancel mid-load", !Assets3D.pending().length && Assets3D.stats().cancelled === cancelled + 1);
    let missing = null; try { const m = Assets.acquire({ gltf: { a: "models/does_not_exist.glb" } }); for (let i = 0; i < 2000 && !m.failed && !m.done; i++) { Assets3D.update(); Assets.update(0); await wait(1); } missing = m.failed; m.release(); } catch (_) { missing = true; }
    check("Assets3D missing file rejects", !!missing);
    flush();
}

async function memorySection() {
    log("SECTION MEMORY");
    const arm = await readTimed("models/arm_read.glb");
    async function peakOf(options) {
        const before = mem().used ?? 0; let peak = before;
        const l = gltfStepped.beginMemory(arm.bytes, "arm_read.glb", undefined, options);
        while (!l.run(4)) peak = Math.max(peak, mem().used ?? 0);
        peak = Math.max(peak, mem().used ?? 0); const a = l.finish(); free(a);
        return { before, peak, delta: peak - before };
    }
    const copy = await peakOf(undefined), borrow = await peakOf({ borrow: true });
    log(`MEMORY arm_read.glb (${arm.bytes.length} bytes) peak used delta: snapshot=${copy.delta} borrow=${borrow.delta} saved=${copy.delta - borrow.delta}`);
    results.memory.copyVsBorrow = { bytes: arm.bytes.length, snapshotDelta: copy.delta, borrowDelta: borrow.delta };
    check("borrow needs less RAM than the snapshot", borrow.delta < copy.delta);
    // leak check: build, abandon and dispose repeatedly
    const bend = (await readTimed("models/bend_hi.glb")).bytes;
    const cycles = n => { for (let i = 0; i < n; i++) {
        const l = gltfStepped.beginMemory(bend, "bend.glb", undefined, { borrow: i % 2 === 0 }); for (let k = 0; k < i % 7; k++) l.step(); l.dispose();
        const m = gltfStepped.beginMemory(bend, "bend.glb"); while (!m.run(8)); free(m.finish());
    } };
    cycles(6); std.gc(); const base = mem().used ?? 0; // warm-up first: caches and allocator pools settle
    cycles(20); std.gc(); const after = mem().used ?? 0;
    log(`MEMORY leak check: used before=${base} after=${after} growth=${after - base}`);
    results.memory.leak = { before: base, after };
    check("no RAM growth after 20 build/abandon cycles (< 64 KiB)", after - base < 65536);
    log(`MEMORY final: ${JSON.stringify(mem())}`);
    flush();
}

/* ---- BENCH: one frame per generator step; the same logic as bin/assets3d_final_bench.js ---- */
const TASKS = [
    { name: "mesh", kind: "meshes", path: "models/lit_cube.glb" },
    { name: "mesh-large", kind: "meshes", path: "models/grid_mesh.glb", few: true },
    { name: "gltf-external", kind: "gltf", path: "models/ext_bend.gltf" },
    { name: "scene-read", kind: "gltf", path: "models/arm_read.glb" },
    { name: "scene-construct", kind: "gltf", path: "models/bend_hi.glb" },
    { name: "texture-upload", kind: "textures3d", path: "models/checker.png", upload: true },
    { name: "texture-large", kind: "textures3d", path: "models/texture_256.png", upload: true },
];

function stats(values) {
    const s = values.slice().sort((a, b) => a - b);
    return { avg: s.reduce((a, b) => a + b, 0) / s.length, p95: s[Math.ceil(s.length * .95) - 1], min: s[0], max: s[s.length - 1] };
}
function* benchScenario() {
    const warmup = config.warmup ?? (quick ? 1 : 3), samples = config.samples ?? (quick ? 3 : 10);
    const budgets = quick ? [8] : [0, 8];
    const real = { GLTF3D: globalThis.GLTF3D, Model3D: globalThis.Model3D };
    for (const budget of budgets) for (const mode of ["atomic", "stepped"]) {
        if (mode === "atomic") {
            globalThis.GLTF3D = realGLTF3D; globalThis.Model3D = { ...realModel3D, Texture: { load: MD.Texture.load } }; /* no loadMemory: the path loaders, as before E03 */
        } else { globalThis.GLTF3D = real.GLTF3D; globalThis.Model3D = real.Model3D; }
        Assets3D.setBudget(budget);
        for (const task of TASKS) {
            const fast = 0;
            if (budget === 0 && task.few && quick) continue;
            const w = task.few ? 1 : warmup, n = task.few ? Math.max(2, samples >> 2) : samples;
            Assets3D.resetStats(); const peaks = [], sums = [], frames = [], walls = [], cpus = [], constructs = [], uploads = [];
            let failed = null;
            for (let round = 0; round < w + n && !failed; round++) {
                // atomic mode = the loaders before E03: textures decode synchronously inside update() (asyncRead: false)
                const asset = task.upload ? { path: task.path, upload: true, ...(mode === "atomic" ? { asyncRead: false } : {}) } : task.path;
                const spec = { [task.kind]: { asset } };
                const wallStart = Date.now(); let peak = 0, sum = 0, count = 0, cpuMax = 0;
                const group = Assets.acquire(spec);
                while (!group.done && !group.failed && count < 200000) {
                    const start = ticks(); Assets3D.update(); const span = ms(start);
                    peak = Math.max(peak, span); sum += span; count++;
                    Assets.update(0);
                    try { cpuMax = Math.max(cpuMax, Loop.getStats().cpuMs); } catch (_) { /* optional */ }
                    yield;
                }
                if (group.failed || !group.done) failed = String(group.error ?? "timeout");
                group.release(); Assets.update(0);
                if (round >= w) { peaks.push(peak); sums.push(sum); frames.push(count); walls.push(Date.now() - wallStart); cpus.push(cpuMax);
                    const last = Assets3D.stats().lastLoad; if (last) { constructs.push(last.constructMs ?? 0); uploads.push(last.uploadMs ?? 0); } }
                yield;
            }
            if (failed) { check(`bench ${mode} b${budget} ${task.name}${fast ? "+fast" : ""}`, false, failed); continue; }
            const p = stats(peaks), s = stats(sums), wl = stats(walls), c = stats(cpus), phases = Assets3D.stats().phases;
            const row = { task: task.name + (fast ? "+fast" : ""), mode, budgetMs: budget, samples: n, peakAvgMs: p.avg, peakMaxMs: p.max, sumAvgMs: s.avg, framesAvg: stats(frames).avg,
                wallAvgMs: wl.avg, frameCpuMaxMs: c.max, phases };
            results.bench.push(row);
            log(`BENCH task=${task.name}${fast ? "+fast" : ""} mode=${mode} budget=${budget} samples=${n} peakAvg=${p.avg.toFixed(3)} peakMax=${p.max.toFixed(3)} sumAvg=${s.avg.toFixed(3)} framesAvg=${stats(frames).avg.toFixed(1)} wallAvg=${wl.avg.toFixed(1)} frameCpuMax=${c.max.toFixed(2)}`);
            if (constructs.length) { const cs = stats(constructs), up = stats(uploads); row.constructAvgMs = cs.avg; row.constructMaxMs = cs.max; row.uploadAvgMs = up.avg; row.uploadMaxMs = up.max; log(`LOAD task=${task.name}${fast ? "+fast" : ""} mode=${mode} budget=${budget} constructAvg=${cs.avg.toFixed(3)} constructMax=${cs.max.toFixed(3)} uploadAvg=${up.avg.toFixed(3)} uploadMax=${up.max.toFixed(3)}`); }
            for (const [name, v] of Object.entries(phases)) log(`PHASE task=${task.name}${fast ? "+fast" : ""} mode=${mode} budget=${budget} ${name} units=${v.count} totalMs=${v.totalMs.toFixed(3)} maxUnitMs=${v.maxMs.toFixed(3)}`);
            flush();
        }
    }
    globalThis.GLTF3D = real.GLTF3D; globalThis.Model3D = real.Model3D;
}

async function main() {
    log(`E03 PS2 validation start (${quick ? "quick" : "full"}) log=${LOG}`);
    const sections = [["system", systemSection], ["io", ioSection], ["functional", functionalSection], ["memory", memorySection]];
    for (const [name, fn] of sections) {
        if (skip.has(name)) { log(`SKIP ${name}`); continue; }
        try { await fn(); } catch (e) { check(`section ${name} crashed`, false, String(e) + (e && e.stack ? " | " + e.stack.split("\n")[0] : "")); }
        flush();
    }
    if (skip.has("bench")) return finish();
    log("SECTION BENCH"); flush();
    globalThis.Loop = undefined; // Assets3D is pumped by hand, timed explicitly
    const scenario = benchScenario();
    Loop.run(() => {
        let step;
        try { step = scenario.next(); } catch (e) { log(`FAIL bench crashed: ${e}`); failures++; step = { done: true }; }
        if (step.done) { Loop.stop(); finish(); }
    }, { maxDelta: 0 });
}
function finish() {
    log(`SUMMARY failures=${failures} checks=${results.checks.length} bench=${results.bench.length} memory=${JSON.stringify(mem())}`);
    log(failures ? "RESULT FAILED" : "RESULT OK");
    log("E03 PS2 validation finished: return e03_ps2_validation.log");
    flush();
}
main().catch(e => { log(`FATAL ${e}`); failures++; finish(); });
