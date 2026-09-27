/*
 * Debug module: overlay, console, watches, shapes, shortcut and the manual
 * frame. Runs on PCSX2/PS2 and on the host (tests/js/run.sh), where stubs of
 * Draw, Font and the GS count what is drawn and set the pad (__debugCalls,
 * __nativeDraws, __setPad); those checks are skipped on the console.
 */
import * as Loop from "Loop";
import * as Debug from "Debug";
import * as DebugNative from "DebugNative";
import * as Screen from "Screen";
import { Font } from "Font";
import * as System from "System";

let passed = 0, failed = 0;

function check(name, condition) {
    if (condition) {
        passed++;
    } else {
        failed++;
        console.log("[FAIL] " + name);
    }
}

function throws(name, callback, type) {
    let error = null;
    try { callback(); } catch (e) { error = e; }
    check(name + " throws" + (type ? " " + type.name : ""),
        error !== null && (!type || error instanceof type));
}

const host = typeof globalThis.__setPad === "function";
const RED = 0x800000FF;
const L3 = 0x0002, R3 = 0x0004;

const calls = () => globalThis.__debugCalls || [];
const resetCalls = () => { if (host) { globalThis.__debugCalls = []; globalThis.__nativeDraws(); } };
const printed = () => calls().filter(c => c[0] === "print").map(c => c[3]).join("\n");
const debugSystem = () => Loop.getSystems().some(s => s.name === "debug");

/* Runs `count` whole frames (postDraw included), calling draw(i) in each. */
function frames(count, draw) {
    return new Promise(resolve => {
        let frame = 0;
        Loop.run({
            draw() {
                if (frame === count) {
                    Loop.stop();
                    resolve();
                    return;
                }
                if (draw) draw(frame);
                frame++;
            },
        });
    });
}

check("Debug global", globalThis.Debug === Debug);

// --- Arguments -------------------------------------------------------------------

throws("rect without numbers", () => Debug.rect("a", 0, 1, 1), TypeError);
throws("rect with NaN", () => Debug.rect(0, NaN, 1, 1), TypeError);
throws("bad space", () => Debug.line(0, 0, 1, 1, RED, { space: "camera" }), TypeError);
throws("bad options", () => Debug.circle(0, 0, 1, RED, 5), TypeError);
throws("watch without a function", () => Debug.watch("x", 5), TypeError);
throws("console lines out of range", () => Debug.console(true, { lines: 0 }), RangeError);
throws("toggleWith port 2", () => Debug.toggleWith(L3, 2), RangeError);
throws("toggleWith no buttons", () => Debug.toggleWith(0), RangeError);
throws("configure budget", () => Debug.configure({ budgetMs: -1 }), RangeError);
throws("configure font", () => Debug.configure({ font: {} }), TypeError);
throws("setView scale", () => Debug.setView({ scale: Infinity }), TypeError);
check("nothing registered by failed calls", !debugSystem());

// --- System lifetime ---------------------------------------------------------------

check("overlay off by default", Debug.overlay() === false);
check("overlay(true) returns true", Debug.overlay(true) === true);
check("a Loop system while on", debugSystem());
check("overlay(false)", Debug.overlay(false) === false);
check("system removed when nothing is on", !debugSystem());

// --- Console tail from the runtime's output buffer ----------------------------------

console.log("debug-test line one");
console.log("debug-test line two");
const tail = DebugNative.output(2, 0);
check("output() returns the last printed lines", tail === "debug-test line one\ndebug-test line two");
const version = DebugNative.outputVersion();
console.log("more");
check("outputVersion changes when printing", DebugNative.outputVersion() !== version);
check("output() wraps at columns", DebugNative.output(1, 2) === "re");
throws("output lines above 1000", () => DebugNative.output(1001), RangeError);

// --- Frame-time graph ---------------------------------------------------------------

DebugNative.reset();
check("stats of an empty graph", DebugNative.stats().samples === 0);
DebugNative.record(16.7, 4);
DebugNative.record(33.4, 12);
const figures = DebugNative.stats();
check("stats after two frames", figures.samples === 2 && Math.abs(figures.cpuAvg - 8) < 1e-4 &&
    Math.abs(figures.cpuMax - 12) < 1e-4 && Math.abs(figures.frameMax - 33.4) < 1e-4);
