/* GLB reads run on Thread's native job pool. GLTF3D scenes are then built by
 * GLTF3D.Loader in resumable units (parse, validate, one primitive or node,
 * links, one skin or clip, the JS wrappers) that update() spreads across
 * frames. A single unit is atomic, and meshes and textures still build in one
 * piece, so the budget is soft: stats() reports each phase's largest unit. */
import { Assets } from "Scene";
import { ticks, ticksToMilliseconds } from "ProfilerNative";
import { decodeTexture } from "Assets3DNative";

const g = globalThis, queue = [], identities = new WeakMap();
let nextIdentity = 1, active = null, system = null, updating = false;
let completed = 0, failed = 0, cancelled = 0, lastMs = 0, maxMs = 0;
let maxConstructionMs = 0, maxUploadMs = 0, lastLoad = null;
const phaseStats = new Map();
const DEFAULT_MAX = 16 * 1024 * 1024;
export let budgetMs = 8;
export function setBudget(ms) {
    if (typeof ms !== "number" || !Number.isFinite(ms) || ms < 0)
        throw new RangeError("Assets3D budget must be finite and non-negative");
    budgetMs = ms;
}
const elapsed = start => ticksToMilliseconds((ticks() - start) >>> 0);
function optional(name, what) {
    if (g[name] === undefined) throw new Error(`${what} needs the ${name} module, which is not in this build`);
    return g[name];
}
function material(spec, where) {
    const m = spec.material;
    if (m === undefined) return undefined;
    if (m === null || typeof m !== "object") throw new TypeError(`${where}: material must be an object`);
    const out = { ...m };
    if (m.baseColor) out.baseColor = new Float32Array(m.baseColor);
    return out;
}
function materialKey(spec) {
    const m = spec.material;
    if (!m) return "";
    const plain = {};
    for (const key of Object.keys(m).sort()) {
        const value = m[key];
        if (key === "baseColor" && value) plain[key] = Array.from(value);
        else if (key === "texture" && value && typeof value === "object") {
            if (!identities.has(value)) identities.set(value, nextIdentity++);
            plain[key] = { textureIdentity: identities.get(value) };
        } else plain[key] = value;
    }
    return "#" + JSON.stringify(plain);
}
let defaultStepped = true;
/** Default for the `stepped` asset option: true builds GLB/.gltf in resumable units
 * across update() calls; false builds a whole GLB per call (and .gltf through the path loader). */
