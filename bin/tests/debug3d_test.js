/*
 * Debug3D module: shape helpers, queue lifetime, projection and clipping.
 * Runs on PCSX2/PS2 and on the host (tests/js/run.sh), where the GS stubs of
 * the runner count the lines drawn (__nativeDraws); those checks are skipped
 * on the console, which also prints the cost of drawing 2000 segments.
 */
import * as Debug3D from "Debug3D";
import { Camera } from "Camera3D";
import { Matrix4 } from "Matrix4";
import * as Model3D from "Model3D";

let passed = 0, failed = 0;
function check(name, condition) {
    if (condition) passed++;
    else { failed++; console.log("[FAIL] " + name); }
}
function throws(name, callback, type) {
    let error = null;
    try { callback(); } catch (e) { error = e; }
    check(name + " throws" + (type ? " " + type.name : ""), error !== null && (!type || error instanceof type));
}
const host = typeof globalThis.__nativeDraws === "function";
const draws = () => host ? globalThis.__nativeDraws() : null;

const camera = new Camera({ fovYDegrees: 90, aspect: 640 / 448, near: 1, far: 100 });
camera.setPosition(0, 0, 10).lookAt(0, 0, 0);
Debug3D.clear(); draws();

check("MAX_LINES", Debug3D.MAX_LINES === 8192);
check("line queued", Debug3D.line(0, 0, 0, 1, 0, 0) === true && Debug3D.count() === 1);
check("box is 12 segments", Debug3D.box(-1, -1, -1, 1, 1, 1) === 12);
check("oriented box", Debug3D.box(-1, -1, -1, 1, 1, 1, undefined, 0, new Matrix4()) === 12);
check("sphere 3 x 16", Debug3D.sphere(0, 0, 0, 1) === 48 && Debug3D.sphere(0, 0, 0, 1, undefined, 0, 4) === 12);
check("axes", Debug3D.axes(new Matrix4(), 2) === 3);
check("grid lines", Debug3D.grid(0, 0, 0, 2, 1) === 10);
const other = new Camera({ near: 1, far: 5 });
check("frustum", Debug3D.frustum(other) === 12);
const mesh = Model3D.Mesh.fromGeometry({ positions: new Float32Array([0,0,0, 1,0,0, 0,1,0]),
    normals: new Float32Array([0,0,1, 0,0,1, 0,0,1]), material: { shading: Model3D.DIFFUSE } });
const instance = mesh.createInstance();
check("normals of a mesh", Debug3D.normals(mesh) === 3);
check("normals of an instance", Debug3D.normals(instance, 0.5) === 3);
const bare = Model3D.Mesh.fromGeometry({ positions: new Float32Array([0,0,0, 1,0,0, 0,1,0]) });
check("no normals, no segments", Debug3D.normals(bare) === 0);
check("bulk lines", Debug3D.lines(new Float32Array([0,0,0, 0,1,0, 0,0,0, 0,0,1])) === 2);
const queued = Debug3D.count();
check("queue count", queued === 1 + 12 + 12 + 48 + 12 + 3 + 10 + 12 + 3 + 3 + 2);

throws("line needs 6 numbers", () => Debug3D.line(0, 0, 0, 1, 1), TypeError);
throws("non-finite point", () => Debug3D.line(0, 0, 0, NaN, 0, 0), RangeError);
throws("bad color", () => Debug3D.line(0, 0, 0, 1, 1, 1, "red"), TypeError);
throws("negative seconds", () => Debug3D.line(0, 0, 0, 1, 1, 1, undefined, -1), RangeError);
throws("inverted box", () => Debug3D.box(1, 0, 0, 0, 1, 1), RangeError);
throws("sphere segments", () => Debug3D.sphere(0, 0, 0, 1, undefined, 0, 2), RangeError);
throws("grid step", () => Debug3D.grid(0, 0, 0, 10, 0), RangeError);
throws("grid size", () => Debug3D.grid(0, 0, 0, 1000, 1), RangeError);
throws("lines stride", () => Debug3D.lines(new Float32Array(5)), RangeError);
throws("normals target", () => Debug3D.normals({}), TypeError);
throws("frustum camera", () => Debug3D.frustum({}), TypeError);
check("invalid shapes queue nothing", Debug3D.count() === queued);

