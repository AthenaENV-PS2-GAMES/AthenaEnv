/*
 * Voxel module: blocks, materials, generation, meshing (naive with AO and
 * greedy), dirty tracking, raycast, collision, regions and drawing. Runs on
 * the host and on PCSX2/PS2 (where it prints the costs).
 */
import * as Voxel from "Voxel";
import * as Render3D from "Render3D";
import * as Model3D from "Model3D";
import { Camera } from "Camera3D";

// Render3D needs a depth buffer on the console (the host stubs do not).
if (globalThis.Screen && typeof globalThis.__nativeDraws !== "function") {
    const mode = Screen.getMode(); mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
}
let passed = 0, failed = 0;
function check(name, condition) { if (condition) passed++; else { failed++; console.log("[FAIL] " + name); } }
function throws(name, callback, type) {
    let error = null; try { callback(); } catch (e) { error = e; }
    check(name + " throws" + (type ? " " + type.name : ""), error !== null && (!type || error instanceof type));
}
const host = typeof globalThis.__nativeDraws === "function";
const close = (a, b) => Math.abs(a - b) < 1e-3;
const log = s => { if (!host) console.log("[Voxel] " + s); };
const time = f => { const t = Date.now(); const r = f(); return [Date.now() - t, r]; };

throws("size", () => new Voxel.World({ size: [0, 1, 1] }), RangeError);
throws("chunk", () => new Voxel.World({ size: [8, 8, 8], chunk: 3 }), RangeError);
const w = new Voxel.World({ size: [32, 16, 32], chunk: 16 });
check("sizes", w.sizeX === 32 && w.sizeY === 16 && w.sizeZ === 32 && w.chunkSize === 16);
check("starts empty", w.get(0, 0, 0) === Voxel.AIR && w.dirtyCount === 0);
check("set", w.set(1, 2, 3, 5) === true && w.get(1, 2, 3) === 5 && w.set(1, 2, 3, 5) === false);
check("outside reads air", w.get(-1, 0, 0) === 0 && w.get(0, 99, 0) === 0);
throws("set outside", () => w.set(32, 0, 0, 1), RangeError);
throws("type range", () => w.set(0, 0, 0, 256), RangeError);
check("one chunk dirty", w.dirtyCount === 1);
w.set(15, 0, 0, 1);
check("border block marks the neighbour", w.dirtyCount === 2);
check("fill", w.fill(0, 0, 0, 31, 3, 31, 3) === 32 * 4 * 32 && w.fill(0, 0, 0, 1, 0, 0, 3) === 0);
check("fill clamps", w.fill(-5, 15, -5, 100, 100, 100, 1) === 32 * 32);
w.fill(0, 15, 0, 31, 15, 31, 0);

// Regions round-trip.
const region = w.read(0, 0, 0, 4, 5, 4);
check("read layout", region.length === 80 && region[0] === 3 && region[4 * 4 * 4] === 0);
const copy = new Voxel.World({ size: [4, 5, 4], chunk: 4 });
copy.write(0, 0, 0, 4, 5, 4, region);
check("write", copy.get(1, 2, 3) === w.get(1, 2, 3) && copy.dirtyCount > 0);
throws("read outside", () => w.read(30, 0, 0, 4, 1, 1), RangeError);
copy.dispose();

// Meshing a flat floor: only the top layer's top faces and the outer sides show.
w.setMaterial(3, { color: { top: [0.2, 0.8, 0.2], side: [0.5, 0.4, 0.3], bottom: [0.3, 0.3, 0.3] } });
throws("material color range", () => w.setMaterial(3, { color: [2, 0, 0] }), RangeError);
throws("air material", () => w.setMaterial(0, {}), RangeError);
let n = w.rebuild(0);
check("rebuild all", n === w.stats().chunks && w.dirtyCount === 0);
let s = w.stats();
const floorFaces = 32 * 32 /* top */ + 32 * 32 /* bottom of y=0 against the outside */ + 4 * 32 * 4 /* sides */;
check("naive face count of a slab", s.faces === floorFaces);
w.setStyle({ meshing: "greedy" });
w.rebuild(0);
check("greedy merges faces", w.stats().faces < 40 && w.stats().faces >= 6);
throws("greedy with an atlas", () => w.setStyle({ meshing: "greedy", atlas: Model3D.Texture.fromPixels({ width: 16, height: 16, pixels: new Uint32Array(256) }), tileSize: 8 }), RangeError);
w.setStyle({ meshing: "naive", ambientOcclusion: 0.8 });
w.rebuild(0);

// Raycast: down onto the floor, and from inside.
const down = { x: 5.5, y: 10, z: 5.5, dx: 0, dy: -1, dz: 0 };
let hit = w.raycast(down, 20);
check("raycast hits the top face", hit && hit.x === 5 && hit.y === 3 && hit.z === 5 && hit.ny === 1 && close(hit.distance, 6) && close(hit.py, 4));
check("raycast range", w.raycast(down, 5) === null);
const out = {};
check("raycast out reuse", w.raycast({ x: 0.5, y: 2, z: 0.5, dx: 0, dy: 1, dz: 0 }, 1, out) === out && out.distance === 0 && out.ny === 0);
check("raycast misses through air", w.raycast({ x: 5, y: 10, z: 5, dx: 0, dy: 1, dz: 0 }, 50) === null);
throws("zero direction", () => w.raycast({ x: 0, y: 0, z: 0, dx: 0, dy: 0, dz: 0 }), RangeError);
// Diagonal ray to a wall.
w.fill(10, 4, 0, 10, 8, 31, 3);
hit = w.raycast({ x: 2.5, y: 6.5, z: 2.5, dx: 1, dy: 0, dz: 0.2 }, 30);
check("diagonal hits the wall's -X face", hit && hit.x === 10 && hit.nx === -1);

