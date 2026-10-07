import {Matrix4} from "Matrix4";
import {Quaternion} from "Quaternion";
import {Camera} from "Camera3D";
import * as Model3D from "Model3D";
import * as Render3D from "Render3D";
let checks = 0;
function assert(ok, label) { checks++; if (!ok) throw new Error(label); }
function throws(fn, label) { let raised = false; try { fn(); } catch (_) { raised = true; } assert(raised, label); }
function throwsMessage(fn, fragment, label) {
    try { fn(); } catch (e) { assert(e instanceof RangeError && e.message.includes(fragment), label); return; }
    assert(false, label);
}
function close(a, b) { return Math.abs(a - b) < 0.0001; }
const camera = new Camera({near: 1, far: 10, aspect: 1});
throws(() => new Camera({aspect: 0}), "invalid projection");
throws(() => camera.setPosition(0, 0, 0), "degenerate camera");
const view = camera.getView();
view.set(14, 0);
assert(close(camera.getView().get(14), -5), "camera matrix is an owned copy");
const out = new Matrix4();
assert(camera.getView(out) === out && close(out.get(14), -5), "reusable matrix output");
const t = new Matrix4().set(12, 10), s = new Matrix4().set(0, 2);
const product = t.multiply(s);
assert(close(product.get(12), 10) && close(product.get(0), 2), "A * B composition");
assert(t.get(0) === 1 && s.get(12) === 0, "multiply preserves operands");
const bad = Array(16).fill(0); bad[7] = NaN;
const saved = t.toArray().join(",");
throws(() => t.fromArray(bad), "NaN rejection");
assert(t.toArray().join(",") === saved, "fromArray fails atomically");
throws(() => t.fromArray({get 0() { throw new Error("getter"); }}), "throwing matrix getter");
throws(() => t.set(0, Infinity), "matrix finite values");
const q = new Quaternion().setAxisAngle(0, 0, 1, Math.PI / 2);
assert(close(q.toArray()[2], Math.sqrt(0.5)), "axis-angle radians");
throws(() => q.setAxisAngle(0, 0, 0, 1), "zero axis rejection");
throws(() => q.slerp(new Quaternion(), 2), "slerp bounds");
throws(() => new Quaternion(0, 0, 0, 0), "zero quaternion rejection");
const negative = new Quaternion(0, 0, 0, -1);
assert(close(new Quaternion().slerp(negative, 0.5).toArray()[3], 1), "antipodal shortest path");
// Closed-form Euler matches qz * qy * qx; the shared float conversion keeps
// ints, float32 and float64 inputs and rejects values beyond the float range.
const qx = new Quaternion().setAxisAngle(1, 0, 0, .3), qy = new Quaternion().setAxisAngle(0, 1, 0, -1.2);
const qz = new Quaternion().setAxisAngle(0, 0, 1, 2.5);
const composed = qz.multiply(qy.multiply(qx)).toArray(), closedForm = new Quaternion().setEuler(.3, -1.2, 2.5).toArray();
const sign = composed.reduce((sum, value, i) => sum + value * closedForm[i], 0) < 0 ? -1 : 1;
assert(composed.every((value, i) => close(value, closedForm[i] * sign)), "Euler closed form");
assert(close(new Quaternion().setEuler(0, 0, 3).toArray()[2], Math.sin(1.5)), "integer Euler argument");
assert(close(new Quaternion().setEuler(0, 0, new Float32Array([3])[0]).toArray()[2], Math.sin(1.5)), "float32 Euler argument");
throws(() => new Quaternion().setEuler(1e39, 0, 0), "float range rejection");
throws(() => new Quaternion().setEuler(-Infinity, 0, 0), "infinite rejection");
let typed = false;
try { new Quaternion().setEuler("1", 0, 0); } catch (e) { typed = e instanceof TypeError; }
assert(typed, "non-number TypeError");
const backing = new Float32Array([99,99,99, -1,-1,0, 1,-1,0, 0,1,0, 99]);
const colors = new Float32Array([9,9,9,9, 1,0,0,1, 0,1,0,1, 0,0,1,1]);
const indices = new Uint32Array([99, 2,0,1, 99]);
const mesh = Model3D.Mesh.fromGeometry({
    positions: backing.subarray(3, 12), colors: colors.subarray(4), indices: indices.subarray(1, 4)
});
assert(mesh.vertexCount === 3, "typed array offsets and lengths");
// The indexed cube used by bin/3d.js, including fractional vertex colors.
const cube = Model3D.Mesh.fromGeometry({
    positions: new Float32Array([
        -1,-1,-1, 1,-1,-1, 1,1,-1, -1,1,-1,
        -1,-1,1, 1,-1,1, 1,1,1, -1,1,1
    ]),
    colors: new Float32Array([
        1,0,0,1, 0,1,0,1, 0,0,1,1, 1,1,0,1,
        1,0,1,1, 0,1,1,1, 1,1,1,1, 0.4,0.6,1,1
    ]),
    indices: new Uint32Array([
        0,2,1, 0,3,2, 4,5,6, 4,6,7,
        0,1,5, 0,5,4, 3,7,6, 3,6,2,
        0,4,7, 0,7,3, 1,2,6, 1,6,5
    ])
});
assert(cube.vertexCount === 36, "demo indexed cube expands to twelve triangles");
cube.dispose();
backing.fill(NaN); colors.fill(NaN); indices.fill(99);
const instance = mesh.createInstance(), other = mesh.createInstance().setPosition(1000, 0, 0);
mesh.dispose(); mesh.dispose();
throws(() => mesh.createInstance(), "disposed mesh");
instance.setPosition(1, 2, 0).setRotationQuaternion(0, 0, 0, 1);
const transform = instance.getTransform();
transform.set(12, 100);
assert(close(instance.getTransform().get(12), 1), "instance matrix is an owned copy");
assert(other.getTransform().get(12) === 1000, "instances have independent transforms");
instance.setPosition(0, 0, 0);
const batch = new Render3D.Batch().add(instance).add(other);
assert(batch.size === 2, "batch count");
instance.dispose(); other.dispose(); std.gc();
const stats = batch.draw(camera, Render3D.CULL_NONE);
assert(stats.submittedObjects === 2 && stats.culledObjects === 1 && stats.triangles === 1,
    "batch retains disposed wrappers; frustum culling is native");
