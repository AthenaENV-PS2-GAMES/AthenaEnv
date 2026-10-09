/*
 * Profiler module: scopes, counters, history stats, errors, Render3D
 * attachment and the Debug overlay lines. Runs on PCSX2/PS2 and on the host
 * (tests/js/run.sh); on the console it also prints the cost of a begin/end pair.
 */
import * as Loop from "Loop";
import * as Debug from "Debug";
import * as Profiler from "Profiler";

let passed = 0, failed = 0;
function check(name, condition) {
    if (condition) passed++;
    else { failed++; console.log("[FAIL] " + name); }
}
function throws(name, callback, type) {
    let error = null;
    try { callback(); } catch (e) { error = e; }
    check(name + " throws" + (type ? " " + type.name : ""), error !== null && (!type || error instanceof type));
}
function spin(n) { let x = 0; for (let i = 0; i < n; i++) x += Math.sqrt(i); return x; }

Profiler.reset(true);
check("exports", Profiler.HISTORY === 120 && Profiler.MAX_SCOPES === 64 && Profiler.MAX_DEPTH === 32);
const ticksBefore = Profiler.ticks();
spin(1000);
check("raw clock converts positive elapsed time", Profiler.ticksToMilliseconds((Profiler.ticks() - ticksBefore) >>> 0) > 0);
throws("ticks range", () => Profiler.ticksToMilliseconds(-1), RangeError);
throws("ticks overflow", () => Profiler.ticksToMilliseconds(0x100000000), RangeError);
throws("ticks fraction", () => Profiler.ticksToMilliseconds(1.5), RangeError);
throws("ticks type", () => Profiler.ticksToMilliseconds("10"), TypeError);
check("scope 0 is frame", Profiler.names()[0] === "frame" && Profiler.scope("frame") === 0);
const A = Profiler.scope("work");
check("ids are stable", Profiler.scope("work") === A && A > 0);
const C = Profiler.counter("items");
throws("kind clash", () => Profiler.counter("work"), TypeError);
throws("empty name", () => Profiler.scope(""), RangeError);
throws("long name", () => Profiler.scope("x".repeat(32)), RangeError);
throws("non-printable name", () => Profiler.scope("a\nb"), RangeError);
throws("scope needs a string", () => Profiler.scope(3), TypeError);
throws("bad id", () => Profiler.begin(999), RangeError);
throws("frame is not a user scope", () => Profiler.begin(0), RangeError);
throws("counter as timer", () => Profiler.begin(C), RangeError);
throws("non-finite count", () => Profiler.count(C, NaN), RangeError);
throws("end without begin", () => Profiler.end(), RangeError);
check("errors counted", Profiler.errors() === 1);

Profiler.frame();               // start a clean frame
for (let f = 0; f < 10; f++) {
    Profiler.begin(A); spin(2000); Profiler.end(A);
    Profiler.begin("work"); Profiler.begin("inner"); spin(500); Profiler.end("inner"); Profiler.end("work");
    Profiler.count(C, f); Profiler.count("items");
    Profiler.frame();
}
let s = Profiler.stats("work", 10);
check("timer samples", s.samples === 10 && s.kind === "timer" && s.name === "work");
check("timer values", s.last > 0 && s.average > 0 && s.peak >= s.p95 && s.p95 >= 0 && s.lastCalls === 2 && s.averageCalls === 2);
const inner = Profiler.stats("inner", 10);
check("inclusive: parent >= child", s.average >= inner.average && inner.lastCalls === 1);
check("whole history includes the starting frame", Profiler.stats(0).samples === 11);
const frame = Profiler.stats(0, 10);
check("frame scope covers the work", frame.samples === 10 && frame.average >= s.average);
const items = Profiler.stats(C, 10);
check("counter sums per frame", items.kind === "counter" && items.last === 10 && items.peak === 10 && items.lastCalls === 2);
check("counter average", Math.abs(items.average - 5.5) < 1e-6);
const out = { keep: 1 };
check("stats reuse out", Profiler.stats(C, 3, out) === out && out.keep === 1 && out.samples === 3 && out.average === 9);
throws("stats unknown name", () => Profiler.stats("nope"), RangeError);
throws("stats frames", () => Profiler.stats(C, -1), RangeError);

