/*
 * Ease and Tween modules: the first JavaScript modules embedded in the
 * binary. Runs on PCSX2/PS2 and on the host (tests/js/run.sh, with a stand-in
 * Loop). Tweens are advanced by hand with Tween.update(), then by Loop.run().
 */
import * as Loop from "Loop";
import * as Ease from "Ease";
import * as Tween from "Tween";

let passed = 0, failed = 0;

function check(name, condition) {
    if (condition) {
        passed++;
    } else {
        failed++;
        console.log("[FAIL] " + name);
    }
}

function throws(name, callback) {
    let threw = false;
    try { callback(); } catch (error) { threw = true; }
    check(name + " throws", threw);
}

const near = (a, b, epsilon = 1e-6) => Math.abs(a - b) < epsilon;

// The globals are the embedded modules.
check("Ease global", globalThis.Ease === Ease);
check("Tween global", globalThis.Tween === Tween);

// --- Ease -------------------------------------------------------------------

check("61 curve names", Ease.names.length === 61);
for (const name of Ease.names) {
    const f = Ease.get(name);
    check(name + " starts at 0", near(f(0), 0) && near(f(-0.5), 0));
    check(name + " ends at 1", near(f(1), 1) && near(f(1.5), 1));
    check(name + " is a number midway", Number.isFinite(f(0.37)));
}
// Values from javascript_samples/easing.js (easings.net formulas).
check("outQuad", near(Ease.outQuad(0.3), 0.51));
check("inOutCubic", near(Ease.inOutCubic(0.3), 0.108));
check("outBounce", near(Ease.outBounce(0.3), 0.680625));
check("outBack", near(Ease.outBack(0.3), 0.90713226));
check("outElastic", near(Ease.outElastic(0.3), 0.875));
check("inExpo", near(Ease.inExpo(0.3), 0.0078125));
check("inOutBack overshoots below 0", Ease.inOutBack(0.2) < 0);
check("long names are the same functions", Ease.easeOutBack === Ease.outBack);
check("get by long name", Ease.get("easeInOutSine") === Ease.inOutSine);
check("get a function", Ease.get(Math.sqrt) === Math.sqrt);
throws("get an unknown curve", () => Ease.get("sideways"));
check("back(0) is cubic", near(Ease.back(0).in(0.5), 0.125));
throws("negative overshoot", () => Ease.back(-1));
throws("elastic period 0", () => Ease.elastic({ period: 0 }));
check("steps", Ease.steps(4)(0.3) === 0.25 && Ease.steps(4)(0.99) === 0.75 && Ease.steps(4)(1) === 1);
throws("steps(0)", () => Ease.steps(0));
check("CSS ease at 0.5", near(Ease.cubicBezier(0.25, 0.1, 0.25, 1)(0.5), 0.8024034, 1e-4));
check("linear bezier", near(Ease.cubicBezier(0, 0, 1, 1)(0.3), 0.3, 1e-4));
throws("bezier x out of range", () => Ease.cubicBezier(1.5, 0, 0, 1));
check("reverse", near(Ease.reverse(Ease.inQuad)(0.5), 0.75));
check("mirror", near(Ease.mirror("linear")(0.25), 0.5) && near(Ease.mirror("linear")(0.75), 0.5));
check("lerp", Ease.lerp(10, 20, 0.25) === 12.5);
check("inverseLerp", Ease.inverseLerp(10, 20, 15) === 0.5 && Ease.inverseLerp(3, 3, 7) === 0);
check("remap", Ease.remap(5, 0, 10, 100, 200) === 150);
check("clamp", Ease.clamp(5, 0, 3) === 3 && Ease.clamp(-1, 0, 3) === 0);
check("smoothstep", Ease.smoothstep(0, 1, 0.5) === 0.5 && Ease.smoothstep(0, 1, 2) === 1);
check("damp halves with lambda ln 2 in one second", near(Ease.damp(0, 10, Math.LN2, 1), 5));
{
    let at60 = 0, at30 = 0;
    for (let i = 0; i < 60; i++) at60 = Ease.damp(at60, 100, 8, 1 / 60);
    for (let i = 0; i < 30; i++) at30 = Ease.damp(at30, 100, 8, 1 / 30);
    check("damp is frame-rate independent", near(at60, at30, 1e-3));
}

// --- Tween, advanced by hand ------------------------------------------------

throws("tween a number", () => Tween.to(1, { x: 1 }, 1));
throws("tween a non-number property", () => Tween.to({ x: "a" }, { x: 1 }, 1));
throws("tween to a non-number", () => Tween.to({ x: 0 }, { x: "1" }, 1));
throws("negative duration", () => Tween.to({ x: 0 }, { x: 1 }, -1));
throws("unknown ease", () => Tween.to({ x: 0 }, { x: 1 }, 1, { ease: "nope" }));
throws("fractional repeat", () => Tween.to({ x: 0 }, { x: 1 }, 1, { repeat: 1.5 }));
throws("endless zero-length tween", () => Tween.to({ x: 0 }, { x: 1 }, 0, { repeat: Infinity }));
throws("colors outside the properties", () => Tween.to({ x: 0 }, { x: 1 }, 1, { colors: ["y"] }));
throws("non-function callback", () => Tween.to({ x: 0 }, { x: 1 }, 1, { onComplete: 1 }));
check("nothing active after rejected tweens", Tween.count() === 0);

const tweenSystem = () => Loop.getSystems().some(system => system.name === "tween");

