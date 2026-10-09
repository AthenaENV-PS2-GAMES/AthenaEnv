/*
 * Profiler: where the frame time goes, measured in C.
 *
 *   - timer scopes (begin/end, scope(fn)) read the EE cycle counter in C, so
 *     a pair costs a fraction of System.getMilliseconds();
 *   - counters sum values per frame (e.g. rebuilt chunks, spawned enemies);
 *   - every frame closes into a ring of the last HISTORY frames, from which
 *     stats() gives the last value, average, 95th percentile and peak;
 *   - attachRender3D() records Render3D.frameStats() as counters and warns
 *     when objects fall into the C clipper on the EE (cpuClipObjects), the
 *     costliest 3D path;
 *   - overlay() shows one line per scope in the Debug overlay.
 *
 * With Loop.run(), auto() closes the frames in a Loop system after every
 * draw. Games with their own loop call Profiler.frame() once per frame.
 */
import * as Loop from "Loop";
import * as Debug from "Debug";
import * as Native from "ProfilerNative";

export const HISTORY = Native.HISTORY;
export const MAX_SCOPES = Native.MAX_SCOPES;
export const MAX_DEPTH = Native.MAX_DEPTH;
export const scope = Native.scope;
export const counter = Native.counter;
export const begin = Native.begin;
export const end = Native.end;
export const count = Native.count;
export const stats = Native.stats;
export const names = Native.names;
export const errors = Native.errors;

const WARN_SECONDS = 5;
const state = {
    system: null,
    render: null,           // the Render3D namespace given to attachRender3D()
    renderStats: {},
    renderIds: null,
    warn: true,
    warnedAt: -Infinity,
    elapsed: 0,
    overlay: false,
    overlayFrames: 60,
    watched: new Set(),
    knownScopes: 0,
    scratch: {},
};

/** Runs fn inside a timer scope and returns its result; the scope closes on throw too. */
export function measure(name, fn) {
    if (typeof fn !== "function") throw new TypeError("Profiler.measure expects a function");
    Native.begin(name);
    try { return fn(); } finally { Native.end(name); }
}

function sampleRender() {
    const r = state.render, s = state.renderStats, ids = state.renderIds;
    r.frameStats(s);
    Native.count(ids.triangles, s.triangles);
    Native.count(ids.objects, s.drawPasses);
    Native.count(ids.passes, s.pipelinePasses);
    Native.count(ids.culled, s.culledObjects);
    Native.count(ids.cpuClip, s.cpuClipObjects);
    if (state.warn && s.cpuClipObjects > 0 && state.elapsed - state.warnedAt >= WARN_SECONDS) {
        state.warnedAt = state.elapsed;
        console.log(`[Profiler] ${s.cpuClipObjects} object(s) clipped in C on the EE this frame ` +
            `(Render3D cpuClipObjects): split large meshes that surround the camera`);
    }
}

/** Closes the frame (counters and timers) and returns its length in ms. */
export function frame() {
    if (state.render) sampleRender();
    const ms = Native.frame();
    state.elapsed += ms / 1000;
    if (state.overlay && Native.scopeCount() !== state.knownScopes) syncWatches();
    return ms;
}

/**
 * Closes the frames automatically after every draw of Loop.run() (after
 * the Debug overlay). Returns whether it is on.
 */
export function auto(on = true) {
    if (on && !state.system) {
        state.system = Loop.addSystem({
            name: "profiler",
            priority: 2000,
            postDraw() { frame(); },
        });
    } else if (!on && state.system) {
        Loop.removeSystem(state.system);
        state.system = null;
    }
    return state.system !== null;
}

/**
 * Records the totals of every Render3D draw as the counters 3d.triangles,
 * 3d.objects, 3d.passes, 3d.culled and 3d.cpuClip, read with
 * Render3D.frameStats() at each frame(). Pass the Render3D namespace
 * (`import * as Render3D from "Render3D"`), or null to stop. warn (default
 * true) logs, at most every 5 s, when objects are clipped in C on the EE.
 */
