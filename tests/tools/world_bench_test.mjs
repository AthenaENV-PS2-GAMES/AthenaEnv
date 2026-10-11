// Exercises the complete E00 harness and final-frame capture with mock hardware.
// These timings are synthetic and must never be used as performance evidence.
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const data = source => 'data:text/javascript;base64,' + Buffer.from(source).toString('base64');
const loopURL = data(`export const systems = []; export let running = false;
export function addSystem(s) { systems.push(s); } export function removeSystem(s) { const i = systems.indexOf(s); if (i >= 0) systems.splice(i, 1); }
export function stop() { running = false; } export function run() { running = true; }
export function getStats() { return { cpuMs: 5, frameMs: 17 }; }
export function drive() { const sorted = systems.slice().sort((a,b) => (a.priority ?? 0) - (b.priority ?? 0)); for (const phase of ['preUpdate', 'postDraw']) for (const s of sorted) if (systems.includes(s) && s[phase]) s[phase](); }`);
const clockURL = data('let t = 0; export function ticks() { return ++t; } export function ticksToMilliseconds(t) { return t / 1000; }');
const source = (await readFile(new URL('../../src/modules/bench/js/bench.js', import.meta.url), 'utf8'))
    .replace('"Loop"', JSON.stringify(loopURL)).replace('"ProfilerNative"', JSON.stringify(clockURL));
const Bench = await import(data(source)), Loop = await import(loopURL);
const harness = await readFile(new URL('../../bin/world_systems_bench.js', import.meta.url), 'utf8');
let live = 0, uploads = 0, config;
class Handle {
    constructor() { live++; this.disposed = false; }
    dispose() { assert(!this.disposed); this.disposed = true; live--; }
}
class Camera extends Handle {
    constructor() { super(); this.eye = [0, 0, 5]; this.target = [0, 0, 0]; }
    setPosition(...eye) { assert.notDeepEqual(eye, this.target); this.eye = eye; return this; }
    lookAt(...target) { assert.notDeepEqual(target, this.eye); this.target = target; return this; }
    setViewport() { return this; }
}
class Node extends Handle { add() {} setPosition() { return this; } }
class Scene extends Handle { constructor() { super(); this.root = new Node(); } update() {} }
class Builder extends Handle { box() { return this; } build() { return [new Handle()]; } }
class Texture extends Handle { upload() { uploads++; } }
class Grid extends Handle { fill() {} findPath() { return new Float32Array([1, 2]); } }
class World extends Handle { sphere() {} body() { return { setPosition() {} }; } update() {} }
const files = new Map([['model.glb', 64], ['scene.glb', 128], ['texture.png', 256]]);
const writes = new Map();
let queued = 0, frames = 0;
const native = {
    Bench, Loop, Scene3D: { Scene, Node }, Camera3D: { Camera }, MeshBuilder: { Builder },
    LOD: { Group: Handle, update() {} }, Nav: { Grid }, Triggers3D: { World },
    Model3D: { load() { return new Handle(); }, Texture: { load() { return new Texture(); } } },
    GLTF3D: { load() { return { root: new Node(), nodes: [new Node()], clips: { walk: new Handle() } }; } },
    Color: { new() { return 1; } }, Screen: { getMode() { return { width: 640, height: 448 }; },
        getMemoryStats() { return 0; }, getFreeVRAM() { return 1000; } },
    System: { getMemoryStats() { return { allocs: live }; } },
    Debug3D: { show() {}, setCamera() {}, clear() { queued = 0; },
        lines(values) { queued = values.length / 6; return queued; }, dropped() { return 0; },
        draw() { const n = queued; queued = 0; return n; } },
    console: { log() {} },
    std: { SEEK_END: 2, loadFile() { return JSON.stringify(config); }, open(path, mode) {
        if (mode === 'w') return { puts(s) { writes.set(path, JSON.parse(s)); }, flush() {}, error() { return false; }, close() {} };
        if (!files.has(path)) return null;
        return { seek() { return 0; }, tell() { return files.get(path); },
            read(_buffer, _offset, size) { return Math.min(size, files.get(path)); }, error() { return false; }, close() {} };
    } },
};
globalThis.std = native.std; // Bench.save executes in its module's realm.
function makeConfig() {
    return { path: 'results.json', warmup: 0, samples: 2,
        metadata: { platform: 'mock', revision: 'test', runtime: 'mock', buildOptions: {},
            runId: 'mock', variant: 'before', pair: 1, order: 1, hashes: {} },
        loading: { model: { path: 'model.glb', bytes: 64 }, scene: { path: 'scene.glb', bytes: 128 }, texture: { path: 'texture.png', bytes: 256 } } };
}
async function run() {
    frames = 0;
    vm.runInNewContext(harness, { ...native });
    while (Loop.running) { assert(frames++ < 100); Loop.drive(); await Promise.resolve(); }
    assert.equal(Loop.systems.length, 0);
    assert.equal(live, 0);
    return writes.get('results.json');
}
config = makeConfig();
const report = await run();
assert.equal(report.status, 'completed');
assert.equal(report.results.length, 13);
assert.equal(report.frames.cpu.sampleMs.length, 57); // Nav 33 frames, twelve tasks x two.
assert.equal(frames, 58); // Final observer writes only after the last complete frame.
assert.equal(report.frames.cpu.maxMs, 5);
assert.equal(report.frames.wall.maxMs, 17);
assert.equal(uploads, 2);
assert.equal(report.results.find(r => r.name === 'debug3d.draw2000').metrics.drawn, 2000);
assert.equal(report.results.find(r => r.name === 'loading.upload').metadata.gsWait, true);
config = makeConfig(); config.loading.model.bytes = 65;
assert.equal((await run()).status, 'error'); // A changed/truncated fixture cannot become a baseline.
delete globalThis.std;
console.log('World benchmark harness tests passed (synthetic hardware; no performance evidence)');