async function manualTests() {
    const box = { x: 0, y: 100 };
    const events = [];
    const move = Tween.to(box, { x: 10 }, 1, {
        ease: "linear",
        onStart: () => events.push("start"),
        onUpdate: (target, progress) => events.push(progress),
        onComplete: () => events.push("complete"),
    });
    check("one active tween", Tween.count() === 1 && move.active);
    check("Loop system while tweens are active", tweenSystem());
    Tween.update(0.5);
    check("halfway", box.x === 5 && box.y === 100 && move.progress === 0.5);
    Tween.update(0.75);
    check("exact end value", box.x === 10 && !move.active);
    check("callbacks in order", events.join() === "start,0.5,1,complete");
    check("await gives true", (await move) === true);
    check("Loop system released when idle", !tweenSystem());

    // Start values are read when the delay ends; the delay's excess time counts.
    box.x = 0;
    const late = Tween.to(box, { x: 10 }, 1, { ease: "linear", delay: 0.5 });
    Tween.update(0.25);
    check("waits for its delay", box.x === 0 && late.progress === 0);
    box.x = 6;
    Tween.update(0.75);
    check("starts from the value at its start", near(box.x, 8));   // 6 -> 10, halfway
    Tween.update(1);

    // from(): applied at once, animates back to the current value.
    box.x = 50;
    const drop = Tween.from(box, { x: 0 }, 1, { ease: "linear" });
    check("from applies the start value at once", box.x === 0);
    Tween.update(1);
    check("from ends at the original value", box.x === 50 && (await drop));

    // Repeat and yoyo.
    box.x = 0;
    let repeats = 0;
    const pulse = Tween.to(box, { x: 10 }, 1, { ease: "linear", yoyo: true, repeat: 2, onRepeat: () => repeats++ });
    Tween.update(1.5);
    check("yoyo runs backwards", near(box.x, 5) && repeats === 1);
    Tween.update(2);
    check("odd repeats end at the end", box.x === 10 && repeats === 2 && !pulse.active);
    box.x = 0;
    Tween.to(box, { x: 10 }, 1, { yoyo: true, repeat: 1 });
    Tween.update(5);
    check("even cycles end at the start", box.x === 0);

    // Colors: each channel interpolated.
    const tint = { color: 0x80402010 };
    Tween.to(tint, { color: 0 }, 1, { ease: "linear", colors: ["color"] });
    Tween.update(0.5);
    check("color channels", tint.color === 0x40201008);
    Tween.update(0.5);
    check("color end", tint.color === 0);

    // Kill, pause, realTime, overwrite.
    box.x = 0;
    const killed = Tween.to(box, { x: 10 }, 1, { ease: "linear" });
    Tween.update(0.3);
    killed.kill();
    check("kill keeps the current value", near(box.x, 3) && (await killed) === false);
    let completed = false;
    const finished = Tween.to(box, { x: 20 }, 1, { onComplete: () => { completed = true; } });
    finished.kill(true);
    check("kill(true) jumps to the end", box.x === 20 && completed && (await finished) === true);

    const paused = Tween.to(box, { x: 0 }, 1, { ease: "linear" }).pause();
    Tween.update(0.5);
    check("paused tween waits", box.x === 20 && paused.paused);
    paused.resume();
    Tween.update(0.5);
    check("resumed", near(box.x, 10));
    check("killTweensOf", Tween.killTweensOf(box) === 1 && Tween.count() === 0);

    const menu = { x: 0 }, world = { x: 0 };
    Tween.to(menu, { x: 10 }, 1, { ease: "linear", realTime: true });
    Tween.to(world, { x: 10 }, 1, { ease: "linear" });
    Tween.update(0, 0.5);   // paused game: no game time, real time goes on
    check("realTime tweens use the real delta", near(menu.x, 5) && world.x === 0);
    check("killAll", Tween.killAll() === 2);

    box.x = 0;
    Tween.to(box, { x: 10 }, 1, { ease: "linear" });
    const winner = Tween.to(box, { x: -10 }, 1, { ease: "linear", overwrite: true });
    Tween.update(0);        // both start; the second kills the first
    Tween.update(0.5);
    check("overwrite kills the other tweens", near(box.x, -5) && Tween.getTweensOf(box)[0] === winner);
    Tween.killAll();

    const instant = Tween.to(box, { x: 3 }, 0);
    Tween.update(0);
    check("zero duration completes on the next update", box.x === 3 && !instant.active);

    // Delays and sequences, awaited.
    const order = [];
    const done = Tween.sequence([
        () => Tween.to(box, { x: 4 }, 0.2).then(() => order.push("a")),
        () => Tween.delay(0.2).then(() => order.push("b")),
        () => order.push("c"),
    ]);
    for (let i = 0; i < 20; i++) {
        Tween.update(0.05);
        for (let j = 0; j < 5; j++) await null;   // let the sequence start its next step
    }
    await done;
    check("sequence order", order.join() === "a,b,c");
    check("sequence rejects non-functions", await Tween.sequence([1]).then(() => false, () => true));
    check("idle after manual tests", Tween.count() === 0 && !tweenSystem());
}

// --- Tween under Loop.run() ------------------------------------------------

function loopTests() {
    return new Promise(resolve => {
        const logo = { y: 0 };
        let frames = 0;
        const drop = Tween.to(logo, { y: 100 }, 0.25, { ease: "outBack" });
        Loop.run({
            update() {
                frames++;
                if (!drop.active || frames > 600) {
                    Loop.stop();
                    check("the Loop advances tweens", !drop.active && logo.y === 100);
                    check("in about 15 frames at 60 Hz", frames >= 10 && frames <= 25);
                    check("system released", !tweenSystem());
                    resolve();
                }
            },
        });
    });
}

manualTests()
    .then(loopTests)
    .catch(error => { failed++; console.log("[FAIL] " + error + "\n" + (error.stack || "")); })
    .then(() => {
        console.log(`Result: ${passed} passed, ${failed} failed`);
        if (!failed) console.log("Tween module test passed");
    });
