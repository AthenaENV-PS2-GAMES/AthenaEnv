import * as Loop from "Loop";

function assert(name, condition) {
    if (!condition) throw new Error(name);
}

function expectThrow(name, callback) {
    let threw = false;
    try {
        callback();
    } catch (error) {
        threw = true;
    }
    assert(name + " should throw", threw);
}

const noop = () => {};
expectThrow("run without handlers", () => Loop.run());
expectThrow("run with a number", () => Loop.run(1));
expectThrow("run with empty handlers", () => Loop.run({}));
expectThrow("run with a non-function update", () => Loop.run({ update: 1 }));
expectThrow("run with invalid options", () => Loop.run(noop, 1));
expectThrow("run with a negative maxDelta", () => Loop.run(noop, { maxDelta: -1 }));
expectThrow("run with a non-boolean clear", () => Loop.run(noop, { clear: 1 }));
expectThrow("run with a negative fixedStep", () => Loop.run(noop, { fixedStep: -1 }));
expectThrow("run with a fractional maxSteps", () => Loop.run(noop, { maxSteps: 1.5 }));
expectThrow("run with a zero vsyncInterval", () => Loop.run(noop, { vsyncInterval: 0 }));
expectThrow("negative time scale", () => Loop.setTimeScale(-1));
assert("loop idle before run", !Loop.isRunning());
assert("default time scale", Loop.getTimeScale() === 1);

// Systems: validation and registry.
expectThrow("addSystem without a system", () => Loop.addSystem());
expectThrow("addSystem with a number", () => Loop.addSystem(1));
expectThrow("addSystem with a function", () => Loop.addSystem(noop));
expectThrow("addSystem without phases", () => Loop.addSystem({ name: "empty" }));
expectThrow("addSystem with a non-function phase", () => Loop.addSystem({ update: 1 }));
expectThrow("addSystem with a fractional priority", () => Loop.addSystem({ update: noop, priority: 1.5 }));
expectThrow("addSystem with a non-boolean realTime", () => Loop.addSystem({ update: noop, realTime: 1 }));
expectThrow("addSystem with a non-string name", () => Loop.addSystem({ update: noop, name: 1 }));
expectThrow("removeSystem with a number", () => Loop.removeSystem(1));
{
    const probe = { name: "probe", priority: 3, postDraw: noop, update: noop };
    assert("addSystem returns the system", Loop.addSystem(probe) === probe);
    expectThrow("addSystem twice", () => Loop.addSystem(probe));
    expectThrow("addSystem with a taken name", () => Loop.addSystem({ name: "probe", update: noop }));
    const info = Loop.getSystems().find(s => s.name === "probe");
    assert("getSystems lists the system", info && info.priority === 3 && !info.native &&
        !info.realTime && info.phases.join() === "update,postDraw");
    assert("removeSystem by name", Loop.removeSystem("probe"));
    assert("removeSystem of a removed system", !Loop.removeSystem(probe));
    assert("system re-added after removal", Loop.addSystem(probe) === probe);
    assert("removeSystem by object", Loop.removeSystem(probe));
    assert("no JavaScript systems left", !Loop.getSystems().some(s => !s.native));
}

// Frames start after this script. Several timers expiring together all run
// in the same frame.
const timerFrames = [];
for (let i = 0; i < 4; i++) setTimeout(() => timerFrames.push(Loop.getFrameCount()), 100);

// Phase 1: variable step.
const deltas = [];
Loop.run(dt => {
    deltas.push(dt);
    if (deltas.length === 30) startFixed();
}, { maxDelta: 0.1 });
assert("loop running after run", Loop.isRunning());
assert("no frame before the script ends", Loop.getFrameCount() === 0);

// Phase 2: fixed step with handlers, then 30 FPS and a paused game.
const STEP = 1 / 60;
function startFixed() {
    assert("first delta is zero", deltas[0] === 0);
    assert("deltas within maxDelta", deltas.every(d => d >= 0 && d <= 0.1));
    assert("expired timers run in the same frame",
        timerFrames.length === 4 && timerFrames.every(f => f === timerFrames[0]));

    const game = {
        frames: 0,
        updates: 0,
        steps: [],
        update(step) {
            assert("update gets the fixed step", Math.abs(step - STEP) < 1e-6);
            assert("this is the handlers object", this === game);
            this.updates++;
        },
        draw(alpha) {
            assert("alpha within 0..1", alpha >= 0 && alpha <= 1);
            this.frames++;
            this.steps.push(Loop.getStats().steps);
            if (this.frames === 60) startSlow(this);
        },
    };
    Loop.run(game, { fixedStep: STEP });
}

