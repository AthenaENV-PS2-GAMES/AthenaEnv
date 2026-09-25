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
            finish();
        },
    }, { fixedStep: STEP });
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
