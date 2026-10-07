// Physics3D on PS2: crates, balls and capsules dropped into a walled pit with a ramp,
// stepped natively, each drawn through a bound Scene3D node. Stages of 32,
// 128 and 256 bodies (240 frames, the last 120 measured) print
// PHYSICS3D_PROFILE lines: step and draw ticks (1 tick = 1 us), contacts
// and how many bodies are asleep at the end.
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect: mode.width / height, near: .5, far: 80});
camera.setPosition(0, 13, 15).lookAt(0, 1, 0);
const lights = new Lights.Set().setAmbient(.35, .35, .35).setDirectional(0, .3, .8, .5, .9, .9, .85);
const FACES = [0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,1,5, 0,5,4, 2,6,7, 2,7,3, 0,4,6, 0,6,2, 1,3,7, 1,7,5];
function box(out, min, max) {
    for (const i of FACES) out.push(i & 1 ? max[0] : min[0], i & 2 ? max[1] : min[1], i & 4 ? max[2] : min[2]);
}
const level = [];
box(level, [-8, -1, -8], [8, 0, 8]);
box(level, [-8, 0, -8], [8, 2, -7.5]); box(level, [-8, 0, 7.5], [8, 2, 8]);
box(level, [-8, 0, -7.5], [-7.5, 2, 7.5]); box(level, [7.5, 0, -7.5], [8, 2, 7.5]);
const rampTop = 4 * Math.tan(25 * Math.PI / 180);
level.push(3,0,-6, 3,0,6, 7.5,rampTop+.2,6,  3,0,-6, 7.5,rampTop+.2,6, 7.5,rampTop+.2,-6);
const material = color => ({shading: Model3D.DIFFUSE, baseColor: new Float32Array(color)});
const levelMesh = Model3D.Mesh.fromGeometry({positions: new Float32Array(level), material: material([.55, .6, .65, 1])});
const crateMesh = (() => { const p = []; box(p, [-.4, -.4, -.4], [.4, .4, .4]);
    return Model3D.Mesh.fromGeometry({positions: new Float32Array(p), material: material([.85, .6, .3, 1])}); })();