// Lifetime: frame-only segments go after one draw; timed ones after their seconds.
Debug3D.clear();
Debug3D.line(-1, 0, 0, 1, 0, 0);
Debug3D.line(0, -1, 0, 0, 1, 0, undefined, 0.5);
Debug3D.age(1);
check("undrawn segments survive age()", Debug3D.count() === 2);
let drawn = Debug3D.draw(camera, 0.25);
check("draw returns visible segments", drawn === 2);
check("frame-only segment removed after its draw", Debug3D.count() === 1);
Debug3D.draw(camera, 0.3);
check("timed segment expires", Debug3D.count() === 0);

// Projection: the centre of the screen and clipping.
if (host) {
    draws();
    Debug3D.line(0, 0, 0, 10, 0, 0);
    Debug3D.draw(camera);
    let d = draws();
    check("centre projects to 320,224", d.lines === 1 && Math.abs(d.firstLine[0] - 320) < 0.01 && Math.abs(d.firstLine[1] - 224) < 0.01);
    Debug3D.line(0, 0, 20, 0, 0, 30);          // behind the camera
    Debug3D.line(-100, 0, 0, -90, 0, 0);       // off screen to the left
    Debug3D.line(0, 0, 0, 0, 0, 20);           // crosses the near plane
    Debug3D.draw(camera);
    d = draws();
    check("behind and off-screen segments are clipped away", d.lines === 1);
    check("near-plane crossing keeps the visible part from the centre", Math.abs(d.firstLine[0] - 320) < 0.01);
    for (let i = 0; i < 600; i++) Debug3D.line(-1, i / 600, 0, 1, i / 600, 0);
    Debug3D.draw(camera);
    d = draws();
    check("batches of at most 256 lines", d.lines === 600 && d.lineLists === 3);
}

// Full queue: drops are counted.
Debug3D.clear();
const many = new Float32Array(6 * 9000);
for (let i = 0; i < 9000; i++) many.set([0, 0, 0, 1, i / 9000, 0], i * 6);
check("queue caps at MAX_LINES", Debug3D.lines(many) === 8192 && Debug3D.count() === 8192 && Debug3D.dropped() === 808);
check("full queue rejects", Debug3D.line(0, 0, 0, 1, 1, 1) === false && Debug3D.dropped() === 809);
Debug3D.clear();
check("clear resets", Debug3D.count() === 0 && Debug3D.dropped() === 0);

// show(false): nothing is queued.
check("show off", Debug3D.show(false) === false && Debug3D.box(0, 0, 0, 1, 1, 1) === 0 && Debug3D.count() === 0);
check("show on", Debug3D.show(true) === true && Debug3D.box(0, 0, 0, 1, 1, 1) === 12);
Debug3D.clear();

// setCamera(): a native Loop system; null removes it.
Debug3D.setCamera(camera);
throws("setCamera type", () => Debug3D.setCamera({}), TypeError);
Debug3D.setCamera(null);
Debug3D.setCamera(camera);
camera.dispose();                           // retained by the system
Debug3D.setCamera(null);

// Cost on the console: 2000 segments (one 1000-line Float32Array, twice).
if (!host) {
    const cam = new Camera({ fovYDegrees: 60, aspect: 640 / 448, near: 0.5, far: 200 });
    cam.setPosition(0, 5, 20).lookAt(0, 0, 0);
    const grid = new Float32Array(6 * 1000);
    for (let i = 0; i < 1000; i++) grid.set([-10 + i * 0.02, 0, -10, -10 + i * 0.02, 0, 10], i * 6);
    const t0 = Date.now();
    let frames = 0;
    for (; frames < 30; frames++) { Debug3D.lines(grid); Debug3D.lines(grid); Debug3D.draw(cam); }
    console.log(`[Debug3D] 2000 segments: ${((Date.now() - t0) / frames).toFixed(2)} ms per frame (queue + project + GS packets)`);
    cam.dispose();
}

instance.dispose(); mesh.dispose(); bare.dispose(); other.dispose();
console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("Debug3D module test passed");
