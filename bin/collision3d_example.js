// Collision3D on PS2: a level built from triangles (floor, border walls, a
// pillar, three 0.2 steps and a 20-degree ramp up to a platform), drawn with
// the same triangles it collides with, and characters walking in circles,
// climbing the steps and the ramp, and jumping. Stages of 1, 16 and 64
// characters (180 frames, 60 warm-up) print COLLISION3D_PROFILE lines: the
// character step, 64 raycasts (one call each, then one raycastMany) and the draw, in ticks (1 tick = 1 us).
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect: mode.width / height, near: .5, far: 80});
camera.setPosition(0, 15, 17).lookAt(0, 0, 1);
const lights = new Lights.Set().setAmbient(.35, .35, .35).setDirectional(0, .3, .8, .5, .9, .9, .85);
// Box corners by bits (x, y, z) and two counter-clockwise triangles per face.
const FACES = [0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,1,5, 0,5,4, 2,6,7, 2,7,3, 0,4,6, 0,6,2, 1,3,7, 1,7,5];
function box(out, min, max) {
    for (const i of FACES) out.push(i & 1 ? max[0] : min[0], i & 2 ? max[1] : min[1], i & 4 ? max[2] : min[2]);
}
const level = [];
box(level, [-12, -1, -12], [12, 0, 12]);
box(level, [-12, 0, -12], [12, 1.2, -11.5]); box(level, [-12, 0, 11.5], [12, 1.2, 12]);
box(level, [-12, 0, -11.5], [-11.5, 1.2, 11.5]); box(level, [11.5, 0, -11.5], [12, 1.2, 11.5]);
box(level, [-1, 0, -1], [1, 2.5, 1]);
for (let s = 0; s < 3; s++) box(level, [3 + s * .8, 0, -8], [8, .2 * (s + 1), -4]);
const rampHeight = 5 * Math.tan(20 * Math.PI / 180);
level.push(-8,0,2, -8,rampHeight,7, -4,rampHeight,7,  -8,0,2, -4,rampHeight,7, -4,0,2);
box(level, [-8, 0, 7], [-4, rampHeight, 10]);
const levelMesh = Model3D.Mesh.fromGeometry({positions: new Float32Array(level),
    material: {shading: Model3D.DIFFUSE, baseColor: new Float32Array([.55, .6, .65, 1])}});
const heroMesh = (() => { const p = []; box(p, [-.4, 0, -.4], [.4, 1.8, .4]);
    return Model3D.Mesh.fromGeometry({positions: new Float32Array(p),
        material: {shading: Model3D.DIFFUSE, baseColor: new Float32Array([1, .55, .2, 1])}}); })();
const names = ["characters-1", "characters-16", "characters-64"], counts = [1, 16, 64];
// Ray arguments built once: arrays made per call (with double math, emulated
// on the EE) would cost more than the query.
const DOWN = new Float32Array([0, -1, 0]);
const ORIGINS = Array.from({length: 64}, (_, i) => new Float32Array([(i & 7) * 3 - 10.5, 10, (i >> 3) * 3 - 10.5]));
const ORIGINS_FLAT = new Float32Array(192), RAY_OUT = new Float32Array(256);
ORIGINS.forEach((o, i) => ORIGINS_FLAT.set(o, i * 3));
const FRAMES = 180, WARMUP = 60, timer = Timer.new(), stats = {}, hit = {};
let stage = -1, frame = 0, scene = null, world = null, heroes = [], handles = [], samples = null;
function release() {
    for (const h of heroes) h.character.dispose();
    for (const h of handles) h.dispose();
    if (world) world.dispose();
    if (scene) scene.dispose();
    heroes = []; handles = []; scene = null; world = null;
}
function create(count) {
    scene = new Scene3D.Scene(); world = new Collision3D.World();
    const root = scene.root, levelNode = new Scene3D.Node().setMesh(levelMesh);
    root.add(levelNode); handles.push(root, levelNode);
    world.addNode(levelNode);
    for (let i = 0; i < count; i++) {
        // Circles over the steps, the ramp and the open floor.
        const cx = [5, -6, 6, -6][i % 4], cz = [-6, 5, 6, -5][i % 4], radius = 2 + (i >> 2) % 4;
        const node = new Scene3D.Node();
        node.setMesh(heroMesh); root.add(node); handles.push(node);
        const character = new Collision3D.Character(world, {position: [cx + radius, 3 + (i % 3), cz]});
        character.bind(node);
        heroes.push({character, cx, cz, radius, phase: i * .7});
    }
}
const mean = v => v.reduce((a, b) => a + b, 0) / v.length;
function report() {
    console.log("COLLISION3D_PROFILE " + JSON.stringify({stage: names[stage], samples: samples.step.length,
        triangles: world.triangleCount, stepTicksMean: +mean(samples.step).toFixed(1),
        raycastsTicksMean: +mean(samples.rays).toFixed(1), raycastManyTicksMean: +mean(samples.batch).toFixed(1), drawTicksMean: +mean(samples.draw).toFixed(1),
        onGround: heroes.filter(h => h.character.onGround).length}));
}
Loop.run({
    update(dt) {
        const next = Math.floor(frame / FRAMES);
        if (stage !== next) {
            if (stage >= 0) report();
            release(); stage = next; samples = {step: [], rays: [], batch: [], draw: []};
            create(counts[stage]); console.log("Collision3D stage " + stage + ": " + names[stage]);
        }
        const t = frame / 60;
        for (const h of heroes) {
            const a = t * 2.4 / h.radius + h.phase, c = h.character;
            c.vx = -Math.sin(a) * 2.4; c.vz = Math.cos(a) * 2.4;
            if (c.onGround && (frame + h.phase * 60) % 150 < 1) c.vy = 4.5;
        }
        let start = Timer.getTime(timer);
        Collision3D.step(1 / 60);
        const step = Timer.getTime(timer) - start;
        start = Timer.getTime(timer);
        for (let i = 0; i < 64; i++) world.raycast(ORIGINS[i], DOWN, 20, undefined, hit);
        const rays = Timer.getTime(timer) - start;
        start = Timer.getTime(timer);
        world.raycastMany(ORIGINS_FLAT, DOWN, 20, RAY_OUT);
        const batch = Timer.getTime(timer) - start;
        scene.update();
        if (frame % FRAMES >= WARMUP) { samples.step.push(step); samples.rays.push(rays); samples.batch.push(batch); }
    },
    draw() {
        const start = Timer.getTime(timer);
        scene.draw(camera, Render3D.CULL_BACK, lights, stats);
        if (frame % FRAMES >= WARMUP) samples.draw.push(Timer.getTime(timer) - start);
        if (++frame === names.length * FRAMES) {
            report(); console.log("Collision3D profile complete;"); release(); Timer.destroy(timer); Loop.stop();
        }
    }
});
