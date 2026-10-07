// QuickJS counterpart of samples/native/3d_profile/main.c: same 8x8 grid of
// 64 cubes and the same seven 180-frame stages (60 warm-up frames), so the
// difference against the C log is the cost of the binding and of the script.
// First, per-call timings of the setters a game calls every frame. Update
// ticks include the JS setters (and Scene3D.update); draw ticks exclude HUD
// and flip. Times are Timer units: convert with CLOCKS_PER_SEC printed by the
// native profile (1 tick = 1 us when it prints 1000000).
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect: mode.width / height, near: 1, far: 30});
camera.lookAt(0, 0, -1).setPosition(0, 0, 0);
const lights = new Lights.Set().setAmbient(.25, .25, .25).setDirectional(0, .4, .6, 1, .8, .8, .8);
const names = ["batch-unlit-static", "batch-lit-static", "scene-lit-static",
    "batch-lit-animated", "scene-lit-animated", "scene-lit-rows", "scene-many-nodes",
    // QuickJS only: stages 3 and 4 through the bulk setters, one call per frame.
    "batch-lit-animated-bulk", "scene-lit-animated-bulk",
    // Native motion: each cube spins with setSpin() once; per frame only advance().
    "scene-lit-spin",
    // Animation3D: each cube plays a rotation clip; per frame Animation3D.advance().
    "scene-lit-clip",
    // CameraRig3D: an auto-rotating Orbit circles the static grid.
    "scene-lit-orbit",
    // Tween3D: every cube bobs with a native yoyo tween; per frame Tween3D.advance().
    "scene-lit-tween"];
const GRID = 8, COUNT = GRID * GRID, FRAMES = 180, WARMUP = 60, CALLS = 2000;
const color = new Float32Array([1, .85, .4, 1]);
const unlit = Model3D.load("models/lit_cube.glb", {shading: Model3D.UNLIT, baseColor: color});
const lit = Model3D.load("models/lit_cube.glb", {shading: Model3D.DIFFUSE, baseColor: color});
const timer = Timer.new();
const cubeX = i => ((i % GRID) - 3.5) * .9, rowY = r => (r - 3.5) * .7;