throws("record NaN", () => DebugNative.record(NaN, 1), RangeError);
if (host) {
    globalThis.__nativeDraws();
    const bars = DebugNative.graph(0, 0, 100, 30);
    const drawn = globalThis.__nativeDraws();
    check("graph draws one sprite per bar and the budget line", bars === 2 && drawn.sprites === 2 && drawn.lines === 1);
    // The usual case: bars about a pixel wide are lines, all in one batch.
    for (let i = 0; i < 240; i++) DebugNative.record(16.7, i % 20);
    const narrow = DebugNative.graph(0, 0, 200, 36);
    const batched = globalThis.__nativeDraws();
    check("narrow bars drawn as one batch of lines", narrow === 200 && batched.sprites === 0 &&
        batched.lineLists === 1 && batched.lines === 200 - Math.ceil(200 / 20) + 1);
}
DebugNative.reset();

async function frameTests() {
    // Overlay and console over a few frames.
    let player = { x: 12.7, y: 40.2, state: "idle" };
    Debug.watch("player", () => `${player.x | 0},${player.y | 0} ${player.state}`);
    Debug.watch("broken", () => { throw new Error("oops"); });
    Debug.overlay(true);
    Debug.console(true, { lines: 3 });
    console.log("shown in the console");
    resetCalls();
    await frames(4);
    const samples = Debug.frameStats().samples;
    check("the overlay records a sample per frame", samples >= 4);
    if (host) {
        const text = printed();
        check("overlay shows the FPS", /60\.0 FPS/.test(text));
        check("overlay shows memory and VRAM", /RAM 12\.0 MB used, 18\.0 MB free/.test(text) &&
            /VRAM 1536 KB free/.test(text));
        // The heap line walks the whole heap (6.7 ms on the PS2): off by default.
        check("no heap walk by default", !/JS \d/.test(text) && System.__memoryStatsCalls === 0);
        check("watch value shown", /player: 12,40 idle/.test(text));
        check("watch error shown inline", /broken: <oops>/.test(text));
        check("console shows the output", /shown in the console/.test(text));
        check("graph drawn by C", globalThis.__nativeDraws().sprites > 0);
        check("panels drawn", calls().filter(c => c[0] === "rect").length >= 2);
    }

    // The text refreshes 4 times per second, not every frame.
    player.state = "running";
    resetCalls();
    await frames(2);
    if (host) check("watch not re-read every frame", !/running/.test(printed()));
    await frames(16);   // over 0.25 s of 1/60 frames
    resetCalls();
    await frames(1);
    if (host) check("watch refreshed after a quarter second", /player: 12,40 running/.test(printed()));
    check("unwatch", Debug.unwatch("broken") === true && Debug.unwatch("broken") === false);
    Debug.overlay(false);
    Debug.console(false);

    // Shapes: one frame by default, `seconds` in real time, world space.
    resetCalls();
    Debug.rect(10, 20, 30, 40, RED);
    Debug.rect(1, 2, 3, 4, RED, { filled: true, seconds: 0.05 });
    Debug.setView({ x: 100, y: 50, scale: 2 });
    Debug.circle(110, 60, 5, RED, { space: "world" });
    Debug.text(0, 0, "hit!", { seconds: 0.05 });
    await frames(1);
    if (host) {
        // Rects, lines and circles are drawn by C (counted by the runner's stubs).
        const drawn = globalThis.__nativeDraws();
        check("outline rect as four lines", drawn.lines === 4 &&
            drawn.firstLine[0] === 10 && drawn.firstLine[1] === 20);
        check("filled rect as a sprite", drawn.sprites === 1);
        check("world space through setView", drawn.circles === 1 && drawn.lastCircle[0] === 20 &&
            drawn.lastCircle[1] === 20 && drawn.lastCircle[2] === 10 && drawn.lastCircle[3] === 0);
        check("text drawn", /hit!/.test(printed()));
        check("shapes do not go through Draw", !calls().some(c => c[0] === "line" || c[0] === "circle"));
    }
    resetCalls();
    await frames(1);
    if (host) {
        const drawn = globalThis.__nativeDraws();
        check("one-frame shapes are gone", drawn.lines === 0 && drawn.circles === 0);
        check("timed shapes stay", drawn.sprites === 1 && /hit!/.test(printed()));
    }
    await frames(4);
    resetCalls();
    await frames(1);
    if (host) {
        const drawn = globalThis.__nativeDraws();
        check("timed shapes expire", calls().length === 0 && drawn.sprites === 0);
    }
    check("system released when the shapes are gone", !debugSystem());
    Debug.setView({});

    // Shortcut: the pad is read directly, the combination toggles everything.
    if (host) {
        Debug.overlay(true);
        Debug.toggleWith(L3 | R3);
        globalThis.__setPad(L3);   // one button: nothing
        await frames(1);
        check("partial combination does nothing", Debug.show() === true);
        globalThis.__setPad(L3 | R3);
        await frames(1);
        check("combination hides", Debug.show() === false);
        resetCalls();
        await frames(2);           // still held: no second toggle
        check("held combination toggles once", Debug.show() === false);
        check("hidden: nothing drawn", calls().length === 0);
        globalThis.__setPad(0);
        await frames(1);
        globalThis.__setPad(L3 | R3);
        await frames(1);
        check("pressed again shows", Debug.show() === true);
        globalThis.__setPad(0);
        Debug.toggleWith(null);
        Debug.overlay(false);
    }
    check("show() sets and returns", Debug.show(false) === false && Debug.show(true) === true);
    check("everything off: no system", !debugSystem());

    // Games without Loop.run(): Debug.frame() draws by itself.
    DebugNative.reset();
    resetCalls();
    Debug.overlay(true);
    Debug.frame(1 / 30, 7);
    check("frame() records its sample", Debug.frameStats().samples === 1 &&
        Math.abs(Debug.frameStats().frameAvg - 1000 / 30) < 0.01);
    if (host) check("frame() draws the overlay", /30\.0 FPS/.test(printed()));
    Debug.overlay(false);
    throws("frame without dt", () => Debug.frame(), TypeError);
}