assert(stats.sourceTriangles === 1 && stats.clippedTriangles === 0 && stats.rejectedTriangles === 0,
    "contained objects keep the interior path");
assert(stats.geometryBytes === 64, "contained DMA payload includes padding and excludes culled objects");
assert(stats.drawPasses === 1 && stats.pipelinePasses === 1, "culled objects open no pass");
const reused = {extra: 7};
assert(batch.draw(camera, Render3D.CULL_NONE, undefined, reused) === reused && reused.extra === 7 &&
    reused.submittedObjects === 2 && reused.triangles === 1 && reused.geometryBytes === 64, "batch stats reuse an object");
reused.triangles = -1;
assert(batch.draw(camera, Render3D.CULL_NONE, undefined, reused).triangles === 1, "reused stats are overwritten");
throws(() => batch.draw(camera, Render3D.CULL_NONE, undefined, 3), "stats must be an object");
assert(batch.draw(camera, Render3D.CULL_NONE, undefined, undefined).submittedObjects === 2, "undefined stats allocate");
const clipCamera = new Camera({near: 1, far: 10, aspect: 1});
clipCamera.lookAt(0, 0, -1).setPosition(0, 0, 0);
const clipMesh = Model3D.Mesh.fromGeometry({positions: new Float32Array([-.1,-.1,.5, .3,-.1,-2, -.1,.3,-2])});
const clipInstance = clipMesh.createInstance(); clipMesh.dispose();
const clipStats = Render3D.draw(clipInstance, clipCamera, Render3D.CULL_NONE);
assert(clipStats.sourceTriangles === 1 && clipStats.triangles === 2 && clipStats.clippedTriangles === 1 &&
    clipStats.rejectedTriangles === 0 && clipStats.vuBatches === 1, "near-plane clipping expands a triangle natively");
assert(clipStats.geometryBytes === 160, "clipped DMA payload uses xyzw and padding");
clipInstance.dispose(); clipCamera.dispose();
const rejectMesh = Model3D.Mesh.fromGeometry({positions: new Float32Array([
    -100,-.5,0, -100,.5,0, -101,0,0, 100,-.5,0, 100,.5,0, 101,0,0
])});
const rejectInstance = rejectMesh.createInstance(); rejectMesh.dispose();
const rejected = Render3D.draw(rejectInstance, camera, Render3D.CULL_NONE);
assert(rejected.culledObjects === 0 && rejected.sourceTriangles === 2 && rejected.rejectedTriangles === 2 &&
    rejected.triangles === 0 && rejected.vuBatches === 0, "triangle rejection after conservative AABB test");
