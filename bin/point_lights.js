// Point lights on PS2: a 7 x 7 grid of lit cubes. Stage 0 has ambient and
// one directional light; stage 1 adds three coloured point lights circling
// the grid (lit per vertex on VU1); stage 2 lowers the camera into the grid,
// so the cubes it passes are clipped at the near plane on VU1 with the same
// lighting; stage 3 adds distance fog (dark blue from 6 to 20 units), seen
// from a low camera across the grid. 180-frame stages (60 warm-up) print POINT_LIGHTS lines with the
// draw ticks (1 tick = 1 us) and the near-clipped object count.
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect: mode.width / height, near: .5, far: 60});
const lights = new Lights.Set().setAmbient(.15, .15, .15).setDirectional(0, .2, .8, .4, .35, .35, .35);
const names = ["directional", "point-3", "point-3-near", "point-3-fog"];
const FRAMES = 180, WARMUP = 60, timer = Timer.new(), stats = {};
const cube = Model3D.load("models/lit_cube.glb", {shading: Model3D.DIFFUSE, baseColor: new Float32Array([.9, .9, .9, 1])});
const scene = new Scene3D.Scene(), root = scene.root, nodes = [];
for (let i = 0; i < 49; i++) {
    const node = new Scene3D.Node().setMesh(cube).setPosition((i % 7 - 3) * 2.2, 0, (Math.floor(i / 7) - 3) * 2.2).setScale(.7, .7, .7);
    root.add(node); nodes.push(node);
}
scene.update();
const colors = [[1, .2, .1], [.1, 1, .3], [.2, .4, 1]];
let stage = -1, frame = 0, draws = [], near = [];
const mean = v => v.reduce((a, b) => a + b, 0) / v.length;
function report() {
    console.log("POINT_LIGHTS " + JSON.stringify({stage: names[stage], samples: draws.length,
        drawTicksMean: +mean(draws).toFixed(1), nearClipObjectsMean: +mean(near).toFixed(1), triangles: stats.triangles}));
}
Loop.run({
    update() {
        const next = Math.floor(frame / FRAMES);
        if (stage !== next) {
            if (stage >= 0) report();
            stage = next; draws = []; near = [];
            if (stage === 3) lights.setFog(6, 20, .05, .08, .2); else lights.disableFog();
            console.log("Point lights stage " + stage + ": " + names[stage]);
        }
        const t = frame / 60;
        for (let i = 0; i < 3; i++) {
            if (stage === 0) { lights.disablePoint(i); continue; }
            const a = t * .8 + i * 2.094;
            lights.setPoint(i, Math.cos(a) * 4.5, 1.6, Math.sin(a) * 4.5, colors[i][0], colors[i][1], colors[i][2], 6);
        }
        if (stage === 2) camera.setPosition(Math.sin(t * .5) * 3, .3, 6.5).lookAt(0, 0, -2);
        else if (stage === 3) camera.setPosition(0, 2.5, 10).lookAt(0, 0, -6);
        else camera.setPosition(0, 11, 13).lookAt(0, 0, 0);
    },
    draw() {
        const start = Timer.getTime(timer);
        scene.draw(camera, Render3D.CULL_BACK, lights, stats);
        if (frame % FRAMES >= WARMUP) { draws.push(Timer.getTime(timer) - start); near.push(stats.nearClipObjects); }
        if (++frame === names.length * FRAMES) {
            report(); console.log("Point lights complete;"); Timer.destroy(timer); Loop.stop();
        }
    }
});
