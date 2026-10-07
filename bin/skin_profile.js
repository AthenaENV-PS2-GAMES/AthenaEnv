// Skinning cost: 1 and then 4 skinned columns of bend_hi.glb (2334 vertices
// each), bending continuously. Two 180-frame stages (60 warm-up) print
// SKIN_PROFILE lines with update and draw ticks (1 tick = 1 us).
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect: mode.width / height, near: .5, far: 50});
camera.setPosition(0, 1.5, 7).lookAt(0, 1, 0);
const lights = new Lights.Set().setAmbient(.3, .3, .3).setDirectional(0, .4, .6, 1, .8, .8, .8);
const names = ["skinned-1x2334", "skinned-4x2334"];
const FRAMES = 180, WARMUP = 60, timer = Timer.new(), stats = {};
let stage = -1, frame = 0, scene = null, players = [], handles = [], updates = [], draws = [];
function release() {
    for (const p of players) p.dispose();
    for (const h of handles) h.dispose();
    if (scene) scene.dispose();
    players = []; handles = []; scene = null;
}
function create(count) {
    scene = new Scene3D.Scene();
    const root = scene.root; handles.push(root);
    for (let i = 0; i < count; i++) {
        const column = GLTF3D.load("models/bend_hi.glb", {shading: Model3D.DIFFUSE, baseColor: new Float32Array([.4, .7, 1, 1])});
        const holder = new Scene3D.Node().setPosition((i - (count - 1) / 2) * 1.5, 0, 0);
        holder.add(column.root); root.add(holder);
        handles.push(holder, column.root, ...column.nodes);
        const p = new Animation3D.Player(column.clips.bend, column.nodes);
        p.loop = true; p.speed = .5; p.time = i * .2; p.play(); players.push(p);
        column.clips.bend.dispose();
    }
}
const mean = v => v.reduce((a, b) => a + b, 0) / v.length;
function report() {
    console.log("SKIN_PROFILE " + JSON.stringify({stage: names[stage], samples: draws.length,
        updateTicksMean: +mean(updates).toFixed(1), drawTicksMean: +mean(draws).toFixed(1), triangles: stats.triangles}));
}
Loop.run({
    update() {
        const next = Math.floor(frame / FRAMES);
        if (stage !== next) {
            if (stage >= 0) report();
            release(); stage = next; updates = []; draws = [];
            create(stage ? 4 : 1); console.log("Skin profile stage " + stage + ": " + names[stage]);
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
            report(); console.log("Skin profile complete;"); release(); Timer.destroy(timer); Loop.stop();
        }
    }
});