export function setStepped(value) {
    if (typeof value !== "boolean") throw new TypeError("Assets3D stepped must be a boolean");
    defaultStepped = value;
}
function steppedOf(spec, available) {
    if (spec.stepped !== undefined && typeof spec.stepped !== "boolean") throw new TypeError("Assets3D stepped must be a boolean");
    return available && (spec.stepped ?? defaultStepped);
}
function readOptions(path, spec, resumable, anyFile = false) {
    if (spec.asyncRead !== undefined && typeof spec.asyncRead !== "boolean")
        throw new TypeError("Assets3D asyncRead must be a boolean");
    // .gltf (external files) needs the resolver of the resumable loaders.
    if (!(anyFile || (resumable ? /\.(glb|gltf)$/i : /\.glb$/i).test(path)) || spec.asyncRead === false) return null;
    const maxBytes = spec.maxBytes ?? DEFAULT_MAX;
    if (!Number.isInteger(maxBytes) || maxBytes < 1 || maxBytes > DEFAULT_MAX)
        throw new RangeError("Assets3D maxBytes must be an integer from 1 to 16777216");
    return { maxBytes };
}
function stopSystem() {
    if (!queue.length && !active && system) { g.Loop.removeSystem(system); system = null; }
}
function useSystem() {
    if (system || !g.Loop || typeof g.Loop.addSystem !== "function") return;
    system = g.Loop.addSystem({ name: "assets3d", priority: -100, realTime: true, preUpdate() { update(); } });
}
function schedule(path, handle, run, read = null, upload = null, free = null, begin = null, start = null) {
    return new Promise((resolve, reject) => {
        const job = { path, handle, run, read, upload, free, begin, start, resolve, reject, reader: null, loader: null, depReader: null, depIndex: -1, dependencies: 0, depWaitMs: 0,
            bytesDone: 0, bytesTotal: 0, phase: "queued", ended: false, steps: 0, constructMs: 0 };
        handle.cancel = () => {
            if (job.ended) return;
            job.ended = true; cancelled++;
            if (job.reader) job.reader.cancel();
            if (job.depReader) job.depReader.cancel();
            job.reader = null; job.depReader = null;
            if (job.loader) { mergeLoaderStats(job); job.loader.dispose(); job.loader = null; }
            if (active === job) active = null;
            const i = queue.indexOf(job); if (i >= 0) queue.splice(i, 1);
            reject(Object.assign(new Error(`Assets3D cancelled '${path}'`), { path, code: "CANCELLED" }));
            stopSystem();
        };
        queue.push(job); useSystem();
    });
}
function fail(job, error) {
    if (job.depReader) job.depReader.cancel();
    job.ended = true; job.reader = null; job.depReader = null; failed++;
    if (job.loader) { mergeLoaderStats(job); job.loader.dispose(); job.loader = null; }
    if (active === job) active = null;
    if (error && typeof error === "object" && error.path === undefined) error.path = job.path;
    job.reject(error);
}
function uploadTexture(texture) {
    const start = ticks();
    try { texture.upload(); }
    finally { maxUploadMs = Math.max(maxUploadMs, elapsed(start)); }
}
function construct(job, bytes) {
    job.phase = "construct";
    const start = ticks(); let value, constructed = false;
    try {
        value = job.run(bytes);
        constructed = true;
        const constructMs = elapsed(start);
        maxConstructionMs = Math.max(maxConstructionMs, constructMs);
        const uploadStart = ticks();
        if (job.upload) job.upload(value);
        const uploadMs = job.upload ? elapsed(uploadStart) : 0;
        if (job.ended) { if (job.free) job.free(value); return; }
        lastLoad = { path: job.path, mode: job.start ? "job-decode" : job.read ? "job-memory" : "path", bytes: job.bytesTotal,
            readWaitMs: job.readWaitMs ?? 0, constructMs, uploadMs };
        job.ended = true; completed++; job.reader = null; job.resolve(value);
    } catch (e) {
        if (!constructed) maxConstructionMs = Math.max(maxConstructionMs, elapsed(start));
        if (value && job.free) job.free(value);
        if (!job.ended) fail(job, e);
    }
}
/** phases: count = units run, totalMs, maxMs = the largest single unit. The
 * native loaders time their own units; "begin", "dependencies" (reads) and
 * "finish" (the JS handles) are timed here. */
function addPhase(name, units, totalMs, maxMs) {
    let entry = phaseStats.get(name);
    if (!entry) phaseStats.set(name, entry = { count: 0, totalMs: 0, maxMs: 0 });
    entry.count += units; entry.totalMs += totalMs; entry.maxMs = Math.max(entry.maxMs, maxMs);
    maxConstructionMs = Math.max(maxConstructionMs, maxMs);
}
function recordPhase(name, ms) { addPhase(name, 1, ms, ms); }
const loaderStats = new Float64Array(3 * 16);
/** Folds the loader's own per-phase accounting into the statistics; call before it ends.
 * Fills a preallocated array: the last call of a load allocates nothing here. */
function mergeLoaderStats(job) {
    const names = job.loader.constructor.PHASES;
    try { job.loader.phaseStatsInto(loaderStats); } catch (_) { return; }
    for (let i = 0; i < names.length; i++)
        if (loaderStats[3 * i] > 0) addPhase(names[i], loaderStats[3 * i], loaderStats[3 * i + 1], loaderStats[3 * i + 2]);
}
function begin(job, bytes) {
    job.phase = "construct";
    const start = ticks();
    try { job.loader = job.begin(bytes); }
    catch (e) { fail(job, e); return; }
    finally { const ms = elapsed(start); recordPhase("begin", ms); job.constructMs += ms; }
    job.reader = null; // the loader borrows the read buffer (pinned by the binding)
}
/** The loader waits for external files: reads them one at a time on Thread jobs
 * (paths relative to the model, each limited by maxBytes) and hands the bytes
 * over. Returns true while it still waits for a read, "failed" if the job ended. */