/* Colors, bounded shapes, layout, frame budget, console colors, self cost and errors. */
async function robustnessTests() {
    // Bad colors fail at the call, not later inside the Loop.
    throws("rect with a string color", () => Debug.rect(0, 0, 1, 1, "red"), TypeError);
    throws("text with a string color", () => Debug.text(0, 0, "x", { color: "red" }), TypeError);
    throws("line with a fractional color", () => Debug.line(0, 0, 1, 1, 1.5), TypeError);
    Debug.rect(0, 0, 1, 1, -2147483520);   // an int32 packing is accepted
    Debug.clear();

    // Shapes are bounded: the oldest are dropped, and the overlay says so.
    for (let i = 0; i < 5000; i++) Debug.rect(i, 0, 1, 1, RED, { seconds: 10 });
    Debug.overlay(true);
    resetCalls();
    await frames(1);
    if (host) {
        // + 1: the budget line of the overlay graph.
        const drawn = globalThis.__nativeDraws();
        check("the newest 2048 shapes are kept", drawn.lines === 4 * 2048 + 1);
        // 8192 outline edges in batches of 256: 32 packets, not 8192; plus the
        // overlay graph, which is one more batch.
        check("outlines drawn in batches", drawn.lineLists === 32 + 1);
        check("dropped shapes reported", /shapes dropped: 2952 \(queue full\)/.test(printed()));
    }
    Debug.clear();
    Debug.overlay(false);
    // Hidden: shapes are not drawn but still expire.
    Debug.rect(0, 0, 5, 5, RED);
    Debug.show(false);
    resetCalls();
    await frames(1);
    Debug.show(true);
    await frames(1);
    if (host) check("hidden one-frame shapes expire undrawn", globalThis.__nativeDraws().lines === 0);
    throws("native shape kind", () => DebugNative.shape(7, 0, 0, 1, 1, RED, 0, 0), RangeError);

    // Batches: 4 floats per shape, one call; groups that are not finite skipped.
    check("rects queues the finite groups",
        Debug.rects(new Float32Array([10, 20, 30, 40, NaN, 0, 1, 1, 5, 5, Infinity, 1]), RED) === 1);
    check("lines queues a segment", Debug.lines(new Float32Array([0, 0, 50, 50]), RED) === 1);
    resetCalls();
    await frames(1);
    if (host) {
        const drawn = globalThis.__nativeDraws();
        check("batched shapes drawn", drawn.lines === 5 && drawn.firstLine[0] === 10 &&
            drawn.firstLine[1] === 20 && drawn.lineLists === 1);
    }
    throws("rects of a plain array", () => Debug.rects([1, 2, 3, 4]), TypeError);
    throws("rects with a length not multiple of 4", () => Debug.rects(new Float32Array(6)), RangeError);
    throws("rects with a bad color", () => Debug.rects(new Float32Array(4), "red"), TypeError);
    check("empty batch registers nothing", Debug.rects(new Float32Array(0)) === 0);
    throws("native shape NaN", () => DebugNative.shape(0, NaN, 0, 1, 1, RED, 0, 0), RangeError);
    Debug.overlay(true);

    // Layout: the title-safe area by default, a margin when configured, and
    // a panel as wide as its text.
    Debug.watch("long", () => "x".repeat(100));
    resetCalls();
    await frames(1);
    if (host) {
        const panel = calls().find(c => c[0] === "rect");
        check("panel inside the safe area", panel && panel[1] === 32 && panel[2] === 22);
        const text = printed();
        check("long watch clipped", /long: x{39}\.\.\.$/m.test(text) && !/x{40}/.test(text));
        const widest = Math.max(...text.split("\n").map(line => line.length)) * 8;
        check("panel as wide as its text", panel[3] === Math.max(widest, 200) + 8);
        check("overlay shows its own cost", /debug \d+\.\d\d ms/.test(text));
        check("NTSC budget", /budget 16\.7/.test(text));
    }
    Debug.unwatch("long");
    Debug.configure({ margin: 8, vsyncInterval: 2 });
    resetCalls();
    await frames(1);
    if (host) {
        const panel = calls().find(c => c[0] === "rect");
        check("configured margin", panel && panel[1] === 8 && panel[2] === 8);
        check("vsyncInterval 2 doubles the budget", /budget 33\.3/.test(printed()));
        Screen.setMode({ mode: Screen.PAL, width: 640, height: 512, psm: 0 });
        Debug.configure({ vsyncInterval: 1 });
        resetCalls();
        await frames(1);
        check("PAL budget", /budget 20\.0/.test(printed()));
        Debug.configure({ budgetMs: 12.5 });
        resetCalls();
        await frames(1);
        check("explicit budget", /budget 12\.5/.test(printed()));
        Screen.setMode({ mode: Screen.NTSC, width: 640, height: 448, psm: 0 });
    }
    Debug.configure({ margin: null, budgetMs: 0, vsyncInterval: 1 });
    throws("configure vsyncInterval 5", () => Debug.configure({ vsyncInterval: 5 }), RangeError);
    throws("configure margin string", () => Debug.configure({ margin: "big" }), TypeError);
    Debug.overlay(false);

    // Console: error lines in red, the rest in white.
    Debug.console(true, { lines: 4 });
    console.log("Error: something broke");
    console.log("all good");
    resetCalls();
    await frames(1);
    if (host) {
        const prints = calls().filter(c => c[0] === "print");
        const failLine = prints.find(c => /something broke/.test(c[3]));
        const goodLine = prints.find(c => /all good/.test(c[3]));
        check("error lines in red", failLine && failLine[4] !== goodLine[4]);
        resetCalls();
        await frames(1);
        check("console laid out once, drawn each frame",
            !calls().some(c => c[0] === "render") && calls().some(c => c[0] === "print"));
    }
    Debug.console(false);

    // Debug.frame() inside Loop.run(): skipped, with one warning.
    Debug.overlay(true);
    const before = Debug.frameStats().samples;
    await frames(3, () => Debug.frame(1 / 60));
    check("frame() under Loop.run() does not draw twice", Debug.frameStats().samples - before <= 3);
    check("frame() under Loop.run() warns once",
        DebugNative.output(40).split("\n").filter(line => /already draws/.test(line)).length === 1);
    Debug.overlay(false);

    // An error while drawing turns the module off; the game goes on.
    if (host) {
        const broken = new Font();
        Debug.configure({ font: broken });
        Debug.overlay(true);
        broken.free();
        await frames(2);
        check("an error while drawing turns the module off", Debug.overlay() === false && !debugSystem());
        check("the error is logged", /turned off after an error/.test(DebugNative.output(40)));
        Debug.overlay(true);
        resetCalls();
        await frames(1);
        check("turning it on again uses the built-in font", /FPS/.test(printed()));
        Debug.overlay(false);
    }
}

