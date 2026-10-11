// Deterministic counterpart of samples/native/3d_regression/main.c.
// 11 stages, 180 frames each: 60 warm-up + 120 measured frames. Stops by itself.
// Stage 9 spins a closed cube with one colour per face under CULL_BACK: every
// face must show whole, in one colour (a broken cull shows inner triangles).
// Stage 10 puts 64 triangles across the left and right screen edges: drawn
// by VU1 inside the GS guard band and cut straight by the scissor.
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S;
Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect: mode.width / height, near: 1, far: 20});
camera.lookAt(0, 0, -1).setPosition(0, 0, 0);
const cameraB = new Camera3D.Camera({aspect: mode.width / height, near: 1, far: 20});
cameraB.lookAt(.5, 0, -1).setPosition(.5, 0, 0);
const names = ["depth", "cull-back", "cull-front", "camera-tile-camera", "recreate",
    "inside-batch", "clip-batch", "inside-individual", "clip-individual", "closed-cull", "edge-batch"];
const keys = ["submittedObjects", "culledObjects", "drawPasses", "pipelinePasses", "sourceTriangles",
    "triangles", "clippedTriangles", "rejectedTriangles", "vuBatches", "geometryBytes", "guardBandObjects"];
// The screen edge at z = -2.5: tan(30 deg) * aspect * 2.5.
const EDGE = 2.5 * Math.tan(Math.PI / 6) * mode.width / height;
const white = Color.new(255,255,255), orange = Color.new(255,128,0), gray = Color.new(64,64,64);
const tile = new TileMap.Instance({
    descriptor: new TileMap.Descriptor({materials: [{textureIndex: -1, endOffset: 0}]}),
    spriteBuffer: TileMap.SpriteBuffer.fromObjects([{x: 0, y: 0, w: 32, h: 16, r: 255, g: 128, b: 0, a: 128}])
});
const timer = Timer.new();
let objects = [], batch = null, stage = -1, frame = 0, samples = [], totals, last = null;
function release() {
    if (batch) batch.dispose();
    batch = null;
    for (const object of objects) object.dispose();
    objects = [];
}
function mesh(positions, color) {
    const colors = new Float32Array(positions.length / 3 * 4);
    for (let i = 0; i < colors.length; i += 4) colors.set(color, i);
    return Model3D.Mesh.fromGeometry({positions: new Float32Array(positions), colors});
}
function pair(depth) {
    const a = mesh([-.5,-.5,-2, .5,-.5,-2, 0,.5,-2], depth ? [0,1,0,1] : [1,1,0,1]);
    const b = mesh(depth ? [-1,-1,-4, 1,-1,-4, 0,1,-4] :
        [-.5,-.5,-2, 0,.5,-2, .5,-.5,-2], depth ? [1,0,0,1] : [0,1,1,1]);
    objects = [a.createInstance(), b.createInstance()]; a.dispose(); b.dispose();
    if (!depth) { objects[0].setPosition(-.8,0,-1); objects[1].setPosition(.8,0,-1); }
}
// Counter-clockwise from outside, as tools/make_3d_lighting_asset.js.
function cube() {
    const p = [[-1,-1,-1],[1,-1,-1],[1,1,-1],[-1,1,-1],[-1,-1,1],[1,-1,1],[1,1,1],[-1,1,1]];
    const faces = [[0,2,1,0,3,2],[4,5,6,4,6,7],[0,1,5,0,5,4],[3,7,6,3,6,2],[0,4,7,0,7,3],[1,2,6,1,6,5]];
    const tint = [[1,0,0,1],[0,1,0,1],[0,0,1,1],[1,1,0,1],[0,1,1,1],[1,0,1,1]];
    const positions = new Float32Array(36 * 3), colors = new Float32Array(36 * 4);
    faces.forEach((face, f) => face.forEach((index, k) => {
        positions.set(p[index], (f * 6 + k) * 3); colors.set(tint[f], (f * 6 + k) * 4);
    }));
    const resource = Model3D.Mesh.fromGeometry({positions, colors});
    objects = [resource.createInstance().setPosition(0, 0, -3).setScale(.6, .6, .6)];
    resource.dispose();
}
// crossing: 0 inside, 1 through the near plane, 2 across the screen edges.
function collection(crossing) {
    const resource = crossing === 2 ? mesh([-.3,-.06,-2.5, .3,-.06,-2.5, 0,.06,-2.5], [1,.5,0,1]) :
        mesh(crossing ? [-.06,-.06,-.4, .06,-.06,-2.5, 0,.06,-2.5] :
        [-.06,-.06,-2.5, .06,-.06,-2.5, 0,.06,-2.5], [0,.8,1,1]);
    batch = new Render3D.Batch();
    for (let i = 0; i < 64; i++) {
        const object = crossing === 2 ? resource.createInstance().setPosition(i & 1 ? EDGE : -EDGE, (Math.floor(i / 2) - 15.5) * .04, 0) :
            resource.createInstance().setPosition((i % 8 - 3.5) * .12, (Math.floor(i / 8) - 3.5) * .12, 0);
        objects.push(object); batch.add(object);
    }
    resource.dispose();
}
function begin(next) {
    release(); stage = next; samples = []; totals = {};
    for (const key of keys) totals[key] = 0;
    if (stage === 9) cube();
    else if (stage === 10) collection(2);
    else if (stage >= 5) collection(stage === 6 || stage === 8 ? 1 : 0);
    else pair(stage === 0 || stage === 4);
    console.log("3D regression stage " + stage + ": " + names[stage]);
}
function render(local) {
    const sum = {};
    for (const key of keys) sum[key] = 0;
    function add(stats) { for (const key of keys) sum[key] += stats[key]; }
    const cull = stage === 1 || stage === 9 ? Render3D.CULL_BACK : stage === 2 ? Render3D.CULL_FRONT : Render3D.CULL_NONE;
    if (stage === 9) objects[0].setRotationEuler(local * .03, local * .02, 0);
    if (stage === 3) {
        add(Render3D.draw(objects[0], camera, cull));
        tile.render(mode.width / 2 - 16, 36); // Different VU1 program and BASE/OFFSET.
        add(Render3D.draw(objects[1], cameraB, cull));
    } else if (stage === 5 || stage === 6 || stage === 10) add(batch.draw(camera, cull));
    else for (let n = 0; n < objects.length; n++) {
        const index = (stage === 0 || stage === 4) && Math.floor(local / 30) % 2 ? objects.length - 1 - n : n;
        add(Render3D.draw(objects[index], camera, cull));
    }
    return sum;
}
function report() {
    samples.sort((a,b) => a-b);
    const mean = samples.reduce((a,b) => a+b, 0) / samples.length;
    const result = {stage: names[stage], samples: samples.length,
        drawTicksMean: mean, drawTicksP95: samples[Math.ceil(samples.length * .95) - 1],
        drawTicksP99: samples[Math.ceil(samples.length * .99) - 1], memory: System.getMemoryStats()};
    for (const key of keys) result[key + "PerFrame"] = totals[key] / samples.length;
    console.log("3D_BASELINE " + JSON.stringify(result));
}
console.log("3D regression: drawTicks use Timer/clock native units; excludes HUD, flip and reporting. " +
    "First 60 frames per stage are warm-up; geometryBytes excludes tags/state/programs/2D.");
Loop.run({draw() {
    const next = Math.floor(frame / 180), local = frame % 180;
    if (stage !== next) begin(next);
    // Recreate before measurement. Every 30th draw releases handles before flip.
    if (stage === 4 && objects.length === 0) pair(true);
    const start = Timer.getTime(timer);
    last = render(local);
    const ticks = Timer.getTime(timer) - start;
    if (local >= 60) {
        samples.push(ticks);
        for (const key of keys) totals[key] += last[key];
    }
    if (stage === 4 && local % 30 === 29) release();
    for (let i = 0; i < names.length; i++) Draw.rect(12 + i * 20, 12, 14, 14, i === stage ? orange : gray);
    Draw.rect(12, height - 20, Math.max(1, Math.floor((mode.width - 24) * (local + 1) / 180)), 8, white);
    if (local === 179) report();
    if (++frame === names.length * 180) {
        release(); camera.dispose(); cameraB.dispose(); Timer.destroy(timer);
        console.log("3D regression complete; inspect images before recording visual PASS.");
        Loop.stop();
    }
}});
