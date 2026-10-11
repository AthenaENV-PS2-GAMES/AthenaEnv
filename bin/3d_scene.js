// Deterministic counterpart of samples/native/3d_scene/main.c.
// Seven 180-frame stages; the script only edits node transforms. Update,
// world matrices, bounds, culling and submission run natively.
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect: mode.width / height, near: 1, far: 30});
camera.lookAt(0, 0, -1).setPosition(0, 0, 0);
const lights = new Lights.Set().setAmbient(.25, .25, .25).setDirectional(0, .4, .6, 1, .8, .8, .8);
const names = ["hierarchy", "reparent", "visibility", "subtree-cull", "many-nodes", "loop-system", "recreate"];
const GRID = 8;
const timer = Timer.new();
let stage = -1, frame = 0, demo = null, updateTicks = 0, drawTicks = 0;
function summary() {
    console.log("3D scene: stage=" + names[stage] + " mean over 120 frames updateTicks=" +
        (updateTicks / 120).toFixed(1) + " drawTicks=" + (drawTicks / 120).toFixed(1) + " (Timer units)");
}
function release() {
    if (!demo) return;
    demo.scene.dispose();   // also detaches it from the Loop
    for (const node of demo.nodes) node.dispose();
    demo = null;
}
function create() {
    const cube = Model3D.load("models/lit_cube.glb", {shading: Model3D.DIFFUSE,
        baseColor: new Float32Array([1, .85, .4, 1])});
    const marker = Model3D.Mesh.fromGeometry({positions: new Float32Array([-.5,-.5,0, .5,-.5,0, 0,.5,0]),
        colors: new Float32Array([1,0,0,1, 0,1,0,1, 0,0,1,1])});
    const scene = new Scene3D.Scene(), root = scene.root, system = new Scene3D.Node();
    root.add(system.setPosition(0, 0, -7)); root.dispose();
    demo = {scene, system, nodes: [system], rows: []};
    if (stage === 4) {
        for (let r = 0; r < GRID; r++) {
            const row = new Scene3D.Node().setPosition(0, (r - 3.5) * .7, 0);
            system.add(row); demo.rows.push(row); demo.nodes.push(row);
            for (let c = 0; c < GRID; c++) {
                const n = new Scene3D.Node(cube).setPosition((c - 3.5) * .9, 0, 0).setScale(.25, .25, .25);
                row.add(n); n.dispose();   // the row retains it
            }
        }
    } else {
        demo.sun = new Scene3D.Node(cube).setScale(.8, .8, .8);
        demo.planet = new Scene3D.Node(cube).setScale(.4, .4, .4);
        demo.moon = new Scene3D.Node(marker).setPosition(0, 0, 2.2);
        system.add(demo.sun).add(demo.planet); demo.planet.add(demo.moon);
        demo.nodes.push(demo.sun, demo.planet, demo.moon);
    }
    cube.dispose(); marker.dispose();
    if (stage === 5) scene.attachLoop();
}
function animate(local) {
    const angle = local * Math.PI / 90;
    if (stage === 4) {
        demo.system.setRotationEuler(.3, angle * .25, 0);
        demo.rows.forEach((row, r) => row.setRotationEuler(angle * (r & 1 ? 1 : -1), 0, 0));
        return;
    }
    demo.sun.setRotationEuler(.35, angle * .5, 0);
    demo.planet.setPosition(3 * Math.cos(angle * .5), 0, 3 * Math.sin(angle * .5)).setRotationEuler(0, angle * 2, 0);
    demo.system.setRotationEuler(.25, 0, 0);
    if (stage === 1 && local % 60 === 0) {
        const toSun = local % 120 === 0;
        (toSun ? demo.sun : demo.planet).add(demo.moon);
        console.log("3D scene: moon parented to " + (toSun ? "sun" : "planet"));
    }
    if (stage === 2 && local % 45 === 0) demo.planet.visible = !demo.planet.visible;
    if (stage === 3) demo.system.setPosition(9 * Math.sin(angle * .5), 0, -7);
}
const white = Color.new(255, 255, 255, 128), orange = Color.new(255, 128, 0, 128), gray = Color.new(64, 64, 64, 128);
Loop.run({
    update() {
        const next = Math.floor(frame / 180), local = frame % 180;
        if (stage !== next) {
            if (stage >= 0) summary();
            release(); stage = next; updateTicks = drawTicks = 0;
            console.log("3D scene stage " + stage + ": " + names[stage]);
        }
        if (!demo) create();
        animate(local);
    },
    draw() {
        const local = frame % 180;
        let start = Timer.getTime(timer), u = null;
        // The attached stage was updated by the Loop's native POST_UPDATE system.
        if (!demo.scene.attached) u = demo.scene.update();
        const middle = Timer.getTime(timer);
        const s = demo.scene.draw(camera, Render3D.CULL_NONE, lights);
        const end = Timer.getTime(timer);
        if (local >= 60) { updateTicks += middle - start; drawTicks += end - middle; }
        if (local % 60 === 0) console.log("3D scene: stage=" + names[stage] + " visited=" + (u ? u.visitedNodes : 0) +
            " worldUpdates=" + (u ? u.worldUpdates : 0) + " queued=" + s.queuedObjects + " submitted=" + s.submittedObjects +
            " culled=" + s.culledObjects + " culledSubtrees=" + s.culledSubtrees + " triangles=" + s.triangles +
            " geometryBytes=" + s.geometryBytes);
        if (stage === 6 && local % 30 === 29) release();
        for (let i = 0; i < names.length; i++) Draw.rect(12 + i * 20, 12, 14, 14, i === stage ? orange : gray);
        Draw.rect(12, height - 20, Math.max(1, Math.floor((mode.width - 24) * (local + 1) / 180)), 8, white);
        if (++frame === names.length * 180) {
            summary(); release(); lights.dispose(); camera.dispose(); Timer.destroy(timer);
            console.log("3D scene complete; inspect images before recording visual PASS."); Loop.stop();
        }
    }
});
