import {Matrix4} from "Matrix4";
import {Camera} from "Camera3D";
import * as Model3D from "Model3D";
import * as Scene3D from "Scene3D";
import * as Lights from "Lights";
let checks = 0;
function assert(ok, label) { checks++; if (!ok) throw new Error(label); }
function throws(fn, type, label) {
    let error = null; try { fn(); } catch (e) { error = e; }
    assert(error !== null && (!type || error instanceof type), label);
}
function close(a, b) { return Math.abs(a - b) < 1e-4; }
const POST_UPDATE = 2;
const mesh = Model3D.Mesh.fromGeometry({positions: new Float32Array([-1,-1,-1, 1,-1,-1, 1,1,1])});
const scene = new Scene3D.Scene();
assert(Scene3D.MAX_DEPTH === 64, "depth limit export");
const root = scene.root;
const parent = new Scene3D.Node(), child = new Scene3D.Node(mesh), leaf = new Scene3D.Node().setMesh(mesh);
mesh.dispose(); std.gc();
assert(child.hasMesh && leaf.hasMesh && !parent.hasMesh, "nodes retain meshes after handle disposal");
assert(root.add(parent) === root && parent.add(child).add(new Scene3D.Node()) === parent, "chainable add");
child.add(leaf);
parent.setPosition(10, 0, 0).setRotationEuler(0, 0, Math.PI / 2).setScale(2, 2, 2);
child.setPosition(1, 0, 0); leaf.setPosition(0, 0, -3);
assert(scene.stale, "setters only mark dirty");
throws(() => child.getWorldTransform(), InternalError, "stale world query throws");
throws(() => scene.draw(new Camera()), InternalError, "draw never updates implicitly");
let u = scene.update();
assert(!scene.stale && u.visitedNodes === 5 && u.worldUpdates === 5, "first update visits every node");
const world = child.getWorldTransform();
assert(close(world.get(12), 10) && close(world.get(13), 2), "world = parent * local");
const out = new Matrix4();
assert(leaf.getWorldTransform(out) === out && close(out.get(14), -6), "world into out");
assert(close(leaf.getLocalTransform().get(14), -3), "local transform");
const b = parent.getWorldBounds();
assert(close(b.min[2], -8) && close(b.max[2], 2) && close(b.min[0], 8), "subtree bounds");
assert(parent.getChild(1).getWorldBounds() === null, "empty subtree has no bounds");
u = scene.update(); assert(u.visitedNodes === 0, "clean update is free");
leaf.setPosition(0, 0, -4); u = scene.update();
assert(u.worldUpdates === 1 && u.visitedNodes === 4, "only the dirty path is visited");
// Rejected edits are atomic.
throws(() => leaf.add(parent), RangeError, "cycle rejected");
throws(() => leaf.add(leaf), RangeError, "self parent rejected");
throws(() => leaf.add(scene.root), RangeError, "scene root cannot be a child");
throws(() => leaf.setPosition(NaN, 0, 0), RangeError, "nonfinite position");
throws(() => leaf.setRotationQuaternion(0, 0, 0, 0), RangeError, "zero quaternion");
throws(() => leaf.setPosition("1", 0, 0), TypeError, "strict numbers");
throws(() => leaf.add({}), TypeError, "foreign child");
throws(() => leaf.visible = 1, TypeError, "visible needs a boolean");
throws(() => leaf.getChild(0), RangeError, "child index");
assert(!scene.stale && leaf.getParent() !== null && child.childCount === 1, "graph unchanged");
let tail = new Scene3D.Node(); const chain = tail;
for (let i = 1; i < Scene3D.MAX_DEPTH - 1; i++) { const next = new Scene3D.Node(); tail.add(next); tail = next; }
root.add(chain);
throws(() => tail.add(new Scene3D.Node()), RangeError, "depth limit");
chain.detach(); assert(scene.stale, "detach marks the old parent"); scene.update();
// Draw: culling, visibility, stats; lights are optional and borrowed.
const camera = new Camera({near: 1, far: 100, aspect: 1});
camera.setPosition(5, 0, 40).lookAt(5, 0, 0);
const lights = new Lights.Set();
let s = scene.draw(camera, 0, lights);
assert(s.queuedObjects === 2 && s.submittedObjects === 2 && s.triangles === 2 && s.culledSubtrees === 0, "draw queue");
parent.visible = false; assert(scene.stale && !parent.visible, "visibility marks dirty");
u = scene.update(); assert(u.worldUpdates === 0, "visibility does not recompute transforms");
assert(parent.getWorldBounds() === null && scene.draw(camera).queuedObjects === 0, "hidden subtree");
parent.visible = true; parent.setPosition(1000, 0, 0); scene.update();
s = scene.draw(camera, 0);
assert(s.culledSubtrees === 1 && s.culledObjects === 2 && s.submittedObjects === 2 && s.queuedObjects === 0, "subtree culling");
throws(() => scene.draw(camera, 3), RangeError, "cull mode");
throws(() => scene.draw({}), TypeError, "camera class");
// Reparenting keeps local transforms.
root.add(leaf); scene.update();
assert(close(leaf.getWorldTransform().get(14), -4) && child.childCount === 0, "reparent");
leaf.detach(); throws(() => leaf.getWorldBounds(), InternalError, "outside the scene is stale");
root.add(leaf);
// Loop integration: a native POST_UPDATE system.
assert(scene.attachLoop(-5) === scene && scene.attached, "attach");
throws(() => scene.attachLoop(), InternalError, "single attachment");
throws(() => scene.attachLoop(.5), RangeError, "integer priority");
leaf.setPosition(0, 1, -4); assert(scene.stale, "dirty before the frame");
__runNativeSystems(POST_UPDATE, 1 / 60, 1 / 60);
assert(!scene.stale && close(leaf.getWorldTransform().get(13), 1), "Loop updated the scene");
assert(scene.detachLoop().attached === false, "detach");
leaf.setPosition(0, 0, 0); __runNativeSystems(POST_UPDATE, 1 / 60, 1 / 60);
assert(scene.stale, "detached scene no longer updates");
// Handles: wrappers are independent; dispose is idempotent.
const again = leaf.getParent(); assert(again !== null && again.childCount === 2, "parent handle");
again.dispose(); again.dispose(); throws(() => again.childCount, TypeError, "disposed handle");
throws(() => Scene3D.Node.prototype.dispose.call({}), TypeError, "foreign receiver");
// Nodes outlive their scene; scenes keep graphs alive for the Loop.
const survivor = new Scene3D.Scene(), kept = new Scene3D.Node(), survivorRoot = survivor.root;
survivorRoot.add(kept); survivorRoot.dispose();
survivor.attachLoop(); survivor.dispose(); survivor.dispose();
assert(kept.getParent() === null, "disposing the scene detaches it from the Loop and frees the graph");
const orphan = new Scene3D.Scene(); orphan.root.add(new Scene3D.Node()); orphan.attachLoop();
// orphan stays attached: runtime cleanup must detach it before teardown.
for (let i = 0; i < 64; i++) { const n = new Scene3D.Node(); root.add(n); if (i & 1) n.detach(); }
scene.update(); lights.dispose(); camera.dispose();
console.log("Scene3D tests passed (" + checks + " checks)");
