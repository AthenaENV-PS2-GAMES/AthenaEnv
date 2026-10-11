import {Matrix4} from "Matrix4";
import {Camera} from "Camera3D";
import * as Model3D from "Model3D";
import * as Scene3D from "Scene3D";
import * as Lights from "Lights";
import * as Render3D from "Render3D";
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
let root = scene.root;
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
// Accessors reuse constructor/subclass wrappers and preserve JS properties.
assert(scene.root === root && scene.root === scene.root, "root wrapper identity");
assert(parent.getParent() === root && child.getParent() === parent, "parent constructor identity");
assert(root.getChild(0) === parent && parent.getChild(0) === child && child.getChild(0) === leaf, "child identity");
child.label = "retained metadata";
assert(parent.getChild(0).label === child.label, "metadata survives accessors");
const labels = new Map([[child,"child"]]);
assert(labels.get(parent.getChild(0)) === "child", "node identity as Map key");
class SpecialNode extends Scene3D.Node { marker() { return 42; } }
const special = new SpecialNode(); parent.add(special);
assert(parent.getChild(parent.childCount-1) === special && parent.getChild(parent.childCount-1).marker() === 42, "subclass wrapper reused");
special.detach();special.dispose();
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
assert(s.drawPasses === 2 && s.pipelinePasses === 1, "meshes of one pipeline share a pass");
const drawOut = {};
assert(scene.draw(camera, 0, lights, drawOut) === drawOut && drawOut.queuedObjects === 2 && drawOut.pipelinePasses === 1 &&
    drawOut.geometryBytes === s.geometryBytes, "draw stats reuse an object");
throws(() => scene.draw(camera, 0, lights, "x"), TypeError, "draw stats must be an object");
Render3D.frameStats();
assert(scene.draw(camera, 0, lights, null) === undefined, "null draw stats return undefined");
assert(Render3D.frameStats().triangles === 2, "scene draws feed Render3D.frameStats");
assert(scene.update(null) === undefined, "null update stats return undefined");
const updateOut = {};
assert(scene.update(updateOut) === updateOut && updateOut.worldUpdates === 0 && updateOut.visitedNodes === 0,
    "update stats reuse an object");
