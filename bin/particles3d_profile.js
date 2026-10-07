// Particles3D: a fountain of billboards behind and through a row of lit
// cubes, so the depth test against the scene is visible. Two 180-frame
// stages (60 warm-up) with 1000 and 2000 particles print PARTICLES3D_PROFILE
// lines (Timer ticks, 1 tick = 1 us).
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect: mode.width / height, near: .5, far: 60});
camera.setPosition(0, 2, 9).lookAt(0, 1.5, 0);
const lights = new Lights.Set().setAmbient(.3, .3, .3).setDirectional(0, .4, .6, 1, .8, .8, .8);
const lit = Model3D.load("models/lit_cube.glb", {shading: Model3D.DIFFUSE, baseColor: new Float32Array([.4, .6, 1, 1])});
const scene = new Scene3D.Scene(), root = scene.root;
for (let i = 0; i < 5; i++) root.add(new Scene3D.Node(lit).setPosition((i - 2) * 1.6, .5, 1).setScale(.6, .6, .6));
root.dispose(); scene.update();
const texture = new Image("models/checker.png");
const names = ["billboards-1000", "billboards-2000"];
const FRAMES = 180, WARMUP = 60;
const timer = Timer.new();
const white = Color.new(255, 255, 255), orange = Color.new(255, 128, 0), gray = Color.new(64, 64, 64);
let stage = -1, frame = 0, emitter = null, updates = [], draws = [], sent = 0;
function mean(values) { return values.reduce((a, b) => a + b, 0) / values.length; }
function report() {
    console.log("PARTICLES3D_PROFILE " + JSON.stringify({stage: names[stage], samples: updates.length,
        updateTicksMean: +mean(updates).toFixed(1), drawTicksMean: +mean(draws).toFixed(1),
        particlesMean: +(sent / draws.length).toFixed(1), particlesAlive: emitter.count}));
}
function create(index) {
    const count = index ? 2000 : 1000;
    if (emitter) emitter.dispose();
    emitter = new Particles3D.Emitter(texture, {capacity: count, rate: count / 2, life: [1.5, 2.5],
        speed: [3, 5], direction: [0, 1, 0], spread: .45, gravity: [0, -3, 0], drag: .1, size: [.35, .05],
        color: [Color.new(255, 220, 160, 128), Color.new(255, 60, 20, 0)], area: [.6, 0, .6], seed: 5})
        .setPosition(0, 0, -1.5);
    emitter.emit(count);
}
console.log("Particles3D profile: Timer ticks; draw excludes the cubes, HUD and flip.");
Loop.run({
    update() {
        const next = Math.floor(frame / FRAMES), local = frame % FRAMES;
        if (stage !== next) {
            if (stage >= 0) report();
            stage = next; updates = []; draws = []; sent = 0;
            create(stage); console.log("Particles3D profile stage " + stage + ": " + names[stage]);
        }
        const start = Timer.getTime(timer);
        emitter.update(1 / 60);
        if (local >= WARMUP) updates.push(Timer.getTime(timer) - start);
    },
    draw() {
        scene.draw(camera, Render3D.CULL_BACK, lights);
        const start = Timer.getTime(timer);
        const n = emitter.draw(camera);
        if (frame % FRAMES >= WARMUP) { draws.push(Timer.getTime(timer) - start); sent += n; }
        for (let i = 0; i < names.length; i++) Draw.rect(12 + i * 20, 12, 14, 14, i === stage ? orange : gray);
        Draw.rect(12, height - 20, Math.max(1, Math.floor((mode.width - 24) * (frame % FRAMES + 1) / FRAMES)), 8, white);
        if (++frame === names.length * FRAMES) {
            report(); console.log("Particles3D profile complete; a fountain rises behind and through the cubes.");
            emitter.dispose(); Timer.destroy(timer); Loop.stop();
        }
    }
});