assert(rejected.geometryBytes === 0, "rejected triangles emit no geometry payload");
rejectInstance.dispose();
throws(() => instance.setPosition(0, 0, 0), "disposed instance");
throws(() => batch.draw(camera, 20), "invalid cull mode");
for (const [obj, method] of [[q,"dispose"], [camera,"dispose"], [batch,"dispose"], [mesh,"dispose"], [instance,"dispose"]]) {
    const foreign = {sentinel: "intact"};
    throws(() => Object.getPrototypeOf(obj)[method].call(foreign), "foreign dispose receiver");
    assert(foreign.sentinel === "intact", "foreign object remains intact");
}
// Bulk setters match the per-instance setters and validate before writing.
const bulkMesh = Model3D.Mesh.fromGeometry({positions: new Float32Array([0,0,0, 1,0,0, 0,1,0])});
const bulk = [bulkMesh.createInstance(), bulkMesh.createInstance()];
const single = [bulkMesh.createInstance(), bulkMesh.createInstance()];
const xyz = new Float32Array([1, 2, 3, -4, .5, 6, 99]);
const euler = new Float32Array([.1, .2, .3, -.4, .5, -.6]);
assert(Model3D.setPositions(bulk, xyz) === 2 && Model3D.setRotationsEuler(bulk, euler) === 2, "bulk setters return the count");
for (let i = 0; i < 2; i++) {
    single[i].setPosition(xyz[i*3], xyz[i*3+1], xyz[i*3+2]).setRotationEuler(euler[i*3], euler[i*3+1], euler[i*3+2]);
    const a = bulk[i].getTransform(), b = single[i].getTransform();
    let same = true; for (let k = 0; k < 16; k++) same = same && a.get(k) === b.get(k);
    assert(same, "bulk transform " + i + " equals per-instance setters");
}
const before = bulk[0].getTransform().get(12);
throws(() => Model3D.setPositions(bulk, new Float32Array([7, 7, 7, 7, NaN, 7])), "non-finite bulk value");
throws(() => Model3D.setPositions(bulk, new Float32Array(5)), "short bulk values");
throws(() => Model3D.setPositions(bulk, [0, 0, 0, 0, 0, 0]), "bulk values must be a Float32Array");
throws(() => Model3D.setPositions(bulk[0], xyz), "bulk instances must be an array");
throws(() => Model3D.setPositions([bulk[0], {}], xyz), "bulk instances must be instances");
assert(bulk[0].getTransform().get(12) === before, "failed bulk calls change nothing");
// An element getter that disposes an earlier handle cannot free it mid-call.
const tricky = [single[0]];
Object.defineProperty(tricky, 1, {get() { single[0].dispose(); std.gc(); return single[1]; }});
assert(Model3D.setPositions(tricky, xyz) === 2, "handles are retained while the call runs");
assert(Model3D.setPositions([], new Float32Array(0)) === 0, "empty bulk call");
bulk.forEach(i => i.dispose()); single[1].dispose(); bulkMesh.dispose();
const risky = new Camera();
throws(() => risky.setProjection({get aspect() { risky.dispose(); return 1; }}), "reentrant camera disposal");
const sentinel = {};
try { camera.setProjection({get near() { throw sentinel; }}); throw new Error("missing exception"); }
catch (e) { assert(e === sentinel, "preserves getter exception"); }
throws(() => Model3D.Mesh.fromGeometry({positions: [0,0,0]}), "strict Float32Array");
throws(() => Model3D.Mesh.fromGeometry({positions: new Float32Array(6)}), "triangle list length");
throws(() => Model3D.Mesh.fromGeometry({positions: new Float32Array(9), indices: new Uint32Array([0,1,3])}), "index bounds");
throws(() => Model3D.Mesh.fromGeometry({positions: new Float32Array(9), colors: new Float32Array(4)}), "color count");
throws(() => Model3D.Mesh.fromGeometry({positions: new Float32Array([NaN,0,0,0,0,0,0,0,0])}), "finite positions");
throwsMessage(() => Model3D.Mesh.fromGeometry({positions: new Float32Array(9), indices: new Uint32Array([0,3,1])}),
    "indices[1] = 3 exceeds vertex count 3", "actionable index error");
throwsMessage(() => Model3D.Mesh.fromGeometry({positions: new Float32Array(9), colors: new Float32Array([1,1,1,1,1,2,1,1,1,1,1,1])}),
    "colors[5]", "actionable color error");
const ieeePositions = new Float32Array(9), ieeeBits = new Uint32Array(ieeePositions.buffer);
for (const bits of [0x7f800000, 0xff800000, 0x7fc00000, 0x7f800001]) {
    ieeeBits[4] = bits;
    throwsMessage(() => Model3D.Mesh.fromGeometry({positions: ieeePositions}), "positions[4]", "reject IEEE nonfinite bit pattern");
}
throws(() => Model3D.load("missing.obj"), "missing model");
throws(() => Model3D.load("a\0.obj"), "NUL path");
throws(() => Model3D.Mesh.fromGeometry({positions: new Float32Array(new SharedArrayBuffer(36))}), "shared input rejected");
const loaded = Model3D.load("../tests/host/3d/triangle.gltf");
assert(loaded.vertexCount === 3, "glTF loader");
loaded.dispose();
const binaryModel = Model3D.load("../tests/host/3d/triangle.glb");
assert(binaryModel.vertexCount === 3, "GLB loader"); binaryModel.dispose();
throws(() => Model3D.load("../tests/host/3d/transformed.gltf"), "unsupported glTF transform");
batch.clear(); batch.dispose(); batch.dispose();
q.dispose(); q.dispose(); negative.dispose(); camera.dispose(); camera.dispose();
throws(() => q.toArray(), "disposed quaternion");
throws(() => camera.getView(), "disposed camera");
throws(() => batch.clear(), "disposed batch");
std.gc();
console.log("3D module tests passed (" + checks + " checks)");
