// Morph target cost: morph_hi.glb (2304 vertices, 2 targets, flat normals
// regenerated after blending). Stage 0 keeps the weights at 0 (plain mesh
// path), stage 1 animates one box and stage 2 four boxes. 180-frame stages
// (60 warm-up) print MORPH_PROFILE lines with update and draw ticks (1 us).
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect: mode.width / height, near: .5, far: 50});
camera.setPosition(0, 2, 9).lookAt(0, .5, 0);
const lights = new Lights.Set().setAmbient(.3, .3, .3).setDirectional(0, .4, .6, 1, .8, .8, .8);
const names = ["static-1x2304", "morph-1x2304", "morph-4x2304"];
const FRAMES = 180, WARMUP = 60, timer = Timer.new(), stats = {};
let stage = -1, frame = 0, scene = null, players = [], handles = [], updates = [], draws = [];
function release() {
    for (const p of players) p.dispose();
    for (const h of handles) h.dispose();
    if (scene) scene.dispose();
    players = []; handles = []; scene = null;
}
function create(count, animate) {
    scene = new Scene3D.Scene();
    const root = scene.root; handles.push(root);
    for (let i = 0; i < count; i++) {
        const box = GLTF3D.load("models/morph_hi.glb", {shading: Model3D.DIFFUSE, baseColor: new Float32Array([1, .6, .3, 1])});
        const holder = new Scene3D.Node().setPosition((i - (count - 1) / 2) * 2.6, 0, 0);
        holder.add(box.root); root.add(holder);
        handles.push(holder, box.root, ...box.nodes);
        if (animate) {
            const p = new Animation3D.Player(box.clips.morph, box.nodes);
            p.loop = true; p.time = i * .7; p.play(); players.push(p);
        }
        box.clips.morph.dispose();
    }
}
const mean = v => v.reduce((a, b) => a + b, 0) / v.length;
function report() {
    console.log("MORPH_PROFILE " + JSON.stringify({stage: names[stage], samples: draws.length,
        updateTicksMean: +mean(updates).toFixed(1), drawTicksMean: +mean(draws).toFixed(1), triangles: stats.triangles}));
}
Loop.run({
    update() {
        const next = Math.floor(frame / FRAMES);
        if (stage !== next) {
            if (stage >= 0) report();
            release(); stage = next; updates = []; draws = [];
            create(stage === 2 ? 4 : 1, stage > 0); console.log("Morph profile stage " + stage + ": " + names[stage]);
        }
        const start = Timer.getTime(timer);
        Animation3D.advance(1 / 60); scene.update();
        if (frame % FRAMES >= WARMUP) updates.push(Timer.getTime(timer) - start);
    },
    draw() {
        const start = Timer.getTime(timer);
        scene.draw(camera, Render3D.CULL_BACK, lights, stats);
        if (frame % FRAMES >= WARMUP) draws.push(Timer.getTime(timer) - start);
        if (++frame === names.length * FRAMES) {
            report(); console.log("Morph profile complete;"); release(); Timer.destroy(timer); Loop.stop();
        }
    }
});
