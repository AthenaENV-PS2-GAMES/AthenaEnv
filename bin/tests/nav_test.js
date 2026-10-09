/*
 * Nav module: grid costs, A* (diagonal, no corner cutting, costs, smoothing),
 * line of sight, nearest walkable, crowds of agents and node binding.
 */
import * as Nav from "Nav";
import * as Scene3D from "Scene3D";
import { Matrix4 } from "Matrix4"; // world transforms are Matrix4 objects
void Matrix4;

let passed = 0, failed = 0;
function check(name, condition) { if (condition) passed++; else { failed++; console.log("[FAIL] " + name); } }
function throws(name, callback, type) {
    let error = null; try { callback(); } catch (e) { error = e; }
    check(name + " throws" + (type ? " " + type.name : ""), error !== null && (!type || error instanceof type));
}
const host = typeof globalThis.__nativeDraws === "function";
const close = (a, b, e = 1e-3) => Math.abs(a - b) < e;

throws("grid size", () => new Nav.Grid(0, 4), RangeError);
throws("grid too big", () => new Nav.Grid(1024, 1024), RangeError);
const grid = new Nav.Grid(20, 20, { cellSize: 1, x: 0, z: 0 });
check("props", grid.width === 20 && grid.depth === 20 && grid.cellSize === 1 && grid.getCost(3, 3) === 1);
check("outside is blocked", grid.getCost(-1, 0) === 0 && grid.getCost(20, 0) === 0);

let path = grid.findPath(0.5, 0.5, 10.5, 0.5);
check("straight path smoothed to two points", path && path.length === 4 && close(path[2], 10.5) && close(path[3], 0.5));
// A wall with one gap at z = 18.
grid.fill(10, 0, 10, 17, 0);
check("fill", grid.getCost(10, 5) === 0 && grid.getCost(10, 18) === 1);
path = grid.findPath(5.5, 5.5, 15.5, 5.5);
check("goes around the wall", path !== null && path.length >= 6);
let maxZ = 0;
for (let i = 1; i < path.length; i += 2) maxZ = Math.max(maxZ, path[i]);
check("through the gap (z >= 18)", maxZ > 17.9);
for (let i = 2; i < path.length; i += 2)
    check(`segment ${i / 2} has line of sight`, grid.lineOfSight(path[i - 2], path[i - 1], path[i], path[i + 1]));
check("expanded cells reported", grid.lastExpanded > 10);
const raw = grid.findPath(5.5, 5.5, 15.5, 5.5, { smooth: false });
check("unsmoothed path has a point per cell", raw.length / 2 > path.length / 2);
grid.fill(10, 18, 10, 19, 0);
check("sealed: unreachable", grid.findPath(5.5, 5.5, 15.5, 5.5) === null);
check("target blocked", grid.findPath(5.5, 5.5, 10.5, 5.5) === null);
grid.fill(10, 18, 10, 19, 1);
// No corner cutting: a diagonal between two blocked cells is not taken.
const g2 = new Nav.Grid(3, 3);
g2.setCost(1, 0, 0); g2.setCost(0, 1, 0);
check("no corner cutting", g2.findPath(0.5, 0.5, 1.5, 1.5) === null);
check("4-way option", g2.findPath(2.5, 2.5, 2.5, 0.5, { diagonal: false }) !== null);
// Costs: a cheap road is preferred over a short expensive field.
const g3 = new Nav.Grid(10, 3);
g3.fill(0, 1, 9, 1, 9);                     // expensive middle row
const cheap = g3.findPath(0.5, 1.5, 9.5, 1.5, { smooth: false });
let leftRow = false; for (let i = 2; i < cheap.length - 2; i += 2) if (cheap[i + 1] !== 1.5) leftRow = true;
check("costs steer the path", leftRow);
const smoothCheap = g3.findPath(0.5, 1.5, 9.5, 1.5);
check("default smoothing preserves the cheap detour", smoothCheap.length > 4 && Array.from(smoothCheap).some((v, i) => i % 2 === 1 && v !== 1.5));
const thin = new Nav.Grid(1, 128);
check("near-axis ray checks every cell", thin.lineOfSight(0.5, 0.5, 0.500001, 127.5));
thin.setCost(0, 64, 0);
check("near-axis ray cannot skip a wall", !thin.lineOfSight(0.5, 0.5, 0.500001, 127.5));
check("huge finite coordinates have no line of sight", !grid.lineOfSight(1e30, 1e30, 0.5, 0.5));
throws("fractional search limit", () => grid.findPath(1.5, 1.5, 5.5, 5.5, { maxIterations: 1.5 }), RangeError);
throws("overflowing search limit", () => grid.findPath(1.5, 1.5, 5.5, 5.5, { maxIterations: 1e30 }), RangeError);
thin.dispose();
const doomed = new Nav.Grid(2, 2);
const newTarget = new Proxy(function () {}, { get(target, key) {
    if (key === "prototype") doomed.dispose();
    return target[key];
} });
throws("prototype getter disposes grid", () => Reflect.construct(Nav.Crowd, [doomed], newTarget), TypeError);
check("lineOfSight blocked", !grid.lineOfSight(5.5, 5.5, 15.5, 5.5) && grid.lineOfSight(5.5, 5.5, 5.5, 15.5));
const near = grid.nearestWalkable(10.5, 5.5);
check("nearest walkable", near && (close(near.x, 9.5) || close(near.x, 11.5)) && close(near.z, 5.5));
check("nearest from outside the grid", grid.nearestWalkable(-0.5, 5.5, 2).x === 0.5);
check("large nearest radius stays bounded", grid.nearestWalkable(5.5, 5.5, 0xffffffff).x === 5.5);
const costs = new Uint8Array(400).fill(1); costs[0] = 0;
grid.setCosts(costs);
check("setCosts", grid.getCost(0, 0) === 0 && grid.getCost(10, 5) === 1);
throws("setCosts size", () => grid.setCosts(new Uint8Array(3)), RangeError);
throws("cost range", () => grid.setCost(1, 1, 300), RangeError);

