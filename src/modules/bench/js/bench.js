/* Bench runs one synchronous batch per frame. Only the work callback is
 * timed: setup, cleanup, sorting and JSON checkpoints stay outside it.
 * Rendering measures CPU submission unless the task explicitly waits for
 * the GS. The timer and JS call overhead remain in every measurement. */
import * as Loop from "Loop";
import { ticks, ticksToMilliseconds } from "ProfilerNative";

function integer(value, min, max, name) {
    if (!Number.isInteger(value) || value < min || value > max)
        throw new RangeError(`Bench ${name} must be an integer from ${min} to ${max}`);
    return value;
}
function callback(value, name, required = false) {
    if (typeof value !== "function" && (required || value !== undefined))
        throw new TypeError(`Bench ${name} must be a function`);
    return value;
}
function sync(value) {
    if (value && typeof value.then === "function") throw new TypeError("Bench callbacks must be synchronous");
    return value;
}
function positive(value, name) {
    if (typeof value !== "number" || !Number.isFinite(value) || value <= 0)
        throw new RangeError(`Bench ${name} must be finite and positive`);
    return value;
}
function statistics(values) {
    const sorted = values.slice().sort((a, b) => a - b);
    const n = sorted.length;
    return { averageMs: n ? sorted.reduce((sum, v) => sum + v, 0) / n : 0,
        p95Ms: n ? sorted[Math.ceil(n * .95) - 1] : 0,
        minMs: n ? sorted[0] : 0, maxMs: n ? sorted[n - 1] : 0 };
}

/** Write a checkpoint between tasks, on a writable device. */
export function save(report, path) {
    if (typeof path !== "string" || !path.length) throw new TypeError("Bench path must be a nonempty string");
    const json = JSON.stringify(report, null, 2);
    if (typeof std === "undefined" || typeof std.open !== "function") throw new Error("Bench.save needs std.open");
    const file = std.open(path, "w");
    if (!file) throw new Error(`Bench cannot open ${path}`);
    try {
        file.puts(json + "\n");
        file.flush();
        if (file.error()) throw new Error(`Bench cannot write ${path}`);
    } finally { file.close(); }
}