function serviceDependencies(job) {
    for (;;) {
        if (!job.depReader) {
            const list = job.loader.dependencies;
            job.dependencies = list.length;
            const index = list.findIndex(d => !d.provided);
            if (index < 0) { job.phase = "construct"; return false; }
            job.phase = "dependencies"; job.depIndex = index; job.depStart = Date.now();
            try { job.depReader = optional("Thread", "Assets3D dependencies").readFileAsync(job.path.replace(/[^\/\\:]*$/, "") + list[index].uri, job.read); }
            catch (e) { fail(job, e); return "failed"; }
            return true; // a newly submitted read yields before polling
        }
        let status;
        try { status = job.depReader.poll(); }
        catch (e) { fail(job, e); return "failed"; }
        job.bytesDone = status.bytesDone; job.bytesTotal = status.bytesTotal;
        if (status.state === "running") return true;
        if (status.state !== "done") { fail(job, status.error); return "failed"; }
        job.depWaitMs += Math.max(0, Date.now() - job.depStart);
        try { job.loader.provide(job.depIndex, status.result); }
        catch (e) { fail(job, e); return "failed"; }
        job.depReader = null; status = null;
    }
}
/** Runs loader units for up to remainingMs (at least one); true once the job has ended
 * (completed or failed). The native loader times each unit and stops on the budget. */
function step(job, remainingMs) {
    let value, ran = 1;
    try {
        const done = job.loader.run(remainingMs > 0 ? remainingMs : 0);
        ran = Math.max(1, job.loader.lastUnits);
        if (done) {
            mergeLoaderStats(job);
            const start = ticks();
            value = job.loader.finish();
            const ms = elapsed(start); recordPhase("finish", ms); job.constructMs += ms;
        }
    } catch (e) {
        mergeLoaderStats(job); fail(job, e); return true;
    }
    job.steps += ran;
    if (value === undefined) return false;
    job.loader = null;
    if (job.ended) { if (job.free) job.free(value); return true; }
    lastLoad = { path: job.path, mode: "job-memory-steps", bytes: job.bytesTotal, readWaitMs: job.readWaitMs ?? 0,
        constructMs: job.constructMs, steps: job.steps, dependencies: job.dependencies, dependencyWaitMs: job.depWaitMs, uploadMs: 0 };
    job.ended = true; completed++; job.resolve(value);
    return true;
}
/** Polls reads without waiting. Runs at least one unit per call and keeps
 * going until budgetMs is spent; one unit (a texture decode, a large mesh)
 * may exceed it. Reads serialize to bound RAM; steps of one asset run in order. */