// end() names must match the innermost scope.
Profiler.begin("work"); Profiler.begin("inner");
throws("mismatched end", () => Profiler.end("work"), RangeError);
Profiler.end("inner"); Profiler.end("work");
check("mismatch counted", Profiler.errors() === 2);

// Open scopes are split at frame().
Profiler.begin("work"); spin(1000);
Profiler.frame();
check("open scope counted in the closing frame", Profiler.stats("work", 1).last > 0);
Profiler.end("work");
Profiler.frame();

check("measure returns", Profiler.measure("m", () => 42) === 42);
throws("measure closes on throw", () => Profiler.measure("m", () => { throw new Error("x"); }), Error);
Profiler.begin("work"); Profiler.end("work"); // the stack is balanced again
throws("measure needs fn", () => Profiler.measure("m", 1), TypeError);

check("describe timer", /ms p95 .* max /.test(Profiler.describe("work")));
check("describe counter", /^avg .* max /.test(Profiler.describe("items")));
const report = Profiler.report();
check("report has every scope", report.length === Profiler.names().length && report[0].name === "frame");

// Render3D attachment, with a stand-in exposing frameStats().
let pending = { triangles: 0, drawPasses: 0, pipelinePasses: 0, culledObjects: 0, cpuClipObjects: 0 };
const fakeRender = {
    frameStats(o = {}) { Object.assign(o, pending); pending = { triangles: 0, drawPasses: 0, pipelinePasses: 0, culledObjects: 0, cpuClipObjects: 0 }; return o; },
};
throws("attach needs Render3D", () => Profiler.attachRender3D({}), TypeError);
Profiler.attachRender3D(fakeRender, { warn: false });
pending = { triangles: 900, drawPasses: 3, pipelinePasses: 2, culledObjects: 1, cpuClipObjects: 1 };
Profiler.frame();
check("3d counters", Profiler.stats("3d.triangles", 1).last === 900 && Profiler.stats("3d.objects", 1).last === 3 &&
    Profiler.stats("3d.cpuClip", 1).last === 1);
check("C clip alert in the overlay line", /C CLIP!/.test(Profiler.describe("3d.cpuClip", 1)));
Profiler.attachRender3D(null);

// Overlay: one Debug watch per scope, removed when off.
check("overlay on", Profiler.overlay(true, { frames: 30 }) === true);
throws("overlay frames", () => Profiler.overlay(true, { frames: 0 }), RangeError);
Profiler.scope("late");
Profiler.frame();
check("overlay off", Profiler.overlay(false) === false);
Debug.overlay(false);

// auto(): a Loop system after the Debug overlay.
check("auto on", Profiler.auto(true) && Loop.getSystems().some(x => x.name === "profiler"));
check("auto off", !Profiler.auto(false) && !Loop.getSystems().some(x => x.name === "profiler"));

// Depth limit.
let depthError = null;
try { for (let i = 0; i <= Profiler.MAX_DEPTH; i++) Profiler.begin("deep"); } catch (e) { depthError = e; }
check("depth limit", depthError instanceof RangeError);
for (let i = 0; i < Profiler.MAX_DEPTH; i++) Profiler.end("deep");

// Cost of a begin/end pair (by id and by name), averaged over 2000 pairs.
Profiler.reset(false);
const N = 2000, P = Profiler.scope("pair"), OUTER = Profiler.scope("pairs");
Profiler.begin(OUTER); for (let i = 0; i < N; i++) { Profiler.begin(P); Profiler.end(P); } Profiler.end(OUTER);
Profiler.frame();
const byId = Profiler.stats(OUTER, 1).last * 1000 / N;
Profiler.begin(OUTER); for (let i = 0; i < N; i++) { Profiler.begin("pair"); Profiler.end("pair"); } Profiler.end(OUTER);
Profiler.frame();
const byName = Profiler.stats(OUTER, 1).last * 1000 / N;
console.log(`[Profiler] begin/end pair: ${byId.toFixed(2)} us by id, ${byName.toFixed(2)} us by name`);
check("pair cost measured", byId > 0 && byName > 0);

Profiler.reset(true);
check("reset forgets names", Profiler.names().length === 1);
throws("old ids invalid after forget", () => Profiler.begin(A), RangeError);

console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("Profiler module test passed");
