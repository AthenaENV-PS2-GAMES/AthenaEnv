/*
 * LOD module: level selection by distance with hysteresis, hidden bands and
 * the draw distance, bias, stats and errors. Runs on the host and on PCSX2/PS2.
 */
import * as LOD from "LOD";
import * as Scene3D from "Scene3D";
import * as Model3D from "Model3D";
import * as Render3D from "Render3D";
import * as Lights from "Lights";
import { Camera } from "Camera3D";

if (globalThis.Screen && typeof globalThis.__nativeDraws !== "function") {
    const mode = Screen.getMode(); mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
}
let passed = 0, failed = 0;
function check(name, condition) { if (condition) passed++; else { failed++; console.log("[FAIL] " + name); } }
function throws(name, callback, type) {
    let error = null; try { callback(); } catch (e) { error = e; }
    check(name + " throws" + (type ? " " + type.name : ""), error !== null && (!type || error instanceof type));
}
const quad = Model3D.Mesh.fromGeometry({ positions: new Float32Array([-1,-1,0, 1,-1,0, 1,1,0, -1,-1,0, 1,1,0, -1,1,0]) });
const tri = Model3D.Mesh.fromGeometry({ positions: new Float32Array([-1,-1,0, 1,-1,0, 0,1,0]) });
const scene = new Scene3D.Scene();
const node = new Scene3D.Node(quad);
scene.root.add(node);
scene.update();
const camera = new Camera({ near: 0.5, far: 500, aspect: 1 });
const at = z => { camera.setPosition(0, 0, z).lookAt(0, 0, 0); };
const drawn = () => { scene.update(); return scene.draw(camera, Render3D.CULL_NONE).sourceTriangles; };

throws("needs levels", () => new LOD.Group(node, []), RangeError);
throws("increasing thresholds", () => new LOD.Group(node, [{ mesh: quad, until: 20 }, { mesh: tri, until: 10 }]), RangeError);
throws("hysteresis range", () => new LOD.Group(node, [{ mesh: quad, until: 10 }], { hysteresis: 0.9 }), RangeError);
throws("node type", () => new LOD.Group({}, [{ mesh: quad, until: 10 }]), TypeError);

const group = new LOD.Group(node, [{ mesh: quad, until: 10 }, { mesh: tri, until: 30 }], { hysteresis: 0.1 });
check("before the first selection", group.level === -2 && group.enabled);
at(5); LOD.update(camera);
check("near: high level", group.level === 0 && drawn() === 2);
at(20); LOD.update(camera);
check("middle: low level", group.level === 1 && drawn() === 1 && Math.abs(group.distance - 20) < 1e-3);
at(10.5); LOD.update(camera);
check("hysteresis keeps the low level just inside the threshold", group.level === 1);
at(8.5); LOD.update(camera);
check("well inside: back to high", group.level === 0);
at(25); LOD.update(camera);
at(31); LOD.update(camera);
check("hysteresis keeps it drawn just past the last threshold", group.level === 1);
at(40); LOD.update(camera);
check("far: hidden", group.level === LOD.HIDDEN && node.visible === false && drawn() === 0);
const stats = LOD.update(camera, {});
check("stats", stats.groups === 1 && stats.hidden === 1 && stats.changes === 0);
at(5); check("update returns changes", LOD.update(camera) === 1 && node.visible === true);

LOD.setBias(2);
at(15); LOD.update(camera);
check("bias doubles the thresholds", group.level === 0);
LOD.setBias(1);
throws("bias must be positive", () => LOD.setBias(0), RangeError);

const lights = new Lights.Set();
LOD.setDrawDistance(12, { lights, color: [0.5, 0.5, 0.5] });
at(15); LOD.update(camera);
check("draw distance hides", group.level === LOD.HIDDEN);
LOD.setDrawDistance(0, { lights });
LOD.update(camera);
check("no limit again", group.level !== LOD.HIDDEN);
throws("fog range", () => LOD.setDrawDistance(10, { lights, fogStart: 5, color: [2, 0, 0] }), RangeError);

// A null mesh hides inside its band.
const n2 = new Scene3D.Node(quad); scene.root.add(n2); scene.update();
const g2 = new LOD.Group(n2, [{ mesh: quad, until: 5 }, { mesh: null, until: 50 }], { hysteresis: 0 });
at(20); LOD.update(camera);
check("null mesh band hides", g2.level === LOD.HIDDEN && n2.visible === false);
at(3); LOD.update(camera);
check("null band returns to near mesh", g2.level === 0 && n2.visible === true);
g2.enabled = false;
check("disabled", g2.enabled === false && g2.level === -2);
g2.dispose();
throws("disposed", () => g2.level, TypeError);

const n3 = new Scene3D.Node(quad); scene.root.add(n3); scene.update();
const g3 = new LOD.Group(n3, [{ mesh: quad, until: 5 }, { mesh: null, until: 10 }, { mesh: tri, until: 40 }]);
at(8); LOD.update(camera);
check("intermediate null band hides", g3.level === LOD.HIDDEN);
at(12); LOD.update(camera);
check("intermediate null band returns to farther mesh", g3.level === 2 && n3.visible);
LOD.setDrawDistance(15); at(30); LOD.update(camera);
LOD.setDrawDistance(0); LOD.update(camera);
check("removing draw limit restores the selected band", g3.level === 2 && n3.visible);
g3.dispose();

// A later level getter disposes an earlier wrapper: captured meshes stay alive.
const temporary = Model3D.Mesh.fromGeometry({ positions: new Float32Array([-1,-1,0, 1,-1,0, 0,1,0]) });
const retained = new LOD.Group(n3, [
    { mesh: temporary, until: 10 },
    { mesh: tri, get until() { temporary.dispose(); return 40; } },
]);
at(3); LOD.update(camera);
check("mesh retained across later getters", retained.level === 0);
scene.update(); check("retained mesh can still draw", scene.draw(camera, Render3D.CULL_NONE).sourceTriangles > 0);
retained.dispose();
const doomed = new Scene3D.Node();
const newTarget = new Proxy(function () {}, { get(target, key) {
    if (key === "prototype") doomed.dispose();
    return target[key];
} });
throws("prototype getter disposes node", () => Reflect.construct(LOD.Group, [doomed, [{ mesh: tri, until: 10 }]], newTarget), TypeError);

// Many groups: cost of a selection pass (printed on the console).
const many = [];
for (let i = 0; i < 300; i++) { const n = new Scene3D.Node(quad).setPosition(i % 20, 0, -(i / 20 | 0)); scene.root.add(n); many.push(new LOD.Group(n, [{ mesh: quad, until: 10 }, { mesh: tri, until: 25 }])); }
scene.update();
const t0 = Date.now();
for (let f = 0; f < 50; f++) { at(10 + (f % 10)); LOD.update(camera); }
if (typeof globalThis.__nativeDraws !== "function") console.log(`[LOD] 300 groups: ${((Date.now() - t0) * 1000 / 50).toFixed(0)} us per selection`);
for (const g of many) g.dispose();
group.dispose(); lights.dispose(); camera.dispose(); scene.dispose(); quad.dispose(); tri.dispose();
console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("LOD module test passed");
