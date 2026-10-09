/*
 * MeshBuilder module: shapes, state (color, transform, UV rect), winding,
 * merge, heightmap, splitting into several meshes and errors. Runs on the
 * host and on PCSX2/PS2 (where it also prints the cost of building).
 */
import * as MeshBuilder from "MeshBuilder";
import * as Model3D from "Model3D";
import * as Render3D from "Render3D";
import { Camera } from "Camera3D";
import { Matrix4 } from "Matrix4";

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

const b = new MeshBuilder.Builder();
check("empty", b.vertexCount === 0 && b.triangleCount === 0 && b.partCount === 0 && b.build().length === 0);
b.box(-1, -1, -1, 1, 1, 1);
check("box: 24 vertices, 12 triangles", b.vertexCount === 24 && b.triangleCount === 12);
b.sphere(0, 0, 0, 1, 8, 4);
check("sphere counts", b.vertexCount === 24 + 9 * 5 && b.triangleCount === 12 + 8 * 4 * 2);
b.clear().cylinder(0, 0, 0, 1, 2, 6);
check("cylinder with caps", b.vertexCount === 14 + 2 * 8 && b.triangleCount === 12 + 12);
b.clear().plane(0, 0, 0, 4, 4, 2, 3);
check("plane divisions", b.vertexCount === 3 * 4 && b.triangleCount === 2 * 3 * 2);
b.clear();
const a0 = b.vertex(0, 0, 0), a1 = b.vertex(1, 0, 0), a2 = b.vertex(0, 1, 0);
check("vertex returns indices", a0 === 0 && a1 === 1 && a2 === 2);
b.triangle(a0, a1, a2);
throws("triangle index range", () => b.triangle(0, 1, 9), RangeError);
throws("box min > max", () => b.box(1, 0, 0, 0, 1, 1), RangeError);
throws("color range", () => b.color(2, 0, 0), RangeError);
throws("sphere segments", () => b.sphere(0, 0, 0, 1, 2), RangeError);
throws("singular transform", () => b.transform(new Matrix4().set(0, 0)), RangeError);
throws("uvTile index", () => b.uvTile(16, 4, 4), RangeError);

// Winding: a box seen from outside keeps every front face with CULL_BACK.
const camera = new Camera({ near: 0.1, far: 100, aspect: 1 });
camera.setPosition(3, 2.5, 4).lookAt(0, 0, 0);
const box = new MeshBuilder.Builder().box(-1, -1, -1, 1, 1, 1).build()[0];
const inst = box.createInstance();
const back = Render3D.draw(inst, camera, Render3D.CULL_BACK), none = Render3D.draw(inst, camera, Render3D.CULL_NONE);
check("box drawn", none.sourceTriangles === 12);
if (host) check("winding: front faces are counter-clockwise (stats agree with CULL_NONE)", back.sourceTriangles === 12);
inst.dispose(); box.dispose();

// Transform and color state.
const t = new MeshBuilder.Builder().color(1, 0, 0).transform(new Matrix4().set(12, 10)).box(0, 0, 0, 1, 1, 1);
const tm = t.build({ shading: Model3D.DIFFUSE })[0];
const ti = tm.createInstance();
check("transformed geometry", tm instanceof Model3D.Mesh);
ti.dispose(); tm.dispose(); t.dispose();

// merge: static batching of a mesh and of an instance (its transform applies).
const rock = new MeshBuilder.Builder().box(0, 0, 0, 1, 1, 1).build()[0];
const rockInst = rock.createInstance(); rockInst.setPosition(5, 0, 0);
const batch = new MeshBuilder.Builder();
for (let i = 0; i < 10; i++) batch.transform(new Matrix4().set(12, i * 2)).merge(rock);
batch.transform(null).merge(rockInst);
check("merge copies triangles (the welded box has 8 corners)", batch.triangleCount === 11 * 12 && batch.vertexCount === 11 * 8);
const merged = batch.build();
check("one mesh", merged.length === 1);
merged[0].dispose(); rockInst.dispose(); rock.dispose(); batch.dispose();

// heightmap with height colors.
const W = 33, D = 33, heights = new Float32Array(W * D);
for (let z = 0; z < D; z++) for (let x = 0; x < W; x++) heights[z * W + x] = Math.sin(x / 5) * Math.cos(z / 7);
const terrain = new MeshBuilder.Builder().heightmap(heights, W, D, { cell: 0.5, scale: 2, low: [0, 0.3, 0], high: [1, 1, 1] });
check("heightmap grid", terrain.vertexCount === W * D && terrain.triangleCount === (W - 1) * (D - 1) * 2);
throws("heightmap size", () => terrain.heightmap(heights, W, D + 1), RangeError);
throws("heightmap low without high", () => terrain.heightmap(heights, W, D, { low: [0, 0, 0] }), TypeError);
const tmesh = terrain.build({ shading: Model3D.DIFFUSE });
check("heightmap builds lit", tmesh.length === 1);
tmesh[0].dispose(); terrain.dispose();

// Splitting: 48000 triangles need three meshes (21844 triangles each).
const big = new MeshBuilder.Builder();
for (let i = 0; i < 4000; i++) big.box(i, 0, 0, i + 0.5, 0.5, 0.5);
const t0 = Date.now();
const parts = big.build();
const buildMs = Date.now() - t0;
check("split into several meshes", big.triangleCount === 48000 && parts.length === big.partCount && parts.length === 3);
if (!host) console.log(`[MeshBuilder] 4000 boxes (48000 triangles): build ${buildMs} ms into ${parts.length} meshes`);
for (const m of parts) m.dispose();
big.dispose();

// Textured: UVs from uvTile.
const pixels = new Uint32Array(16 * 16).fill(0xff00ff00);
const atlas = Model3D.Texture.fromPixels({ width: 16, height: 16, pixels });
const tex = new MeshBuilder.Builder().uvTile(5, 4, 4, 0.05).box(0, 0, 0, 1, 1, 1).build({ texture: atlas });
check("textured build", tex.length === 1);
tex[0].dispose(); atlas.dispose();

b.dispose();
throws("disposed", () => b.box(0, 0, 0, 1, 1, 1), TypeError);
camera.dispose();
console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("MeshBuilder module test passed");
