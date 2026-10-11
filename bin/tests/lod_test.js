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

// E05 spatial grid: identical selection to the full traversal. Two seeded runs (grid off / on) record, per
// frame, every group's level, visibility and the stats; moving nodes, a teleporting camera, enable toggles,
// bias, draw distance, group creation/disposal and a tiny capacity (overflow set) are all exercised.
function gridRun(cell, capacity, groupsCount) {
    let seed = 12345; const rnd = () => (seed = (Math.imul(seed, 1103515245) + 12345) >>> 0) / 4294967296;
    const sc = new Scene3D.Scene(), cam = new Camera({ near: 0.5, far: 500, aspect: 1 });
    const meshes = [quad, tri, null];
    const nodes = [], gs = [], px = [], pz = [];
    const make = i => {
        px[i] = rnd() * 300 - 150; pz[i] = rnd() * 300 - 150;
        const n = new Scene3D.Node(quad).setPosition(px[i], rnd() * 20, pz[i]);
        sc.root.add(n);
        const near = 8 + rnd() * 12;
        gs[i] = new LOD.Group(n, [{ mesh: quad, until: near }, { mesh: meshes[(rnd() * 3) | 0], until: near * 2 }, { mesh: tri, until: near * 3 + rnd() * 20 }], { hysteresis: [0, 0.1, 0.25][i % 3] });
        nodes[i] = n;
    };
    for (let i = 0; i < groupsCount; i++) make(i);
    sc.update();
    LOD.setGrid(cell, capacity === undefined ? undefined : { capacity });
    const trace = []; let skipped = 0, evaluated = 0, total = 0;
    for (let f = 0; f < 90; f++) {
        // Moving nodes (a fifth of them), some jump across cells.
        for (let i = 0; i < groupsCount; i++) if (nodes[i] && i % 5 === f % 5) { px[i] += 3; nodes[i].setPosition(px[i], 0, pz[i]); }
        for (let i = 0; i < groupsCount; i++) if (nodes[i] && (i * 7 + f) % 41 === 0) { px[i] = rnd() * 300 - 150; pz[i] = rnd() * 300 - 150; nodes[i].setPosition(px[i], rnd() * 20, pz[i]); }
        if (f === 30) for (let i = 0; i < groupsCount; i += 11) gs[i].enabled = false;
        if (f === 45) for (let i = 0; i < groupsCount; i += 11) gs[i].enabled = true;
        if (f === 50) LOD.setBias(1.7);
        if (f === 60) LOD.setBias(0.6);
        if (f === 70) LOD.setDrawDistance(60);
        if (f === 80) { LOD.setBias(1); LOD.setDrawDistance(0); }
        if (f === 55) { gs[3].dispose(); nodes[3].dispose(); make(3); sc.update(); }
        if (f % 15 === 0) cam.setPosition(rnd() * 400 - 200, 10, rnd() * 400 - 200);   // teleport
        else { cam.setPosition(rnd() * 20 - 10 + (f % 15) * 3, 10, rnd() * 20 - 10 + (f % 15) * 2); }
        cam.lookAt(0, 0, 0);
        const st = LOD.update(cam, {});
        sc.update();   // moved nodes are noticed here, one frame later, in both modes
        let line = st.groups + "/" + st.hidden + "/" + st.changes + "/" + st.perLevel.join(",") + "|";
        for (let i = 0; i < groupsCount; i++) line += gs[i].level + (nodes[i].visible ? "v" : "h") + ",";
        trace.push(line); skipped += st.cellsSkipped; evaluated += st.evaluated; total += st.groups;
    }
    for (const g of gs) g.dispose();
    LOD.setGrid(0); LOD.setBias(1); LOD.setDrawDistance(0);
    sc.dispose(); cam.dispose();
    return { trace, skipped, evaluated, total };
}
{
    group.enabled = false;   // the earlier group would carry its state from one run to the next
    const ref = gridRun(0, undefined, 400);
    const sameTrace = (a, b) => {
        const i = a.trace.findIndex((l, k) => l !== b.trace[k]);
        if (i >= 0) { const x = a.trace[i], y = b.trace[i]; let j = 0; while (x[j] === y[j]) j++; console.log(`[LOD] trace differs at frame ${i}, offset ${j}: ${x.slice(Math.max(0, j - 30), j + 30)} <> ${y.slice(Math.max(0, j - 30), j + 30)}`); }
        return i < 0 && a.trace.length === b.trace.length;
    };
    const on = gridRun(60, undefined, 400);
    check("grid: selection identical to the full traversal", sameTrace(ref, on));
    check("grid: whole cells are skipped on a sparse scene", on.skipped > 0 && on.evaluated < on.total);
    check("grid: stats without it evaluate every group", ref.evaluated === ref.total && ref.skipped === 0);
    check("grid: tiny cells identical", sameTrace(ref, gridRun(7, undefined, 400)));
    check("grid: huge cell (one cell) identical", sameTrace(ref, gridRun(5000, undefined, 400)));
    const over = gridRun(20, 4, 400);
    check("grid: capacity overflow set identical", sameTrace(ref, over));
    throws("grid: negative cell size", () => LOD.setGrid(-1), RangeError);
    throws("grid: capacity range", () => LOD.setGrid(10, { capacity: 70000 }), RangeError);
    throws("grid: NaN", () => LOD.setGrid(NaN), RangeError);
    // Groups created and enabled while the grid is already on, and reconfiguration at runtime.
    const gs2 = new Scene3D.Scene(), nn = new Scene3D.Node(quad).setPosition(100, 0, 0); gs2.root.add(nn); gs2.update();
    LOD.setGrid(30);
    const late = new LOD.Group(nn, [{ mesh: quad, until: 10 }, { mesh: tri, until: 20 }]);
    camera.lookAt(0, 0, -1).setPosition(0, 0, 0);
    let st = LOD.update(camera, {});
    check("grid: group created after setGrid is selected", late.level === LOD.HIDDEN && st.groups === 1);
    st = LOD.update(camera, {});
    check("grid: settled far cell is skipped", st.cellsSkipped === 1 && st.hidden === 1 && st.evaluated === 0);
    nn.setPosition(2, 0, 0); gs2.update();
    st = LOD.update(camera, {});
    check("grid: node moving into range is re-binned and selected", late.level === 0 && st.cellsSkipped === 0);
    LOD.setGrid(0); LOD.setGrid(45); st = LOD.update(camera, {});
    check("grid: reconfigured at runtime", late.level === 0 && st.groups === 1);
    late.dispose(); LOD.update(camera, {});
    check("grid: disposing the group frees its cell", LOD.stats({}).groups === 0);
    nn.setPosition(500, 0, 0); gs2.update();   // hook list of a node without LOD groups
    LOD.setGrid(0); gs2.dispose();
}
group.dispose(); lights.dispose(); camera.dispose(); scene.dispose(); quad.dispose(); tri.dispose();
console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("LOD module test passed");
