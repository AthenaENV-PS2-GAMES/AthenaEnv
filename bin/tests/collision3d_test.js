import {Matrix4} from "Matrix4";
import * as Scene3D from "Scene3D";
import * as Model3D from "Model3D";
import * as Animation3D from "Animation3D";
import * as GLTF3D from "GLTF3D";
import * as Collision3D from "Collision3D";
let checks = 0;
function assert(ok, label) { checks++; if (!ok) throw new Error(label); }
function throws(fn, type, label) {
    let error = null; try { fn(); } catch (e) { error = e; }
    assert(error !== null && (!type || error instanceof type), label);
}
function near(a, b, e = 1e-3) { return Math.abs(a - b) < e; }
const world = new Collision3D.World();
const floor = world.addBox([-20, -1, -20], [20, 0, 20]);
assert(floor > 0 && world.triangleCount === 12, "box shape");
// Rays: null on a miss, a reusable hit object.
const hit = world.raycast([1, 5, 2], [0, -1, 0], 10);
assert(hit && near(hit.distance, 5) && near(hit.ny, 1) && near(hit.x, 1) && hit.shape === floor, "raycast");
assert(world.raycast([1, 5, 2], [0, -1, 0], 4) === null, "too short");
assert(world.raycast([1, 5, 2], [0, -1, 0], 10, {mask: 2}) === null, "masked");
const out = {};
assert(world.raycast([0, 3, 0], [0, -2, 0], 10, undefined, out) === out && near(out.distance, 3), "out object");
const rays = new Float32Array(8);
assert(world.raycastMany(new Float32Array([0, 5, 0, 100, 5, 0]), new Float32Array([0, -1, 0]), 10, rays) === 1 &&
    near(rays[0], 5) && near(rays[2], 1) && rays[4] === -1, "raycastMany");
throws(() => world.raycastMany(new Float32Array(3), new Float32Array(6), 1, rays), RangeError, "directions 3 or 3n");
const sphere = world.sphereCast([0, 3, 0], .5, [0, -1, 0], 10);
assert(near(sphere.distance, 2.5) && near(sphere.ny, 1), "sphere cast");
assert(world.overlapSphere([0, .3, 0], .5).join() === String(floor), "overlap");
assert(world.setLayer(floor, 4) && world.overlapSphere([0, .3, 0], .5, {mask: 1}).length === 0, "layers");
world.setLayer(floor, 1);
throws(() => world.raycast([0, 0, 0], [0, 0, 0], 1), RangeError, "zero direction");
throws(() => world.addBox([1, 1, 1], [0, 0, 0]), RangeError, "inverted box");
throws(() => world.addTriangles(new Float32Array(8)), RangeError, "9 floats per triangle");
const tri = world.addTriangles(new Float32Array([30, 0, 0, 31, 0, 1, 31, 0, 0]), {layer: 8});
assert(world.raycast([30.8, 1, .2], [0, -1, 0], 2, {mask: 8}).shape === tri, "triangles with a layer");
assert(world.remove(tri) && !world.remove(tri), "remove once");
// Characters: fall, land, walk into a wall, jump.
const player = new Collision3D.Character(world, {radius: .4, height: 1.8, position: [0, 2, 0]});
for (let i = 0; i < 120; i++) player.step(1 / 60);
assert(player.onGround && near(player.y, 0, .02) && near(player.vy, 0), "lands");
assert(near(player.groundNormal[1], 1), "ground normal");
const wall = world.addBox([3, 0, -20], [4, 3, 20]);
for (let i = 0; i < 90; i++) { player.vx = 3; player.step(1 / 60); }
assert(player.hitWall && near(player.x, 2.6, .02), "blocked by the wall");
world.remove(wall);
player.vx = 0; player.vy = 4; player.step(1 / 60);
assert(!player.onGround && player.y > 0, "jumps");
for (let i = 0; i < 120; i++) player.step(1 / 60);
assert(player.onGround, "lands again");
// move() collides without gravity; bind() drives a node.
const node = new Scene3D.Node();
const nodeX = () => node.getLocalTransform(new Matrix4()).get(12);
player.bind(node).move(-1, -1, 0);
assert(near(nodeX(), player.x) && near(player.y, 0, .02), "bound node follows");
const before = player.x;
player.enabled = false;
player.vx = 5;
Collision3D.step(1);
assert(near(player.x, before) && near(nodeX(), before), "disabled characters do not step");
player.vx = 0;
throws(() => new Collision3D.Character(world, {radius: 0}), RangeError, "invalid radius");
throws(() => new Collision3D.Character({}), TypeError, "needs a world");
// Levels from Scene3D/glTF nodes, in their composed transforms.
const level = new Collision3D.World();
const cube = GLTF3D.load("models/arm.glb");
const holder = new Scene3D.Node().setPosition(0, -10, 0);
holder.add(cube.root);
const shape = level.addNode(holder, {layer: 2});
assert(shape > 0 && level.triangleCount > 12, "glTF subtree");
// Beside the arm (|x| > .5): the base cube's top, at y = -10 + 1.
const down = level.raycast([.75, 0, .75], [0, -1, 0], 20, {mask: 2});
assert(down && near(down.y, -9), "composed transform: base cube top");
assert(near(level.raycast([0, 0, .25], [0, -1, 0], 20).y, -8), "the arm on top: -10 + 1.5 + .5");
const mesh = Model3D.load("models/lit_cube.glb");
assert(level.addMesh(mesh) > 0, "mesh");
// Characters keep their world alive; the Loop system can be attached once.
world.dispose(); throws(() => world.raycast([0, 0, 0], [0, 1, 0], 1), TypeError, "disposed world");
player.enabled = true; player.step(1 / 60);
Collision3D.attachLoop(); Collision3D.attachLoop();
assert(Collision3D.isAttached() && Collision3D.detachLoop() && !Collision3D.isAttached(), "loop system");
player.dispose(); node.dispose(); holder.dispose(); cube.root.dispose();
for (const n of cube.nodes) n.dispose();
for (const k in cube.clips) cube.clips[k].dispose();
level.dispose();
// A character dropped without dispose() stays alive (kept) until cleanup.
new Collision3D.Character(new Collision3D.World());
std.gc();
console.log("Collision3D tests passed (" + checks + " checks)");