export class Runner {
    constructor(tasks, options = {}) {
        if (!Array.isArray(tasks) || !tasks.length || tasks.length > 256) throw new TypeError("Bench tasks must be an array of 1 to 256 tasks");
        if (!options || typeof options !== "object") throw new TypeError("Bench options must be an object");
        const warmup = integer(options.warmup ?? 30, 0, 10000, "warmup");
        const samples = integer(options.samples ?? 120, 1, 10000, "samples");
        const iterations = integer(options.iterations ?? 1, 1, 10000, "iterations");
        const names = new Set();
        this._tasks = Array.from(tasks, task => {
            if (!task || typeof task.name !== "string" || !task.name.length || names.has(task.name))
                throw new TypeError("Bench task names must be nonempty and unique");
            names.add(task.name);
            return { name: task.name, run: callback(task.run, "run", true),
                setup: callback(task.setup, "setup"), teardown: callback(task.teardown, "teardown"),
                beforeBatch: callback(task.beforeBatch, "beforeBatch"),
                afterBatch: callback(task.afterBatch, "afterBatch"),
                inspect: callback(task.inspect, "inspect"), metadata: task.metadata ?? {},
                warmup: integer(task.warmup ?? warmup, 0, 10000, "warmup"),
                samples: integer(task.samples ?? samples, 1, 10000, "samples"),
                iterations: integer(task.iterations ?? iterations, 1, 10000, "iterations") };
        });
        this._onResult = callback(options.onResult, "onResult");
        if (options.path !== undefined && (typeof options.path !== "string" || !options.path.length))
            throw new TypeError("Bench path must be a nonempty string");
        this._path = options.path;
        this._frameBudgetMs = positive(options.frameBudgetMs ?? 1000 / 60, "frameBudgetMs");
        this.report = { version: 2, label: options.label ?? "", metadata: options.metadata ?? {},
            frameBudgetMs: this._frameBudgetMs,
            status: "running", results: [] };
        this._index = 0; this._frame = 0; this._active = false; this._busy = false;
        this._cancel = false; this._context = undefined;
        // Allocate all sample storage once; step() reuses it across tasks.
        this._times = new Uint32Array(Math.max(...this._tasks.map(t => t.samples)));
        this._batchTimes = new Uint32Array(this._times.length);
    }
    get done() { return this.report.status !== "running"; }
    cancel() {
        if (this.done) return;
        this._cancel = true;
        if (!this._busy) this.step();
    }
    _finish(error) {
        const task = this._tasks[this._index];
        this._active = false;
        let metrics;
        try { if (task.inspect) metrics = sync(task.inspect(this._context)); }
        catch (e) { if (!error) error = e || new Error(String(e)); }
        try { if (task.teardown) sync(task.teardown(this._context)); }
        catch (e) { if (!error) error = e || new Error(String(e)); }
        const count = Math.max(0, this._frame - task.warmup);
        const scale = ticksToMilliseconds(1) / task.iterations;
        const values = Array.from(this._times.subarray(0, count), t => t * scale);
        const batchValues = Array.from(this._batchTimes.subarray(0, count), t => ticksToMilliseconds(t));
        const result = { name: task.name, status: error ? "error" : this._cancel ? "cancelled" : "completed",
            warmup: task.warmup, samples: count, iterations: task.iterations,
            ...statistics(values), metadata: task.metadata,
            sampleTicks: Array.from(this._times.subarray(0, count)), sampleMs: values,
            batchCpu: { ...statistics(batchValues), sampleMs: batchValues,
                overBudget: batchValues.filter(v => v > this._frameBudgetMs).length } };
        if (metrics !== undefined) result.metrics = metrics;
        if (error) result.error = String(error) + (error.stack ? "\n" + error.stack : "");
        this.report.results.push(result);
        this._context = undefined;
        this._index++;
        if (this._cancel) this.report.status = "cancelled";
        else if (this._index === this._tasks.length)
            this.report.status = this.report.results.some(r => r.status === "error") ? "error" : "completed";
        if (this._path) save(this.report, this._path);
        if (this._onResult) sync(this._onResult(result, this.report));
    }
    /** Executes one batch. Call once per frame in a manual loop. */
    step() {
        if (this._busy) throw new TypeError("Bench.step cannot run recursively");
        if (this.done) return false;
        this._busy = true;
        try {
            const task = this._tasks[this._index];
            if (this._cancel) {
                if (this._active) this._finish();
                else { this.report.status = "cancelled"; if (this._path) save(this.report, this._path); }
                return false;
            }
            let error;
            try {
                if (!this._active) {
                    this._frame = 0; this._active = true;
                    this._context = task.setup ? sync(task.setup()) : undefined;
                }
                if (!this._cancel) {
                    const batchStart = ticks();
                    let elapsed = 0;
                    try {
                        if (task.beforeBatch) sync(task.beforeBatch(this._context));
                        if (!this._cancel) {
                            const start = ticks();
                            for (let i = 0; i < task.iterations && !this._cancel; i++) sync(task.run(this._context, i));
                            elapsed = (ticks() - start) >>> 0;
                        }
                    } catch (e) { error = e || new Error(String(e)); }
                    finally {
                        try { if (task.afterBatch) sync(task.afterBatch(this._context)); }
                        catch (e) { if (!error) error = e || new Error(String(e)); }
                    }
                    const batchElapsed = (ticks() - batchStart) >>> 0;
                    if (!error && !this._cancel) {
                        if (this._frame >= task.warmup) this._times[this._frame - task.warmup] = elapsed;
                        if (this._frame >= task.warmup) this._batchTimes[this._frame - task.warmup] = batchElapsed;
                        this._frame++;
                    }
                }
            } catch (e) { error = e || new Error(String(e)); }
            if (error || this._cancel || this._frame === task.warmup + task.samples) this._finish(error);
            return !this.done;
        } catch (e) {
            this.report.status = "error";
            throw e; // checkpoint/onResult failures cannot be reported as success
        } finally { this._busy = false; }
    }
}

/** Uses the existing Loop, without starting or stopping it. */
export function run(tasks, options) {
    const runner = new Runner(tasks, options);
    return new Promise((resolve, reject) => {
        const system = { priority: 900000, realTime: true, postDraw() {
            try {
                if (!runner.step()) { Loop.removeSystem(system); resolve(runner.report); }
            } catch (e) { Loop.removeSystem(system); reject(e); }
        } };
        Loop.addSystem(system);
    });
}