/* Overlay options: without the graph, and compact (FPS line and watches). */
async function overlayOptionTests() {
    throws("overlay options", () => Debug.overlay(true, 5), TypeError);
    Debug.watch("kept", () => "yes");
    Debug.overlay(true, { graph: false });
    resetCalls();
    await frames(1);
    if (host) {
        check("graph: false draws no graph", globalThis.__nativeDraws().lineLists === 0);
        check("graph: false keeps the text", /RAM/.test(printed()));
    }
    Debug.overlay(true, { graph: true, compact: true });
    resetCalls();
    await frames(1);
    if (host) {
        const text = printed();
        check("compact keeps the FPS line and the watches", /FPS/.test(text) && /kept: yes/.test(text) &&
            !/RAM|VRAM|JS /.test(text));
        check("compact keeps the graph", globalThis.__nativeDraws().lineLists === 1);
    }
    // heap: true adds the JS heap line, read once every 5 seconds.
    Debug.overlay(true, { compact: false, heap: true });
    const walksBefore = host ? System.__memoryStatsCalls : 0;
    resetCalls();
    await frames(60);   // one second of 1/60 frames
    if (host) {
        check("heap line shown", /JS 2\.0\/8\.0 MB, 4321 objects \(every 5 s\)/.test(printed()));
        check("heap walked once in a second", System.__memoryStatsCalls - walksBefore === 1);
    }
    Debug.overlay(true, { heap: false });
    resetCalls();
    await frames(2);
    if (host) check("heap: false drops the line", !/JS \d/.test(printed()));
    Debug.overlay(false, { compact: false });
    Debug.unwatch("kept");
}