export function attachRender3D(render3d, options) {
    if (render3d === null) { state.render = null; return; }
    if (!render3d || typeof render3d.frameStats !== "function")
        throw new TypeError("Profiler.attachRender3D expects the Render3D module");
    if (options !== undefined && (options === null || typeof options !== "object"))
        throw new TypeError("Profiler.attachRender3D options must be an object");
    state.warn = !options || options.warn === undefined ? true : !!options.warn;
    state.renderIds = {
        triangles: Native.counter("3d.triangles"), objects: Native.counter("3d.objects"),
        passes: Native.counter("3d.passes"), culled: Native.counter("3d.culled"),
        cpuClip: Native.counter("3d.cpuClip"),
    };
    render3d.frameStats();  // drop the draws before the attachment
    state.render = render3d;
}

function fmt(value) {
    return value >= 100 ? value.toFixed(0) : value >= 10 ? value.toFixed(1) : value.toFixed(2);
}

/** One line for a scope: "1.23 ms p95 2.10 max 3.40" or "avg 120 max 340". */
export function describe(name, frames) {
    const s = Native.stats(name, frames, state.scratch);
    if (!s.samples) return "-";
    if (s.kind === "timer") {
        const calls = s.averageCalls > 1.05 ? ` x${fmt(s.averageCalls)}` : "";
        return `${fmt(s.average)} ms p95 ${fmt(s.p95)} max ${fmt(s.peak)}${calls}`;
    }
    const alert = s.name === "3d.cpuClip" && s.peak > 0 ? " C CLIP!" : "";
    return `avg ${fmt(s.average)} max ${fmt(s.peak)}${alert}`;
}

function syncWatches() {
    const list = Native.names();
    state.knownScopes = list.length;
    for (const name of list) {
        if (state.watched.has(name)) continue;
        state.watched.add(name);
        Debug.watch(name, () => describe(name, state.overlayFrames));
    }
}

/**
 * Shows a line per scope in the Debug overlay (and turns the overlay on),
 * over the last `frames` frames (default 60). Returns whether it is on.
 */
export function overlay(on, options) {
    if (options !== undefined) {
        if (options === null || typeof options !== "object") throw new TypeError("Profiler.overlay options must be an object");
        if (options.frames !== undefined) {
            const f = options.frames;
            if (!Number.isInteger(f) || f < 1 || f > Native.HISTORY)
                throw new RangeError(`Profiler.overlay frames must be an integer from 1 to ${Native.HISTORY}`);
            state.overlayFrames = f;
        }
    }
    if (on !== undefined && !!on !== state.overlay) {
        state.overlay = !!on;
        if (state.overlay) { Debug.overlay(true); syncWatches(); }
        else {
            for (const name of state.watched) Debug.unwatch(name);
            state.watched.clear();
            state.knownScopes = 0;
        }
    }
    return state.overlay;
}

/** Stats of every scope over the last `frames` frames (allocates: for logs and tests). */
export function report(frames) {
    return Native.names().map(name => Native.stats(name, frames));
}

/** Prints report() as a table with console.log. */
export function log(frames) {
    for (const s of report(frames)) {
        if (!s.samples) continue;
        console.log(`[Profiler] ${s.name.padEnd(16)} ${describe(s.name, frames)}`);
    }
    const e = Native.errors();
    if (e) console.log(`[Profiler] ${e} unmatched end() or too deep begin() call(s)`);
}

/** Clears the history; forget = true also drops the scope names (ids become invalid). */
export function reset(forget) {
    Native.reset(forget);
    if (forget) {
        for (const name of state.watched) Debug.unwatch(name);
        state.watched.clear();
        state.knownScopes = 0;
        if (state.render) attachRender3D(state.render, { warn: state.warn });
        if (state.overlay) syncWatches();
    }
}