export function update() {
    if (updating) return 0;
    updating = true;
    const start = ticks(); let ran = 0;
    try {
        while (active || queue.length) {
            if (ran && elapsed(start) >= budgetMs) break;
            if (!active) {
                active = queue.shift();
                if (active.read || active.start) {
                    active.phase = "reading"; active.readStart = Date.now();
                    try { active.reader = active.start ? active.start() : optional("Thread", "Assets3D GLB").readFileAsync(active.path, active.read); }
                    catch (e) { fail(active, e); ran++; continue; }
                    // Newly submitted reads yield before polling or construction.
                    ran++; break;
                }
            }
            const job = active;
            if (job.reader) {
                let status;
                try { status = job.reader.poll(); }
                catch (e) { fail(job, e); ran++; continue; }
                job.bytesDone = status.bytesDone ?? 0; job.bytesTotal = status.bytesTotal ?? 0;
                if (status.state === "running") break;
                if (status.state !== "done") { fail(job, status.error); ran++; continue; }
                job.readWaitMs = Math.max(0, Date.now() - job.readStart);
                if (job.begin) { begin(job, status.result); status = null; if (!job.ended) { ran++; continue; } }
                else { construct(job, status.result); status = null; }
            } else if (job.loader) {
                if (job.loader.phase === "dependencies") {
                    const waiting = serviceDependencies(job);
                    if (waiting === "failed") { ran++; continue; }
                    if (waiting) { ran++; break; }
                }
                if (!step(job, budgetMs - elapsed(start))) { ran++; continue; }
            } else construct(job, undefined);
            active = null; ran++;
        }
    } finally {
        lastMs = elapsed(start); maxMs = Math.max(maxMs, lastMs);
        updating = false; stopSystem();
    }
    return ran;
}
function dispose(value) { if (value && typeof value.dispose === "function") value.dispose(); }
function freeGltf(asset) {
    for (const clip of Object.values(asset.clips || {})) dispose(clip);
    for (const node of asset.nodes || []) dispose(node);
    if (asset.root) { if (asset.root.detach) asset.root.detach(); dispose(asset.root); }
}
const kinds = {
    meshes: {
        load(path, spec, handle) {
            const Model3D = optional("Model3D", "Assets3D meshes"), m = material(spec, "Assets3D meshes");
            const resumable = steppedOf(spec, typeof Model3D.beginMemory === "function"), read = readOptions(path, spec, resumable);
            return schedule(path, handle, bytes => read ? Model3D.loadMemory(bytes, path, m) : Model3D.load(path, m), read, null, dispose,
                read && resumable ? bytes => Model3D.beginMemory(bytes, path, m, { dependencies: true, borrow: true }) : null);
        },
        free: dispose,
        key(path, spec) { return path + materialKey(spec) + `@max${spec.maxBytes ?? DEFAULT_MAX}`; },
    },
    textures3d: {
        load(path, spec, handle) {
            const Model3D = optional("Model3D", "Assets3D textures3d");
            // PNG/JPEG/BMP: a job-pool worker reads and decodes the file; update() only adopts the pixels.
            const filter = spec.filter ?? 0, wrap = spec.wrap ?? 0;
            if (spec.asyncRead !== undefined && typeof spec.asyncRead !== "boolean") throw new TypeError("Assets3D asyncRead must be a boolean");
            const maxBytes = spec.maxBytes ?? DEFAULT_MAX;
            if (!Number.isInteger(maxBytes) || maxBytes < 1 || maxBytes > DEFAULT_MAX)
                throw new RangeError("Assets3D maxBytes must be an integer from 1 to 16777216");
            const worker = spec.asyncRead !== false && /\.(png|jpe?g|bmp)$/i.test(path);
            return schedule(path, handle, texture => worker ? texture : Model3D.Texture.load(path, filter, wrap),
                null, spec.upload ? uploadTexture : null, dispose, null, worker ? () => decodeTexture(path, filter, wrap, maxBytes) : null);
        },
        free: dispose,
        reuse(texture, spec) { if (spec.upload) uploadTexture(texture); },
        key(path, spec) { return `${path}@${spec.filter ?? 0}/${spec.wrap ?? 0}@max${spec.maxBytes ?? DEFAULT_MAX}`; },
    },
    gltf: {
        load(path, spec, handle) {
            const GLTF3D = optional("GLTF3D", "Assets3D gltf"), m = material(spec, "Assets3D gltf");
            const resumable = steppedOf(spec, typeof GLTF3D.beginMemory === "function"), read = readOptions(path, spec, resumable);
            return schedule(path, handle, bytes => read ? GLTF3D.loadMemory(bytes, path, m) : GLTF3D.load(path, m), read,
                null, freeGltf, read && resumable ? bytes => GLTF3D.beginMemory(bytes, path, m, { dependencies: true, borrow: true }) : null);
        },
        free: freeGltf,
        key(path, spec) { return path + materialKey(spec) + `@max${spec.maxBytes ?? DEFAULT_MAX}`; },
    },
};
for (const [kind, loader] of Object.entries(kinds)) Assets.define(kind, loader);
export function pending() {
    return (active ? [active, ...queue] : queue).map(job => ({ path: job.path, phase: job.phase,
        bytesDone: job.bytesDone, bytesTotal: job.bytesTotal,
        constructPhase: job.loader ? job.loader.phase : null,
        unitsDone: job.loader ? job.loader.unitsDone : 0, unitsTotal: job.loader ? job.loader.unitsTotal : 0 }));
}
export function stats() {
    return { queued: queue.length, reading: active?.phase === "reading" ? 1 : 0,
        completed, failed, cancelled, lastFrameMs: lastMs, maxFrameMs: maxMs,
        maxConstructionMs, maxUploadMs, lastLoad, budgetMs,
        phases: Object.fromEntries([...phaseStats].map(([name, v]) => [name, { ...v }])) };
}
/** Clears the lastLoad, maxima and per-phase statistics; counters and the queue stay. */
export function resetStats() {
    lastMs = 0; maxMs = 0; maxConstructionMs = 0; maxUploadMs = 0; lastLoad = null; phaseStats.clear();
}
export const KINDS = Object.keys(kinds);
