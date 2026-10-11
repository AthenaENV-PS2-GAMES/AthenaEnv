import {Matrix4} from "Matrix4";
import * as Scene3D from "Scene3D";
import * as Animation3D from "Animation3D";
import * as GLTF3D from "GLTF3D";
import * as Camera3D from "Camera3D";
import * as Model3D from "Model3D";
let checks = 0;
function assert(ok, label) { checks++; if (!ok) throw new Error(label); }
function throws(fn, type, label) {
    let error = null; try { fn(); } catch (e) { error = e; }
    assert(error !== null && (!type || error instanceof type), label);
}
function close(a, b) { return Math.abs(a - b) < 1e-3; }
const local = (node, i) => node.getLocalTransform(new Matrix4()).get(i);

const arm = GLTF3D.load("models/arm.glb");
assert(arm.nodes.length === 3 && arm.names.join() === "base,arm,lamp", "nodes and names");
assert(arm.root.childCount === 2 && arm.nodes[0].childCount === 1, "hierarchy");
assert(close(local(arm.nodes[1], 13), 1.5) && close(local(arm.nodes[2], 12), 3), "TRS and matrix nodes");
assert(Object.keys(arm.clips).join() === "wave,bob" && close(arm.clips.wave.duration, 2), "clips by name");

// Plays on the loaded nodes, also inside a scene.
const scene = new Scene3D.Scene(), root = scene.root;
root.add(arm.root); root.dispose();
const wave = new Animation3D.Player(arm.clips.wave, arm.nodes);
wave.play(); wave.advance(1.25);
assert(close(local(arm.nodes[0], 12), 1), "STEP translation");
scene.update();
assert(scene.stale === false, "scene updates the animated nodes");
wave.dispose();

// Handles outlive each other.
const bob = arm.clips.bob;
arm.root.dispose(); for (const n of arm.nodes) n.dispose();
assert(close(bob.duration, 1), "clip handle independent of the nodes");
bob.dispose(); arm.clips.wave.dispose();

throws(() => GLTF3D.load("models/missing.glb"), TypeError, "missing file");
throws(() => GLTF3D.load(1), TypeError, "path string");
throws(() => GLTF3D.load("models/arm.glb", {shading: 7}), RangeError, "material validated");
const lit = GLTF3D.load("models/arm.glb", {shading: 1, baseColor: new Float32Array([1, .5, .2, 1])});
assert(lit.nodes.length === 3, "material override");
lit.root.dispose(); for (const n of lit.nodes) n.dispose(); for (const k in lit.clips) lit.clips[k].dispose();
// Skinned column: drawn through the scene with its vertices deformed in C.
const column = GLTF3D.load("models/bend.glb");
const skinScene = new Scene3D.Scene(), skinRoot = skinScene.root;
skinRoot.add(column.root); skinRoot.dispose(); skinScene.update();
const bend = new Animation3D.Player(column.clips.bend, column.nodes);
bend.time = 1; skinScene.update();
const drawn = skinScene.draw(new Camera3D.Camera(), 0);
assert(drawn.queuedObjects === 1 && drawn.triangles === 18, "skinned mesh drawn");
bend.dispose(); skinScene.dispose(); column.root.dispose(); for (const n of column.nodes) n.dispose(); column.clips.bend.dispose();
// Morph targets: weights animated by the clip, set and read from JS.
const morph = GLTF3D.load("models/morph.glb", {shading: Model3D.DIFFUSE});
const box = morph.nodes[0];
assert(box.getWeights().join() === "0,0", "default mesh weights");
const morphPlayer = new Animation3D.Player(morph.clips.morph, morph.nodes);
morphPlayer.time = 1;
assert(box.getWeights().join() === "1,0", "weights channel applied");
const morphScene = new Scene3D.Scene(), morphRoot = morphScene.root;
morphRoot.add(morph.root); morphRoot.dispose(); morphScene.update();
assert(close(box.getWorldBounds().max[1], 2), "bounds grow with the weights");
box.setWeights(new Float32Array([0, .5])); morphScene.update();
assert(box.getWeights().join() === "0,0.5" && close(box.getWorldBounds().max[1], 1), "setWeights");
assert(morphScene.draw(new Camera3D.Camera(), 0).queuedObjects === 1, "morphed mesh drawn");
throws(() => box.setWeights([0, 0, 0, 0, 0, 0, 0, 0, 0]), RangeError, "at most 8 weights");
throws(() => box.setWeights([NaN]), RangeError, "finite weights");
const weightsClip = new Animation3D.Clip([{path: "weights", times: new Float32Array([0, 1]),
    values: new Float32Array([0, 0, 1, 1])}]);
const weightsPlayer = new Animation3D.Player(weightsClip, [box]);
weightsPlayer.time = .5;
assert(box.getWeights().join() === "0.5,0.5", "JS weights track, 2 per key");
throws(() => new Animation3D.Clip([{path: "weights", times: new Float32Array([0, 1]),
    values: new Float32Array([0, 1, 2])}]), RangeError, "weights per key must divide");
weightsPlayer.dispose(); weightsClip.dispose();
morphPlayer.dispose(); morphScene.dispose(); morph.root.dispose(); box.dispose(); morph.clips.morph.dispose();
const plain = GLTF3D.load("models/lit_cube.glb");
assert(plain.nodes.length === 1 && Object.keys(plain.clips).length === 0, "single mesh file");
plain.root.dispose(); plain.nodes[0].dispose();
scene.dispose();
std.gc();
console.log("glTF3D tests passed (" + checks + " checks)");