// Crowd.
grid.setCost(0, 0, 1);
grid.fill(10, 0, 10, 17, 0);
const scene = new Scene3D.Scene(), body = new Scene3D.Node();
scene.root.add(body);
const crowd = new Nav.Crowd(grid);
const a = crowd.add({ x: 5.5, y: 2, z: 5.5, speed: 4, radius: 0.3, node: body });
check("agent starts idle", a.state === "idle" && close(a.x, 5.5) && a.speed === 4);
check("moveTo plans", a.moveTo(15.5, 5.5) === true && a.state === "moving" && a.waypoints >= 2);
let arrived = 0, steps = 0;
for (; steps < 600 && a.state !== "arrived"; steps++) arrived += crowd.update(1 / 30);
check("arrives", a.state === "arrived" && arrived === 1 && close(a.x, 15.5, 1e-2) && close(a.z, 5.5, 1e-2));
check("took a plausible time", steps > 30 && steps < 400);
scene.update();
const w = body.getWorldTransform();
check("node follows the agent (y kept)", close(w.get(12), a.x) && close(w.get(13), 2) && close(w.get(14), a.z));
check("unreachable target", a.moveTo(10.5, 3.5) === false && a.state === "idle");
// Separation: two agents sent to the same point end apart.
const b1 = crowd.add({ x: 2.5, z: 2.5, radius: 0.4 }), b2 = crowd.add({ x: 2.5, z: 4.5, radius: 0.4 });
b1.moveTo(3.5, 12.5); b2.moveTo(3.5, 12.5);
for (let i = 0; i < 300; i++) crowd.update(1 / 30);
const d = Math.hypot(b1.x - b2.x, b1.z - b2.z);
check("separated", d > 0.6);
b1.dispose(); b2.dispose(); a.dispose();
throws("disposed agent", () => a.x, TypeError);

// Cost of a long search (printed on the console).
const big = new Nav.Grid(128, 128);
for (let x = 8; x < 128; x += 16) { const odd = Math.floor(x / 16) % 2; big.fill(x, odd ? 8 : 0, x, odd ? 127 : 119, 0); }
const t0 = Date.now();
let found = null; for (let i = 0; i < 10; i++) found = big.findPath(1.5, 1.5, 126.5, 126.5);
const ms = (Date.now() - t0) / 10;
check("maze path found", found !== null);
if (!host) console.log(`[Nav] 128x128 maze: ${ms.toFixed(1)} ms per findPath, ${big.lastExpanded} cells expanded, ${found.length / 2} points`);
big.dispose(); crowd.dispose(); grid.dispose(); g2.dispose(); g3.dispose(); scene.dispose();
console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("Nav module test passed");