throws(() => scene.update(1), TypeError, "update stats must be an object");
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
// A live node has one JS wrapper: aliases share explicit disposal.
const again = leaf.getParent(); assert(again === root && again.childCount === 2, "parent identity");
again.dispose(); again.dispose(); throws(() => again.childCount, TypeError, "disposed handle");
throws(() => root.childCount, TypeError, "dispose invalidates aliases");
root = scene.root;
assert(root !== again && leaf.getParent() === root, "disposed cache entry replaced by live wrapper");
throws(() => Scene3D.Node.prototype.dispose.call({}), TypeError, "foreign receiver");
// Nodes outlive their scene; scenes keep graphs alive for the Loop.
const survivor = new Scene3D.Scene(), kept = new Scene3D.Node(), survivorRoot = survivor.root;
survivorRoot.add(kept); survivorRoot.dispose();
survivor.attachLoop(); survivor.dispose(); survivor.dispose();
assert(kept.getParent() === null, "disposing the scene detaches it from the Loop and frees the graph");
const orphan = new Scene3D.Scene(); orphan.root.add(new Scene3D.Node()); orphan.attachLoop();
// orphan stays attached: runtime cleanup must detach it before teardown.
// Bulk setters match the per-node setters, mark nodes dirty and validate first.
const bulkNodes = [new Scene3D.Node(), new Scene3D.Node()], singleNodes = [new Scene3D.Node(), new Scene3D.Node()];
for (const n of bulkNodes) root.add(n);
assert(!scene.stale || scene.update(), "settled");
const xyz = new Float32Array([1, 2, 3, -4, .5, 6]), euler = new Float32Array([.1, .2, .3, -.4, .5, -.6]);
assert(Scene3D.setPositions(bulkNodes, xyz) === 2 && Scene3D.setRotationsEuler(bulkNodes, euler) === 2, "bulk count");
assert(scene.stale, "bulk setters mark the scene dirty");
for (let i = 0; i < 2; i++) {
    singleNodes[i].setPosition(xyz[i*3], xyz[i*3+1], xyz[i*3+2]).setRotationEuler(euler[i*3], euler[i*3+1], euler[i*3+2]);
    const a = bulkNodes[i].getLocalTransform(), b = singleNodes[i].getLocalTransform();
    let same = true; for (let k = 0; k < 16; k++) same = same && a.get(k) === b.get(k);
    assert(same, "bulk node " + i + " equals per-node setters");
}
throws(() => Scene3D.setRotationsEuler(bulkNodes, new Float32Array([0, 0, 0, Infinity, 0, 0])), RangeError, "non-finite");
throws(() => Scene3D.setPositions(bulkNodes, new Float32Array(3)), RangeError, "short values");
throws(() => Scene3D.setPositions([bulkNodes[0], 1], xyz), TypeError, "nodes only");
assert(bulkNodes[0].getLocalTransform().get(12) === 1, "failed bulk calls change nothing");
// 2D physics layout: x, y, angle about Z; z kept.
bulkNodes[1].setPosition(0, 0, -7);
assert(Scene3D.setTransforms2D(bulkNodes, new Float32Array([3, 4, 0, 5, 6, Math.PI / 2])) === 2, "2D transforms");
const t2d = bulkNodes[1].getLocalTransform();
assert(close(t2d.get(12), 5) && close(t2d.get(13), 6) && close(t2d.get(14), -7), "x, y set and z kept");
assert(close(t2d.get(0), 0) && close(t2d.get(1), 1), "angle turns about Z");
throws(() => Scene3D.setTransforms2D(bulkNodes, new Float32Array([0, 0, NaN, 0, 0, 0])), RangeError, "finite angle");
scene.update();
// Native motion: configured once, integrated by advance() without JS per frame.
const mover = new Scene3D.Node().setPosition(1, 0, 0).setVelocity(2, 0, 0).setSpin(0, Math.PI, 0);
root.add(mover); scene.update();
assert(scene.advance(.5) === 1 && scene.stale, "advance moves the node and marks the scene dirty");
scene.update();
let local = mover.getLocalTransform();
assert(close(local.get(12), 2) && close(local.get(0), 0) && close(local.get(8), 1) && close(local.get(2), -1), "velocity and local spin");
assert(scene.advance(0) === 0, "zero dt moves nothing");
throws(() => scene.advance(-1), RangeError, "negative dt");
throws(() => scene.advance("x"), TypeError, "dt is a number");
throws(() => mover.setSpin(NaN, 0, 0), RangeError, "finite spin");
mover.setVelocity(0, 0, 0).setSpin(0, 0, 0);
assert(scene.advance(1) === 0, "zero motion stops");
mover.dispose();
for (const n of bulkNodes) n.dispose();
for (const n of singleNodes) n.dispose();
for (let i = 0; i < 64; i++) { const n = new Scene3D.Node(); root.add(n); if (i & 1) n.detach(); }
scene.update(); lights.dispose(); camera.dispose();
// Cyclic unreachable wrappers are collected even while native parents retain
// their nodes. Disposal/finalization must not remove a newer replacement.
{
    const s = new Scene3D.Scene();
    let r = s.root;
    for(let i=0;i<256;i++) {
        let n = new Scene3D.Node().setPosition(i,0,0);
        n.self = n; r.add(n); n = null;
        if(i%8===0) std.gc();
    }
    std.gc();
    for(let i=0;i<256;i++) {
        const n = r.getChild(i);
        assert(n.self === undefined && n.getLocalTransform().get(12) === i, "weak wrapper cache permits cyclic GC "+i);
        assert(r.getChild(i) === n, "replacement identity "+i);
        n.dispose();
    }
    const oldRoot = r; r.dispose(); r = s.root;
    assert(r !== oldRoot && s.root === r, "root wrapper replaced after disposal");
    // Repeated old-root dispose is harmless to its replacement cache entry.
    oldRoot.dispose(); assert(s.root === r, "old disposed alias cannot remove replacement");
    let expired=r; expired.self=expired; expired.dispose(); r=s.root; expired=null;
    std.gc(); assert(s.root === r, "old wrapper finalization cannot remove replacement");
    s.dispose();r.dispose();std.gc();
}
// Constructor prototype getters can dispose a mesh; no borrowed native
// pointer may survive that callback.
{
    const m = Model3D.Mesh.fromGeometry({positions:new Float32Array([0,0,0,1,0,0,0,1,0])});
    const target = new Proxy(function(){}, {get(t,key){
        if(key === "prototype") { m.dispose();std.gc();return Scene3D.Node.prototype; }
        return t[key];
    }});
    throws(()=>Reflect.construct(Scene3D.Node,[m],target),TypeError,"prototype getter disposes mesh safely");
}
// Picking: Camera3D projection and Scene3D raycast/queryBox.
{
    const cam = new Camera({fovYDegrees: 90, aspect: 1, near: 1, far: 100});
    cam.setPosition(0, 0, 10).lookAt(0, 0, 0);
    assert(cam.setViewport(200, 200) === cam, "setViewport chains");
    const p = cam.worldToScreen(0, 0, 0);
    assert(close(p.x, 100) && close(p.y, 100) && close(p.depth, 10), "centre projects to the viewport centre");
    const reuse = {k: 1};
    assert(cam.worldToScreen(10, 10, 0, reuse) === reuse && close(reuse.x, 200) && close(reuse.y, 0) && reuse.k === 1,
        "fov 90: x = depth reaches the right edge, +y is up");
    assert(cam.worldToScreen(0, 0, 20) === null, "points behind the camera do not project");
    const ray = cam.screenToRay(100, 100);
    assert(close(ray.x, 0) && close(ray.z, 10) && close(ray.dz, -1) && close(ray.dx, 0), "centre ray looks forward");
    const corner = cam.screenToRay(200, 0, {});
    assert(close(corner.dx * corner.dx + corner.dy * corner.dy + corner.dz * corner.dz, 1) && corner.dx > 0 && corner.dy > 0,
        "rays are unit length; the top-right pixel looks right and up");
    const back = cam.worldToScreen(10 * corner.dx + corner.x, 10 * corner.dy + corner.y, 10 * corner.dz + corner.z);
    assert(close(back.x, 200) && close(back.y, 0), "screenToRay inverts worldToScreen");
    throws(() => cam.setViewport(0, 10), RangeError, "viewport must be positive");
    throws(() => cam.worldToScreen(0, 0), TypeError, "worldToScreen needs xyz");
    throws(() => cam.screenToRay(0, 0, 5), TypeError, "ray out must be an object");

    const quad = Model3D.Mesh.fromGeometry({positions: new Float32Array([-1,-1,0, 1,-1,0, 1,1,0, -1,-1,0, 1,1,0, -1,1,0])});
    const world = new Scene3D.Scene();
    const near = new Scene3D.Node(quad).setPosition(.3, -.4, 2), far = new Scene3D.Node(quad).setPosition(.3, -.4, -2);
    // Offsets keep the rays off the quads' shared diagonal.
    const tilted = new Scene3D.Node(quad).setPosition(5, 0, 0).setRotationEuler(0, Math.PI / 4, 0);
    world.root.add(near).add(far).add(tilted); quad.dispose();
    throws(() => world.raycast(ray), InternalError, "raycast throws while stale");
    world.update();
    let hit = world.raycast(ray);
    assert(hit && hit.node === near && close(hit.distance, 8) && close(hit.z, 2) && close(hit.nz, 1) && hit.triangle >= 0,
        "nearest triangle hit, normal facing the ray");
    near.visible = false; world.update();
    hit = world.raycast(ray, undefined, undefined, reuse);
    assert(hit === reuse && hit.node === far && close(hit.distance, 12), "hidden nodes are skipped, out reused");
    assert(world.raycast(ray, 11) === null, "maxDistance limits hits");
    near.visible = true; world.update();
    const offAxis = {x: .5, y: .4, z: 10, dx: 0, dy: 0, dz: -1};
    assert(world.raycast(offAxis).node === near, "inside the quad, upper triangle");
    const gap = {x: 1.5, y: 0, z: 10, dx: 0, dy: 0, dz: -1};
    assert(world.raycast(gap) === null, "precise rays miss outside the triangles");
    const side = {x: 5, y: .5, z: 10, dx: 0, dy: 0, dz: -2};
    hit = world.raycast(side);
    assert(hit.node === tilted && close(hit.distance, 10) && close(hit.nz, Math.SQRT1_2) && close(hit.nx, Math.SQRT1_2),
        "world transform applies to triangles and normals");
    hit = world.raycast(side, undefined, {precise: false});
    assert(hit.node === tilted && hit.triangle === -1 && hit.distance < 10 && close(hit.nz, 1), "AABB mode");
    throws(() => world.raycast({x: 0, y: 0, z: 0, dx: 0, dy: 0, dz: 0}), RangeError, "zero direction");
    throws(() => world.raycast({x: 0, y: 0}), TypeError, "ray needs all fields");
    throws(() => world.raycast(ray, -1), RangeError, "negative maxDistance");
    throws(() => world.raycast(ray, 1, {precise: 1}), TypeError, "precise must be a boolean");
    let found = world.queryBox(-2, -2, -3, 2, 2, 3);
    assert(found.length === 2 && found[0] === near && found[1] === far, "queryBox in traversal order");
    const list = [tilted, tilted, tilted];
    assert(world.queryBox(4, -1, -1, 6, 1, 1, list) === list && list.length === 1 && list[0] === tilted, "queryBox refills out");
    assert(world.queryBox(50, 50, 50, 60, 60, 60).length === 0, "empty box query");
    throws(() => world.queryBox(1, 0, 0, 0, 1, 1), RangeError, "inverted box");
    throws(() => world.queryBox(0, 0, 0, 1, 1, 1, {}), TypeError, "queryBox out must be an Array");
    const many = new Scene3D.Node(), tri = Model3D.Mesh.fromGeometry({positions: new Float32Array([0,0,0, .1,0,0, 0,.1,0])});
    for (let i = 0; i < 100; i++) many.add(new Scene3D.Node(tri).setPosition(i * .01, 0, 30));
    world.root.add(many); tri.dispose(); world.update();
    assert(world.queryBox(-1, -1, 29, 2, 1, 31).length === 100, "queryBox beyond the stack buffer");
    world.dispose(); cam.dispose();
}
console.log("Scene3D tests passed (" + checks + " checks)");