function startSlow(game) {
    // Updates follow elapsed time: about one per frame at 60 Hz, or 50 Hz on PAL.
    assert("fixed updates happened", game.updates >= 45 && game.updates <= 75);
    assert("steps per frame bounded", game.steps.every(s => s >= 0 && s <= 5));

    const start = Loop.getRealElapsedTime();
    let frames = 0;
    Loop.run({
        draw() {
            if (++frames < 30) return;
            const perFrame = (Loop.getRealElapsedTime() - start) / (frames - 1);
            assert("vsyncInterval 2 halves the frame rate", perFrame > 0.03 && perFrame < 0.05);
            startPaused();
        },
    }, { vsyncInterval: 2 });
}

function startPaused() {
    const elapsed = Loop.getElapsedTime();
    let updates = 0, frames = 0;
    Loop.setTimeScale(0);
    Loop.run({
        update() { updates++; },
        draw() {
            if (++frames < 20) return;
            assert("paused game does not update", updates === 0);
            assert("paused game time is frozen", Loop.getElapsedTime() === elapsed);
            Loop.setTimeScale(1);
            startSystems();
        },
    }, { fixedStep: STEP });
}

// Phase order of systems and handlers, systems added and removed while the
// loop runs, and real time. Each frame's trace is checked by the first system
// of the next frame.
function startSystems() {
    const trace = [];
    const frame = ["a.pre", "b.update", "a.update", "handler.update", "a.post",
        "a.preDraw", "handler.draw", "a.postDraw"];
    const withLate = ["late", ...frame];
    const withoutB = withLate.filter(step => step !== "b.update");
    let frames = 0;

    const same = (a, b) => a.length === b.length && a.every((step, i) => step === b[i]);
    const first = Loop.addSystem({
        name: "first",
        priority: -1000,
        preUpdate() {
            const expected = [null, frame, frame, withLate, withLate, withoutB][frames];
            if (expected) assert("system order in frame " + frames + ": " + trace.join(), same(trace, expected));
            trace.length = 0;
            if (frames === 5) finishSystems();
        },
    });
    const a = Loop.addSystem({
        name: "a",
        priority: 1,
        preUpdate() { trace.push("a.pre"); },
        update() { trace.push("a.update"); },
        postUpdate() { trace.push("a.post"); },
        preDraw() { trace.push("a.preDraw"); },
        postDraw() {
            trace.push("a.postDraw");
            if (frames === 2) Loop.addSystem(late);  // runs from the next frame
        },
    });
    const b = Loop.addSystem({
        name: "b",
        update() {
            trace.push("b.update");
            if (frames === 3) assert("b removes itself", Loop.removeSystem(this));
        },
    });
    const late = { name: "late", priority: -10, preUpdate() { trace.push("late"); } };
    const real = Loop.addSystem({
        realTime: true,
        preUpdate(dt) { this.real = dt; },
        update(dt) { this.scaled = dt; },
        postUpdate(dt) {
            if (this.scaled > 0)
                assert("realTime systems get the real delta", Math.abs(this.real - 2 * this.scaled) < 1e-6 && dt === this.real);
        },
    });
    Loop.setTimeScale(0.5);

    Loop.run({
        update() { trace.push("handler.update"); },
        draw() {
            trace.push("handler.draw");
            frames++;
        },
    });

    function finishSystems() {
        Loop.setTimeScale(1);
        assert("systems in run order", Loop.getSystems().filter(s => !s.native)
            .map(s => s.name).join() === "first,late,,a");
        Loop.stop();
        assert("systems survive Loop.stop", Loop.getSystems().some(s => s.name === "a"));
        for (const system of [first, late, a, real]) assert("remove system", Loop.removeSystem(system));
        assert("b was removed", !Loop.removeSystem(b));
        finish();
    }
}

function finish() {
    const stats = Loop.getStats();
    Loop.stop();
    assert("loop stopped", !Loop.isRunning());
    assert("fps measured", stats.fps > 10);
    assert("cpu time measured", stats.cpuMs > 0 && stats.cpuMs < 100);
    console.log("Loop stats: " + stats.fps.toFixed(1) + " FPS, cpu " +
        stats.cpuMs.toFixed(2) + " ms, frame " + stats.frameMs.toFixed(2) + " ms");
    console.log("Loop module test passed");
}
