import * as Bench from "Bench";
import * as Loop from "Loop";

let passed = 0, failed = 0;
function check(name, condition) { if (condition) passed++; else { failed++; console.log("[FAIL] " + name); } }
function throws(name, fn, type) {
    let e; try { fn(); } catch (error) { e = error; }
    check(name, e !== undefined && (!type || e instanceof type));
}
const task = { name: "empty", run() {} };
throws("needs tasks", () => new Bench.Runner([]), TypeError);
throws("needs run", () => new Bench.Runner([{ name: "missing" }]), TypeError);
throws("duplicate names", () => new Bench.Runner([task, task]), TypeError);
throws("samples positive", () => new Bench.Runner([task], { samples: 0 }), RangeError);
throws("warmup integer", () => new Bench.Runner([task], { warmup: .5 }), RangeError);
throws("iterations bounded", () => new Bench.Runner([task], { iterations: 10001 }), RangeError);
for (const budget of [0, -1, NaN, Infinity, "16"]) {
    throws("invalid frame budget", () => new Bench.Runner([task], { frameBudgetMs: budget }), RangeError);
}
for (const key of ["beforeBatch", "afterBatch", "inspect"]) {
    throws("invalid " + key, () => new Bench.Runner([{ ...task, [key]: 1 }]), TypeError);
}

let setups = 0, calls = 0, cleanups = 0, checkpoints = 0;
const runner = new Bench.Runner([{
    name: "work", setup() { setups++; return { n: 0 }; },
    run(state) { calls++; state.n++; },
    teardown(state) { cleanups++; check("context retained", state.n === 18); },
}], { warmup: 2, samples: 4, iterations: 3, onResult(result, report) {
    checkpoints++; check("checkpoint after cleanup", cleanups === 1 && report.results[0] === result);
} });
let frames = 0; do { frames++; } while (runner.step());
const r = runner.report.results[0];
check("one batch per step", frames === 6 && calls === 18 && setups === 1 && cleanups === 1 && checkpoints === 1);
check("warmup excluded", r.samples === 4 && r.iterations === 3 && runner.report.status === "completed");
check("ordered statistics", r.minMs <= r.averageMs + 1e-6 && r.averageMs <= r.maxMs + 1e-6 && r.p95Ms >= r.minMs && r.p95Ms <= r.maxMs);
check("report serializable", JSON.parse(JSON.stringify(runner.report)).results.length === 1);
check("report v2 stores frame budget", runner.report.version === 2 && runner.report.frameBudgetMs > 16);
check("all successful raw samples retained", r.sampleTicks.length === 4 && r.sampleMs.length === 4 && r.batchCpu.sampleMs.length === 4);
check("sample summaries match raw data", Math.abs(r.sampleMs.reduce((a, b) => a + b, 0) / 4 - r.averageMs) < 1e-6);
check("batch times include all iterations", r.batchCpu.sampleMs.every((v, i) => v + .001 >= r.sampleMs[i] * 3));
check("finished step does no work", !runner.step() && calls === 18);

let teardown = 0, next = 0;
const bad = new Bench.Runner([
    { name: "bad", run() { throw new Error("failure"); }, teardown() { teardown++; } },
    { name: "next", run() { next++; } },
], { warmup: 0, samples: 1 });
while (bad.step()) {}
check("failed task cleans up and next task runs", teardown === 1 && next === 1 && bad.report.status === "error" && /failure/.test(bad.report.results[0].error));
const badSetup = new Bench.Runner([{ name: "setup", setup() { throw 0; }, run() {}, teardown() { teardown++; } }]);
badSetup.step();
check("falsy thrown values are failures", badSetup.report.status === "error" && teardown === 2);
const asyncTask = new Bench.Runner([{ name: "async", run() { return Promise.resolve(); } }]);
asyncTask.step();
check("async callbacks rejected", asyncTask.report.results[0].status === "error");

let cancelledCalls = 0, cancelledCleanup = 0;
let cancel;
cancel = new Bench.Runner([{ name: "cancel", run() { cancelledCalls++; cancel.cancel(); }, teardown() { cancelledCleanup++; } }], { iterations: 5 });
check("cancel inside work stops batch", !cancel.step() && cancelledCalls === 1 && cancelledCleanup === 1 && cancel.report.status === "cancelled");
cancel.cancel(); check("cancel is idempotent", cancelledCleanup === 1);
const early = new Bench.Runner([{ name: "early", setup() { throw new Error("must not run"); }, run() {} }]);
early.cancel(); check("cancel before setup", early.done && early.report.results.length === 0);