function micro() {
    const node = new Scene3D.Node(lit), item = lit.createInstance(), q = new Quaternion.Quaternion();
    const t0 = Timer.getTime(timer);
    for (let i = 0; i < CALLS; i++) node.setPosition(i * 1e-4, .5, -7);
    const t1 = Timer.getTime(timer);
    for (let i = 0; i < CALLS; i++) node.setRotationEuler(i * 1e-3, i * .5e-3, 0);
    const t2 = Timer.getTime(timer);
    for (let i = 0; i < CALLS; i++) item.setRotationEuler(i * 1e-3, i * .5e-3, 0);
    const t3 = Timer.getTime(timer);
    for (let i = 0; i < CALLS; i++) q.setEuler(i * 1e-3, i * .5e-3, 0);
    const t4 = Timer.getTime(timer);
    for (let i = 0; i < CALLS; i++) camera.setPosition(i * 1e-3, 1, 2).lookAt(0, 0, -7);
    const t5 = Timer.getTime(timer);
    for (let i = 0; i < CALLS; i++) Math.sin(i * 1e-3);
    const t6 = Timer.getTime(timer);
    // Behind the camera: the AABB test culls it, so the call is mostly the
    // binding and its stats object. The literal has the same ten keys.
    camera.lookAt(0, 0, -1).setPosition(0, 0, 0);
    item.setPosition(0, 0, 50);
    for (let i = 0; i < CALLS; i++) Render3D.draw(item, camera, Render3D.CULL_NONE);
    const t7 = Timer.getTime(timer);
    for (let i = 0; i < CALLS; i++) ({submittedObjects: i, culledObjects: i, drawPasses: i, pipelinePasses: i,
        triangles: i, vuBatches: i, sourceTriangles: i, clippedTriangles: i, rejectedTriangles: i, geometryBytes: i});
    const t8 = Timer.getTime(timer);
    const reused = {};
    for (let i = 0; i < CALLS; i++) Render3D.draw(item, camera, Render3D.CULL_NONE, undefined, reused);
    const t9 = Timer.getTime(timer);
    // Bulk setters per object: the call alone with values already in a
    // Float32Array, and the JS loop that fills one. 64 nodes, CALLS objects.
    const nodes = [], values = new Float32Array(COUNT * 3), rounds = CALLS / COUNT;
    for (let i = 0; i < COUNT; i++) nodes.push(new Scene3D.Node(lit));
    const t10 = Timer.getTime(timer);
    for (let r = 0; r < rounds; r++) Scene3D.setRotationsEuler(nodes, values);
    const t11 = Timer.getTime(timer);
    for (let r = 0; r < rounds; r++)
        for (let i = 0; i < COUNT; i++) { values[i * 3] = r; values[i * 3 + 1] = r * .5; values[i * 3 + 2] = 0; }
    const t12 = Timer.getTime(timer);
    // Follow camera: in JS (world matrix, offset, two setters) vs a native rig.
    const followScene = new Scene3D.Scene(), followRoot = followScene.root, target = new Scene3D.Node();
    followRoot.add(target); followRoot.dispose(); target.setPosition(1, 0, -7).setRotationEuler(0, .5, 0);
    followScene.update();
    const world = new Matrix4.Matrix4(), followCamera = new Camera3D.Camera();
    const t13 = Timer.getTime(timer);
    for (let i = 0; i < CALLS; i++) {
        target.getWorldTransform(world);
        const ex = world.get(4) * 2 + world.get(8) * 6 + world.get(12), ey = world.get(5) * 2 + world.get(9) * 6 + world.get(13),
            ez = world.get(6) * 2 + world.get(10) * 6 + world.get(14);
        followCamera.setPosition(ex, ey, ez).lookAt(world.get(12), world.get(13), world.get(14));
    }
    const t14 = Timer.getTime(timer);
    const rig = new CameraRig3D.Follow(followCamera, target).setOffset(0, 2, 6);
    for (let i = 0; i < CALLS; i++) rig.update(1 / 60);
    const t15 = Timer.getTime(timer);
    rig.dispose(); target.dispose(); followScene.dispose(); followCamera.dispose();
    // 64 bobbing cubes for 120 frames: JS Tween on plain objects plus one
    // setPosition per cube, versus Tween3D on the nodes.
    const bobs = [], boxes = [], FRAMES_T = 120;
    for (let i = 0; i < COUNT; i++) { bobs.push({y: 0}); boxes.push(new Scene3D.Node(lit)); }
    for (const b of bobs) Tween.to(b, {y: 1}, .5, {yoyo: true, repeat: Infinity, ease: "inOutSine"});
    const t16 = Timer.getTime(timer);
    for (let f = 0; f < FRAMES_T; f++) {
        Tween.update(1 / 60);
        for (let i = 0; i < COUNT; i++) boxes[i].setPosition(0, bobs[i].y, 0);
    }
    const t17 = Timer.getTime(timer);
    Tween.killAll();
    for (const b of boxes) Tween3D.to(b, {position: [0, 1, 0]}, .5, {yoyo: true, repeat: Infinity, ease: "inOutSine"});
    const t18 = Timer.getTime(timer);
    for (let f = 0; f < FRAMES_T; f++) Tween3D.advance(1 / 60);
    const t19 = Timer.getTime(timer);
    for (const b of boxes) { Tween3D.killTweensOf(b); b.dispose(); }
    for (const n of nodes) n.dispose();
    node.dispose(); item.dispose(); q.dispose();
    const per = ticks => +(ticks / CALLS).toFixed(3);
    console.log("3D_PROFILE_JS_MICRO " + JSON.stringify({calls: CALLS, nodeSetPositionTicks: per(t1 - t0),
        nodeSetEulerTicks: per(t2 - t1), instanceSetEulerTicks: per(t3 - t2), quaternionSetEulerTicks: per(t4 - t3),
        cameraMoveTicks: per(t5 - t4), mathSinTicks: per(t6 - t5), drawCulledTicks: per(t7 - t6),
        statsLiteralTicks: per(t8 - t7), drawCulledReusedTicks: per(t9 - t8),
        bulkEulerPerNodeTicks: per(t11 - t10), fillEulerPerNodeTicks: per(t12 - t11),
        followJsTicks: per(t14 - t13), followRigTicks: per(t15 - t14),
        tween64JsFrameTicks: +((t17 - t16) / FRAMES_T).toFixed(1), tween64NativeFrameTicks: +((t19 - t18) / FRAMES_T).toFixed(1)}));
}

