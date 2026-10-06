// Deterministic counterpart of samples/native/3d_regression/main.c.
// 9 stages, 180 frames each: 60 warm-up + 120 measured frames. Stops by itself.
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S;
Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect: mode.width / height, near: 1, far: 20});
camera.lookAt(0, 0, -1).setPosition(0, 0, 0);
const cameraB = new Camera3D.Camera({aspect: mode.width / height, near: 1, far: 20});
cameraB.lookAt(.5, 0, -1).setPosition(.5, 0, 0);
const names = ["depth", "cull-back", "cull-front", "camera-tile-camera", "recreate",
    "inside-batch", "clip-batch", "inside-individual", "clip-individual"];
const keys = ["submittedObjects", "culledObjects", "drawPasses", "sourceTriangles",
    "triangles", "clippedTriangles", "rejectedTriangles", "vuBatches", "geometryBytes"];
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
function collection(crossing) {
    const resource = mesh(crossing ? [-.06,-.06,-.4, .06,-.06,-2.5, 0,.06,-2.5] :
        [-.06,-.06,-2.5, .06,-.06,-2.5, 0,.06,-2.5], [0,.8,1,1]);
    batch = new Render3D.Batch();
    for (let i = 0; i < 64; i++) {
        const object = resource.createInstance().setPosition((i % 8 - 3.5) * .12, (Math.floor(i / 8) - 3.5) * .12, 0);
        objects.push(object); batch.add(object);
    }
    resource.dispose();
}
function begin(next) {
    release(); stage = next; samples = []; totals = {};
    for (const key of keys) totals[key] = 0;
    if (stage >= 5) collection(stage === 6 || stage === 8);
    else pair(stage === 0 || stage === 4);
    console.log("3D regression stage " + stage + ": " + names[stage]);
}
function render(local) {
    const sum = {};
    for (const key of keys) sum[key] = 0;
    function add(stats) { for (const key of keys) sum[key] += stats[key]; }
    const cull = stage === 1 ? Render3D.CULL_BACK : stage === 2 ? Render3D.CULL_FRONT : Render3D.CULL_NONE;
    if (stage === 3) {
        add(Render3D.draw(objects[0], camera, cull));
        tile.render(mode.width / 2 - 16, 36); // Different VU1 program and BASE/OFFSET.
        add(Render3D.draw(objects[1], cameraB, cull));
    } else if (stage === 5 || stage === 6) add(batch.draw(camera, cull));
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