/* Costs to compare on the console: queuing hitboxes and drawing the overlay. */
async function timings() {
    const now = () => System.getMilliseconds();
    let start = now();
    for (let i = 0; i < 1000; i++) Debug.rect(i % 600, (i * 7) % 400, 16, 16, RED);
    const queueMs = now() - start;
    Debug.clear();
    const boxes = new Float32Array(4000);
    for (let i = 0; i < 1000; i++) boxes.set([i % 600, (i * 7) % 400, 16, 16], i * 4);
    start = now();
    Debug.rects(boxes, RED);
    const batchMs = now() - start;
    start = now();
    await frames(1);   // draws the 1000 outlines (4000 lines) from C
    const drawMs = now() - start;
    // CPU time of the frame (Loop's cpuMs: work without the VSync wait); the
    // frame time itself is pinned to 16.7 ms by VSync and hides the cost.
    const cpuOver = async (count) => {
        let sum = 0;
        await frames(count, () => { sum += Loop.getStats().cpuMs; });
        return sum / count;
    };
    // Where the overlay's time goes: each part alone, over 60 frames.
    const cost = async (setup) => {
        setup();
        await frames(10);
        const ms = await cpuOver(60);
        Debug.overlay(false, { graph: true, compact: false });
        Debug.console(false);
        return ms;
    };
    Debug.overlay(true);
    Debug.console(true);
    await frames(30);   // the font's preload slices end first
    const withOverlay = await cpuOver(60);
    Debug.overlay(false);
    Debug.console(false);
    await frames(10);
    const without = await cpuOver(60);
    const overlayOnly = await cost(() => Debug.overlay(true));
    const noGraph = await cost(() => Debug.overlay(true, { graph: false }));
    const compact = await cost(() => Debug.overlay(true, { compact: true }));
    const consoleOnly = await cost(() => Debug.console(true));
    console.log(`timings: 1000 Debug.rect ${queueMs.toFixed(2)} ms to queue (Debug.rects ${batchMs.toFixed(2)} ms), ` +
        `${drawMs.toFixed(2)} ms ` +
        `to draw; frame CPU with overlay and console ${withOverlay.toFixed(2)} ms, ` +
        `without ${without.toFixed(2)} ms (overlay ${(withOverlay - without).toFixed(2)} ms)`);
    const part = (ms) => (ms - without).toFixed(2);
    // What the overlay reads from System every refresh: memory walks the heap.
    start = now();
    for (let i = 0; i < 10; i++) System.getMemoryStats();
    const memoryMs = (now() - start) / 10;
    console.log(`timings: overlay alone ${part(overlayOnly)} ms, without graph ${part(noGraph)} ms, ` +
        `compact ${part(compact)} ms, console alone ${part(consoleOnly)} ms; ` +
        `System.getMemoryStats() ${memoryMs.toFixed(2)} ms per call`);
}

frameTests()
    .then(robustnessTests)
    .then(overlayOptionTests)
    .then(timings)
    .catch(error => { failed++; console.log("[FAIL] " + error + "\n" + (error.stack || "")); })
    .then(() => {
        Debug.overlay(false);
        Debug.console(false);
        Debug.clear();
        Debug.toggleWith(null);
        console.log(`Result: ${passed} passed, ${failed} failed` + (host ? "" : " (host-only checks skipped)"));
        if (!failed) console.log("Debug module test passed");
    });