// Rotation keys every 0.25 s over 3 s, matching the stage 4 Euler rates.
const KEY_STEP = .25, KEY_COUNT = 13;
function rotationClip() {
    const times = new Float32Array(KEY_COUNT), values = new Float32Array(KEY_COUNT * 4), q = new Quaternion.Quaternion();
    for (let k = 0; k < KEY_COUNT; k++) {
        const t = k * KEY_STEP, angle = t * 2 * Math.PI / 3;
        times[k] = t; values.set(q.setEuler(angle, angle * .5, 0).toArray(), k * 4);
    }
    q.dispose();
    return new Animation3D.Clip([{path: "rotation", times, values}]);
}
let grid = null;
function release() {
    if (!grid) return;
    if (grid.batch) grid.batch.dispose();
    if (grid.scene) grid.scene.dispose();
    for (const cube of grid.cubes) Tween3D.killTweensOf(cube);
    for (const handle of grid.handles) handle.dispose();
    for (const player of grid.players) player.dispose();
    if (grid.clip) grid.clip.dispose();
    if (grid.rig) { grid.rig.dispose(); camera.lookAt(0, 0, -1).setPosition(0, 0, 0); }
    grid = null;
}
function create(stage) {
    grid = {handles: [], items: [], rows: [], cubes: [], players: []};
    if (stage === 0 || stage === 1 || stage === 3 || stage === 7) {
        grid.batch = new Render3D.Batch();
        for (let i = 0; i < COUNT; i++) {
            const item = (stage === 0 ? unlit : lit).createInstance()
                .setPosition(cubeX(i), rowY(Math.floor(i / GRID)), -7).setScale(.25, .25, .25);
            grid.batch.add(item); grid.items.push(item); grid.handles.push(item);
        }
        return;
    }
    grid.scene = new Scene3D.Scene();
    const root = grid.scene.root;
    grid.system = new Scene3D.Node().setPosition(0, 0, -7);
    root.add(grid.system); root.dispose(); grid.handles.push(grid.system);
    for (let r = 0; r < GRID; r++) {
        const row = new Scene3D.Node().setPosition(0, rowY(r), 0);
        grid.system.add(row); grid.rows.push(row); grid.handles.push(row);
        for (let c = 0; c < GRID; c++) {
            const cube = new Scene3D.Node(lit).setPosition(cubeX(c), 0, 0).setScale(.25, .25, .25);
            if (stage === 9) cube.setSpin(2 * Math.PI / 3, Math.PI / 3, 0);
            if (stage === 12) Tween3D.to(cube, {position: [cubeX(c), .3, 0]}, .5 + c * .05,
                {yoyo: true, repeat: Infinity, ease: "inOutSine"});
            if (stage === 10) {
                const player = new Animation3D.Player(grid.clip || (grid.clip = rotationClip()), [cube]);
                player.loop = true; grid.players.push(player.play());
            }
            row.add(cube); grid.cubes.push(cube); grid.handles.push(cube);
        }
    }
}
// Same spin for instances and nodes: each cube about its own centre.
const euler = new Float32Array(COUNT * 3);
function animate(stage, local) {
    const angle = local * Math.PI / 90;
    if (stage === 9) { grid.scene.advance(1 / 60); return; }
    if (stage === 10) { Animation3D.advance(1 / 60); return; }
    if (stage === 12) { Tween3D.advance(1 / 60); return; }
    if (stage === 11) {
        if (!grid.rig) grid.rig = new CameraRig3D.Orbit(camera, grid.system).setLimits(-1, 1, 1, 20)
            .setAngles(0, .2).zoom(1.5);
        grid.rig.autoRotate = .8; grid.rig.update(1 / 60); return;
    }
    if (stage === 7 || stage === 8) {
        for (let i = 0; i < COUNT; i++) { euler[i * 3] = angle; euler[i * 3 + 1] = angle * .5; euler[i * 3 + 2] = 0; }
        if (stage === 7) Model3D.setRotationsEuler(grid.items, euler); else Scene3D.setRotationsEuler(grid.cubes, euler);
        return;
    }
    if (stage === 3) for (const item of grid.items) item.setRotationEuler(angle, angle * .5, 0);
    else if (stage === 4) for (const cube of grid.cubes) cube.setRotationEuler(angle, angle * .5, 0);
    else if (stage === 5 || stage === 6) {
        if (stage === 6) grid.system.setRotationEuler(.3, angle * .25, 0);
        grid.rows.forEach((row, r) => row.setRotationEuler(angle * (r & 1 ? 1 : -1), 0, 0));
    }
}
function median(samples) {
    const sorted = samples.slice().sort((a, b) => a - b), n = sorted.length;
    return n % 2 ? sorted[n >> 1] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
}