// A low-poly sphere of radius 0.35 (8 x 6), counter-clockwise from outside.
const ballMesh = (() => {
    const p = [], at = (i, j) => { const t = Math.PI * j / 6, f = 2 * Math.PI * i / 8;
        return [.35 * Math.sin(t) * Math.cos(f), .35 * Math.cos(t), -.35 * Math.sin(t) * Math.sin(f)]; };
    for (let j = 0; j < 6; j++) for (let i = 0; i < 8; i++) {
        const a = at(i, j), b = at(i, j + 1), c = at(i + 1, j + 1), d = at(i + 1, j);
        if (j > 0) p.push(...a, ...b, ...d);
        if (j < 5) p.push(...d, ...b, ...c);
    }
    return Model3D.Mesh.fromGeometry({positions: new Float32Array(p), material: material([.3, .6, 1, 1])});
})();
// A capsule of radius 0.25 and half height 0.35 along y (8 sides, 3 rings per cap).
const capsuleMesh = (() => {
    const r = .25, h = .35, sides = 8, rings = 3, p = [];
    const at = (i, j) => {   // j: 0 (top pole) .. 2 * rings + 1 (bottom pole)
        const top = j <= rings, k = top ? j : j - 1;
        const t = Math.PI / 2 * (top ? 1 - k / rings : (k - rings) / rings), f = 2 * Math.PI * i / sides;
        const c = Math.cos(t) * r, y = (top ? h : -h) + (top ? 1 : -1) * Math.sin(t) * r;
        return [c * Math.cos(f), y, -c * Math.sin(f)];
    };
    for (let j = 0; j <= 2 * rings; j++) for (let i = 0; i < sides; i++) {
        const a = at(i, j), b = at(i, j + 1), c = at(i + 1, j + 1), d = at(i + 1, j);
        if (j > 0) p.push(...a, ...b, ...d);
        if (j < 2 * rings) p.push(...d, ...b, ...c);
    }
    return Model3D.Mesh.fromGeometry({positions: new Float32Array(p), material: material([.4, .9, .4, 1])});
})();
const names = ["bodies-32", "bodies-128", "bodies-256"], counts = [32, 128, 256];
const FRAMES = 240, WARMUP = 120, timer = Timer.new(), stats = {};
let stage = -1, frame = 0, scene = null, statics = null, world = null, bodies = [], handles = [], samples = null;
function release() {
    for (const b of bodies) b.dispose();
    for (const h of handles) h.dispose();
    if (world) world.dispose();
    if (statics) statics.dispose();
    if (scene) scene.dispose();
    bodies = []; handles = []; world = null; statics = null; scene = null;
}
function create(count) {
    scene = new Scene3D.Scene(); statics = new Collision3D.World();
    const root = scene.root, levelNode = new Scene3D.Node().setMesh(levelMesh);
    root.add(levelNode); handles.push(root, levelNode);
    statics.addNode(levelNode);
    world = new Physics3D.World(statics);
    for (let i = 0; i < count; i++) {
        // Columns over the pit, alternating crates and balls, slightly offset.
        const x = (i % 8) * 1.6 - 5.6 + (i % 3) * .07, z = ((i >> 3) % 8) * 1.6 - 5.6, y = 2 + (i >> 6) * 1.1 + (i % 5) * .2;
        const node = new Scene3D.Node();
        root.add(node); handles.push(node);
        const kind = i % 3, spin = [Math.sin(i * .3), 0, 0, Math.cos(i * .3)];
        const body = kind === 1 ? world.addSphere({radius: .35, position: [x, y, z], restitution: .3}) :
            kind === 2 ? world.addCapsule({radius: .25, halfHeight: .35, position: [x, y, z], rotation: spin}) :
            world.addBox({halfExtents: [.4, .4, .4], position: [x, y, z], rotation: [0, Math.sin(i * .3), 0, Math.cos(i * .3)]});
        node.setMesh(kind === 1 ? ballMesh : kind === 2 ? capsuleMesh : crateMesh);
        body.bind(node); bodies.push(body);
    }
}
const mean = v => v.reduce((a, b) => a + b, 0) / v.length;
function report() {
    console.log("PHYSICS3D_PROFILE " + JSON.stringify({stage: names[stage], bodies: world.bodyCount,
        samples: samples.step.length, stepTicksMean: +mean(samples.step).toFixed(1),
        stepTicksFirst120: +mean(samples.early).toFixed(1), drawTicksMean: +mean(samples.draw).toFixed(1),
        contactsMean: +mean(samples.contacts).toFixed(1), sleeping: bodies.filter(b => b.sleeping).length,
        phases: ["collide", "prepare", "solve", "integrate"].map((name, i) => name + ":" + mean(samples.phases.map(p => p[i])).toFixed(1)).join(" ")}));
}
Loop.run({
    update() {
        const next = Math.floor(frame / FRAMES);
        if (stage !== next) {
            if (stage >= 0) report();
            release(); stage = next; samples = {step: [], early: [], draw: [], contacts: [], phases: []};
            create(counts[stage]); console.log("Physics3D stage " + stage + ": " + names[stage]);
        }
        const start = Timer.getTime(timer);
        world.step(1 / 60);
        const ticks = Timer.getTime(timer) - start;
        if (frame % FRAMES >= WARMUP) {
            samples.step.push(ticks); samples.contacts.push(world.contactCount);
            const p = world.profile; samples.phases.push([p.collide, p.prepare, p.solve, p.integrate]);
        }
        else samples.early.push(ticks);
        scene.update();
    },
    draw() {
        const start = Timer.getTime(timer);
        scene.draw(camera, Render3D.CULL_BACK, lights, stats);
        if (frame % FRAMES >= WARMUP) samples.draw.push(Timer.getTime(timer) - start);
        if (++frame === names.length * FRAMES) {
            report(); console.log("Physics3D profile complete;"); release(); Timer.destroy(timer); Loop.stop();
        }
    }
});