// Collision: falling onto the floor and walking into the wall.
const m = w.moveBox(4.2, 6, 4.2, 4.8, 7.8, 4.8, 0, -5, 0);
check("lands on the floor", m.onGround && m.hitY && close(m.dy, -(6 - 4) + 1e-3));
const wall = w.moveBox(8.2, 4.001, 4.2, 8.8, 5.8, 4.8, 3, 0, 0);
check("stops at the wall", wall.hitX && close(wall.dx, 10 - 8.8 - 1e-3));
const slide = w.moveBox(8.2, 4.001, 4.2, 8.8, 5.8, 4.8, 3, 0, 1);
check("slides along it", slide.hitX && close(slide.dz, 1));
check("boxSolid", w.boxSolid(9.5, 4, 1, 10.5, 5, 2) && !w.boxSolid(5, 5, 5, 6, 6, 6));
throws("moveBox validates", () => w.moveBox(1, 0, 0, 0, 1, 1, 0, 0, 0), RangeError);

// Drawing: frustum and distance culling, stats like Render3D.
w.rebuild(0);
const camera = new Camera({ near: 0.1, far: 200, aspect: 1 });
camera.setPosition(16, 30, 60).lookAt(16, 0, 16);
let ds = w.draw(camera);
check("draws meshed chunks", ds.drawPasses > 0 && ds.sourceTriangles > 0);
check("null stats", w.draw(camera, Render3D.CULL_BACK, undefined, null) === undefined);
const near = w.draw(camera, Render3D.CULL_BACK, undefined, {}, 20);
check("distance culls far chunks", near.drawPasses < ds.drawPasses);
camera.lookAt(16, 30, 200);
ds = w.draw(camera);
check("frustum culls chunks behind", ds.drawPasses === 0 && ds.culledObjects > 0);
w.dispose();
throws("disposed", () => w.get(0, 0, 0), TypeError);

// Generation and the costs (printed on the console).
const big = new Voxel.World({ size: [64, 48, 64], chunk: 16 });
for (const [t, c] of [[1, [0.3, 0.7, 0.2]], [2, [0.45, 0.33, 0.2]], [3, [0.5, 0.5, 0.5]]]) big.setMaterial(t, { color: c });
let [ms] = time(() => big.generate({ seed: 7 }));
log(`generate 64x48x64 hills: ${ms} ms`);
check("surface in range", big.surface(10, 10) > 5 && big.surface(10, 10) < 48);
const top = big.surface(10, 10);
check("layers", big.get(10, top - 1, 10) === 1 && big.get(10, top - 2, 10) === 2 && big.get(10, 1, 10) === 3);
[ms] = time(() => big.generate({ seed: 7, caves: 0.5 }));
log(`generate with caves: ${ms} ms`);
let air = 0; for (let y = 1; y < 15; y++) for (let x = 0; x < 64; x += 2) if (big.get(x, y, 20) === 0) air++;
check("caves carve air underground", air > 0);
const [rebuildMs, chunks] = time(() => big.rebuild(0));
s = big.stats();
log(`naive+AO mesh of ${chunks} chunks (${s.meshedChunks} with faces): ${rebuildMs} ms = faces ${s.lastMeshMs.toFixed(0)} + meshes ${s.lastBuildMs.toFixed(0)} ms; ${s.faces} faces, ${s.meshes} meshes, ~${(s.meshBytes / 1024) | 0} KB`);
big.setStyle({ meshing: "greedy" });
const [greedyMs] = time(() => big.rebuild(0));
log(`greedy mesh: ${greedyMs} ms = faces ${big.stats().lastMeshMs.toFixed(0)} + meshes ${big.stats().lastBuildMs.toFixed(0)} ms, ${big.stats().faces} faces`);
check("greedy fewer faces", big.stats().faces < s.faces);
big.setStyle({});
big.rebuild(0);
// Editing one block rebuilds its chunk only, within the budget.
const x = 20, z = 20, y = big.surface(x, z) - 1;
big.set(x, y, z, 0);
const [editMs, edited] = time(() => big.rebuild(4, x, y, z));
log(`edit + rebuild: ${edited} chunk(s) in ${editMs} ms`);
check("edit rebuilds its chunk", edited >= 1 && big.dirtyCount === 0);
const cam2 = new Camera({ near: 0.1, far: 200, aspect: 640 / 448 });
cam2.setPosition(32, 40, 90).lookAt(32, 10, 32);
if (!host) {
    const t0 = Date.now(); let frames = 0, stats = {};
    for (; frames < 20; frames++) big.draw(cam2, Render3D.CULL_BACK, undefined, stats);
    log(`draw overview: ${((Date.now() - t0) / frames).toFixed(1)} ms per draw call (CPU side), ${stats.sourceTriangles} triangles, ${stats.drawPasses} meshes`);
}
cam2.dispose(); camera.dispose(); big.dispose();

console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("Voxel module test passed");