console.log("3D profile (QuickJS)");
micro();
const white = Color.new(255, 255, 255, 128), orange = Color.new(255, 128, 0, 128), gray = Color.new(64, 64, 64, 128);
let stage = -1, frame = 0, updateSamples = [], drawSamples = [], totals = null, worldUpdates = 0, pendingUpdate = 0;
function summary() {
    const n = updateSamples.length, mean = s => s.reduce((a, b) => a + b, 0) / n, drawMean = mean(drawSamples);
    const fixed = (value, digits) => +value.toFixed(digits);
    console.log("3D_PROFILE_JS " + JSON.stringify({stage: names[stage], samples: n,
        updateTicksMean: fixed(mean(updateSamples), 1), updateTicksMedian: median(updateSamples),
        drawTicksMean: fixed(drawMean, 1), drawTicksMedian: median(drawSamples),
        drawTicksPerPass: totals.drawPasses ? fixed(drawMean * n / totals.drawPasses, 1) : 0,
        worldUpdatesPerFrame: fixed(worldUpdates / n, 2), drawPassesPerFrame: fixed(totals.drawPasses / n, 2),
        pipelinePassesPerFrame: fixed(totals.pipelinePasses / n, 2),
        trianglesPerFrame: fixed(totals.triangles / n, 2), clippedTrianglesPerFrame: fixed(totals.clippedTriangles / n, 2),
        vuBatchesPerFrame: fixed(totals.vuBatches / n, 2), geometryBytesPerFrame: fixed(totals.geometryBytes / n, 1)}));
}
Loop.run({
    update() {
        const next = Math.floor(frame / FRAMES), local = frame % FRAMES;
        if (stage !== next) {
            if (stage >= 0) summary();
            release(); stage = next; create(stage);
            updateSamples = []; drawSamples = []; worldUpdates = 0;
            totals = {drawPasses: 0, pipelinePasses: 0, triangles: 0, clippedTriangles: 0, vuBatches: 0, geometryBytes: 0};
            console.log("3D profile stage " + stage + ": " + names[stage]);
        }
        const start = Timer.getTime(timer);
        animate(stage, local);
        const u = grid.scene ? grid.scene.update() : null;
        pendingUpdate = Timer.getTime(timer) - start;
        if (local >= WARMUP && u) worldUpdates += u.worldUpdates;
    },
    draw() {
        const local = frame % FRAMES, start = Timer.getTime(timer);
        const s = grid.batch ? grid.batch.draw(camera, Render3D.CULL_NONE, lights)
            : grid.scene.draw(camera, Render3D.CULL_NONE, lights);
        const end = Timer.getTime(timer);
        if (local >= WARMUP) {
            updateSamples.push(pendingUpdate); drawSamples.push(end - start);
            totals.drawPasses += s.drawPasses; totals.pipelinePasses += s.pipelinePasses; totals.triangles += s.triangles;
            totals.clippedTriangles += s.clippedTriangles; totals.vuBatches += s.vuBatches;
            totals.geometryBytes += s.geometryBytes;
        }
        for (let i = 0; i < names.length; i++) Draw.rect(12 + i * 20, 12, 14, 14, i === stage ? orange : gray);
        Draw.rect(12, height - 20, Math.max(1, Math.floor((mode.width - 24) * (local + 1) / FRAMES)), 8, white);
        if (++frame === names.length * FRAMES) {
            summary(); release(); unlit.dispose(); lit.dispose(); lights.dispose(); camera.dispose(); Timer.destroy(timer);
            console.log("3D profile complete; stages 0-5 and 7-10 show the same 8x8 grid of 64 cubes; stage 11 circles it; stage 12 bobs it.");
            Loop.stop();
        }
    }
});
