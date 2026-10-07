// Particles2D cost profile. Four 180-frame stages (60 warm-up): native
// emitters with 1000 sprite particles, 1000 rotated particles, 2000 sprite
// particles, and a JavaScript reference that simulates 300 particles in
// script and draws each with image.draw(). Prints PARTICLES_PROFILE lines with
// update and draw ticks (Timer units, 1 tick = 1 us at CLOCKS_PER_SEC 1e6).
const mode = Screen.getMode();
const texture = new Image("models/checker.png");
const names = ["native-1000-sprites", "native-1000-rotated", "native-2000-sprites", "js-300-draw"];
const FRAMES = 180, WARMUP = 60;
const timer = Timer.new();
const white = Color.new(255, 255, 255), orange = Color.new(255, 128, 0), gray = Color.new(64, 64, 64);
let stage = -1, frame = 0, emitter = null, js = null, updates = [], draws = [], counts = 0;

function create(index) {
    const rotated = index === 1, count = index === 2 ? 2000 : 1000;
    if (index === 3) {
        // JS reference: typed arrays, gravity and fade in script.
        const n = 300;
        js = {n, x: new Float32Array(n), y: new Float32Array(n), vx: new Float32Array(n), vy: new Float32Array(n),
            age: new Float32Array(n)};
        for (let i = 0; i < n; i++) spawn(i, Math.random());
        return;
    }
    // Rate keeps the pool full: count particles living about 1.5 s on average.
    emitter = new Particles2D.Emitter(texture, {capacity: count, rate: count / 1.5, life: [1, 2],
        speed: [60, 160], angle: -Math.PI / 2, spread: 1.2, gravity: [0, 120], drag: .3, size: [12, 2],
        color: [Color.new(255, 220, 120, 128), Color.new(255, 40, 0, 0)], area: [40, 8],
        spin: rotated ? [-4, 4] : 0, rotation: rotated ? [0, 6.28] : 0, seed: 7})
        .setPosition(mode.width / 2, mode.height * .75);
    emitter.emit(count);
}
function spawn(i, age) {
    const a = -Math.PI / 2 + (Math.random() - .5) * 1.2, s = 60 + Math.random() * 100;
    js.x[i] = mode.width / 2 + (Math.random() - .5) * 40; js.y[i] = mode.height * .75;
    js.vx[i] = Math.cos(a) * s; js.vy[i] = Math.sin(a) * s; js.age[i] = age;
}
function release() {
    if (emitter) emitter.dispose();
    emitter = null; js = null;
}
function updateJs(dt) {
    for (let i = 0; i < js.n; i++) {
        js.age[i] += dt / 1.5;
        if (js.age[i] >= 1) { spawn(i, 0); continue; }
        js.vy[i] += 120 * dt; js.x[i] += js.vx[i] * dt; js.y[i] += js.vy[i] * dt;
    }
}
function drawJs() {
    for (let i = 0; i < js.n; i++) {
        const size = 12 - 10 * js.age[i];
        texture.draw(js.x[i] - size / 2, js.y[i] - size / 2, {width: size, height: size});
    }
}
function mean(values) { return values.reduce((a, b) => a + b, 0) / values.length; }
function report() {
    console.log("PARTICLES_PROFILE " + JSON.stringify({stage: names[stage], samples: updates.length,
        updateTicksMean: +mean(updates).toFixed(1), drawTicksMean: +mean(draws).toFixed(1),
        particlesMean: +(counts / updates.length).toFixed(1)}));
}
console.log("Particles profile: Timer ticks; draw excludes HUD and flip.");
Loop.run({
    update(dt) {
        const next = Math.floor(frame / FRAMES), local = frame % FRAMES;
        if (stage !== next) {
            if (stage >= 0) report();
            release(); stage = next; updates = []; draws = []; counts = 0;
            if (stage >= names.length) return;
            create(stage); console.log("Particles profile stage " + stage + ": " + names[stage]);
        }
        const start = Timer.getTime(timer);
        if (emitter) emitter.update(1 / 60); else updateJs(1 / 60);
        const ticks = Timer.getTime(timer) - start;
        if (local >= WARMUP) { updates.push(ticks); counts += emitter ? emitter.count : js.n; }
    },
    draw() {
        if (stage >= names.length) return;
        const start = Timer.getTime(timer);
        if (emitter) emitter.draw(); else drawJs();
        const ticks = Timer.getTime(timer) - start;
        if (frame % FRAMES >= WARMUP) draws.push(ticks);
        for (let i = 0; i < names.length; i++) Draw.rect(12 + i * 20, 12, 14, 14, i === stage ? orange : gray);
        Draw.rect(12, mode.height - 20, Math.max(1, Math.floor((mode.width - 24) * (frame % FRAMES + 1) / FRAMES)), 8, white);
        if (++frame === names.length * FRAMES) {
            report(); release();
            console.log("Particles profile complete; all stages show a fountain of sparks.");
            Timer.destroy(timer); Loop.stop();
        }
    }
});