let recursive;
recursive = new Bench.Runner([{ name: "recursive", run() { recursive.step(); } }]);
recursive.step();
check("recursive step fails safely", /recursively/.test(recursive.report.results[0].error));
const checkpointError = new Bench.Runner([task], { warmup: 0, samples: 1, onResult() { throw new Error("write failed"); } });
throws("checkpoint failure propagates", () => checkpointError.step(), Error);
check("checkpoint failure sets status", checkpointError.report.status === "error");

const order = [];
const phases = new Bench.Runner([{
    name: "phases", metadata: { bytes: 64, gsWait: false }, setup() { order.push("setup"); return { calls: 0 }; },
    beforeBatch(s) { order.push("prepare"); check("prepare retains context", s.calls % 2 === 0); },
    run(s) { order.push("work"); s.calls++; },
    afterBatch() { order.push("cleanup"); },
    inspect(s) { order.push("inspect"); return { calls: s.calls }; },
    teardown() { order.push("teardown"); },
}], { warmup: 1, samples: 2, iterations: 2 });
while (phases.step()) {}
check("hooks include warmup and inspect precedes teardown", order.join() === "setup,prepare,work,work,cleanup,prepare,work,work,cleanup,prepare,work,work,cleanup,inspect,teardown");
check("metrics and task metadata retained", phases.report.results[0].metrics.calls === 6 && phases.report.results[0].metadata.bytes === 64);
for (const failure of ["beforeBatch", "run", "afterBatch", "inspect"]) {
    let cleaned = 0, released = 0;
    const t = { name: failure, run() {}, afterBatch() { cleaned++; }, teardown() { released++; } };
    t[failure] = () => { if (failure === "afterBatch") cleaned++; throw new Error(failure); };
    const test = new Bench.Runner([t], { warmup: 0, samples: 1 });
    test.step();
    check(failure + " failure cleans up", cleaned === 1 && released === 1 && test.report.status === "error");
    check(failure + " excludes failed batch", test.report.results[0].samples === (failure === "inspect" ? 1 : 0));
}
let cancelledBatchCleanup = 0;
let cancelPrepare;
cancelPrepare = new Bench.Runner([{ name: "cancelPrepare", beforeBatch() { cancelPrepare.cancel(); },
    run() { throw new Error("must not run"); }, afterBatch() { cancelledBatchCleanup++; } }]);
cancelPrepare.step();
check("cancel in preparation still cleans up", cancelledBatchCleanup === 1 && cancelPrepare.report.results[0].samples === 0 && cancelPrepare.report.status === "cancelled");
const asyncCleanup = new Bench.Runner([{ name: "asyncCleanup", run() {}, afterBatch() { return Promise.resolve(); } }]);
asyncCleanup.step();
check("async batch hooks rejected", asyncCleanup.report.status === "error");

// The real libc writes to a writable boot folder; host runner uses a fake
// file to check JSON checkpoints and flush failures without device access.
const host = typeof globalThis.__nativeDraws === "function";
if (host) {
    const oldOpen = std.open;
    let text = "", closed = 0, ioError = false;
    std.open = () => ({ puts(s) { text = s; }, flush() {}, error() { return ioError; }, close() { closed++; } });
    try {
        Bench.save(runner.report, "results.json");
        check("save writes JSON and closes", JSON.parse(text).results.length === 1 && closed === 1);
        ioError = true;
        throws("write failure", () => Bench.save(runner.report, "results.json"), Error);
        check("failed write closes", closed === 2);
    } finally { if (oldOpen === undefined) delete std.open; else std.open = oldOpen; }
} else {
    Bench.save(runner.report, "bench_test_results.json");
    check("save on boot device", JSON.parse(std.loadFile("bench_test_results.json")).results.length === 1);
}

async function testLoop() {
    const report = await Bench.run([task], { warmup: 1, samples: 3 });
    check("Loop integration resolves", report.status === "completed" && report.results[0].samples === 3);
    check("Loop system removed", !Loop.getSystems().some(s => s.priority === 900000));
    Loop.stop();
    console.log(`Result: ${passed} passed, ${failed} failed`);
    if (!failed) console.log("Bench module test passed");
}
testLoop().catch(e => { Loop.stop(); console.log("[FAIL] Bench async " + e); });
Loop.run(() => {});
