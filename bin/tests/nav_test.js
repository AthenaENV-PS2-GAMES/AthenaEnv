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

// Persistent requests: all search/output stages share the grid's budget.
function finishRequest(g, r, work = 7, budget = 0) {
    let calls = 0;
    while (r.state === "pending") {
        const count = g.stepRequests(work, budget);
        if (!(count > 0 && count <= work && ++calls < 1000000)) throw new Error("Incremental query failed to progress");
    }
    return calls;
}
const queryGrid = new Nav.Grid(20, 20);
queryGrid.fill(10, 0, 10, 17, 0);
for (const diagonal of [false, true]) for (const smooth of [false, true]) {
    const expected = queryGrid.findPath(5.25, 5.75, 15.75, 5.25, { diagonal, smooth });
    for (const limit of [1, 7, 128]) {
        const r = queryGrid.requestPath(5.25, 5.75, 15.75, 5.25, { diagonal, smooth });
        check("request initially pending", r.state === "pending" && r.phase === "queued" && r.getPath() === null && r.pointCount === 0);
        finishRequest(queryGrid, r, limit);
        const result = r.getPath();
        check("incremental path exactly matches synchronous", r.state === "found" && r.error === null && result.join() === expected.join());
        check("all stages counted", r.workUnits > r.expanded && r.expanded > 0 && r.pointCount * 2 === result.length);
        check("request memory declared", r.storageBytes >= Nav.MAX_PATH * 8 && r.storageBytes < Nav.MAX_PATH * 8 + 1024);
        const storage = new Float32Array(4096), view = storage.subarray(5, 4000);
        storage[4] = 123; storage[4000] = 456;
        const length = r.copyPath(view);
        check("reusable output honors subarray and valid length", length === result.length && view.subarray(0, length).join() === expected.join() && storage[4] === 123 && storage[4000] === 456);
        const short = new Float32Array([11, 22]);
        throws("short result buffer", () => r.copyPath(short), RangeError);
        check("short output is untouched", short[0] === 11 && short[1] === 22);
        r.cancel(); check("completed cancel keeps result", r.state === "found");
        r.dispose(); r.dispose(); throws("disposed request", () => r.state, TypeError);
    }
}
const weightedRequest = g3.requestPath(.5, 1.5, 9.5, 1.5);
finishRequest(g3, weightedRequest, 1);
check("incremental weighted smoothing", weightedRequest.getPath().join() === smoothCheap.join());
weightedRequest.dispose();
const cornerRequest = g2.requestPath(.5, .5, 1.5, 1.5);
finishRequest(g2, cornerRequest, 1);
check("incremental corner rule", cornerRequest.state === "noPath" && cornerRequest.getPath() === null);
cornerRequest.dispose();
const sameCell = queryGrid.requestPath(1.2, 1.3, 1.7, 1.8);
finishRequest(queryGrid, sameCell, 1);
check("same cell keeps exact endpoints", sameCell.pointCount === 2 && close(sameCell.getPath()[2], 1.7));
sameCell.dispose();
const tooLong = queryGrid.requestPath(5.5, 5.5, 15.5, 5.5, { smooth: false, maxPoints: 2 });
finishRequest(queryGrid, tooLong, 1);
check("capacity overflow is explicit", tooLong.state === "error" && tooLong.error === "pathTooLong" && tooLong.pointCount === 0 && tooLong.getPath() === null);
tooLong.dispose();
const limited = queryGrid.requestPath(5.5, 5.5, 15.5, 5.5, { maxIterations: 1 });
finishRequest(queryGrid, limited, 1);
check("lifetime expansion limit explicit", limited.error === "iterationLimit");
limited.dispose();
const queued = Array.from({ length: Nav.MAX_PENDING }, () => queryGrid.requestPath(5.5, 5.5, 15.5, 5.5));
check("bounded queue", queryGrid.pendingRequests === Nav.MAX_PENDING);
throws("queue full", () => queryGrid.requestPath(1, 1, 2, 2), RangeError);
throws("synchronous scratch conflict", () => queryGrid.findPath(1, 1, 2, 2), TypeError);
queryGrid.stepRequests(2);
check("FIFO active owns scratch", queued[0].expanded === 1 && queued[1].workUnits === 0);
queued[3].cancel(); queued[3].cancel();
check("queued cancel frees capacity", queryGrid.pendingRequests === Nav.MAX_PENDING - 1 && queued[3].state === "cancelled");
queryGrid.cancelRequests();
check("cancel all removes active queue", queryGrid.pendingRequests === 0 && queued[0].state === "cancelled");
for (const r of queued) r.dispose();
for (const phase of ["search", "countPath", "tracePath", "emitPath", "smoothPath"]) {
    const r = queryGrid.requestPath(5.5, 5.5, 15.5, 5.5);
    let steps = 0;
    while (r.phase !== phase && r.state === "pending" && steps++ < 10000) queryGrid.stepRequests(1);
    check("reaches " + phase, r.phase === phase);
    queryGrid.setCost(1, 1, 2);
    check("mutation invalidates " + phase, r.state === "error" && r.error === "gridChanged" && queryGrid.pendingRequests === 0);
    r.dispose(); queryGrid.setCost(1, 1, 1);
}
const stable = queryGrid.requestPath(1, 1, 2, 2), revision = queryGrid.revision;
queryGrid.fill(1, 1, 2, 2, 1);
check("no-op mutation retains query", stable.state === "pending" && queryGrid.revision === revision);
queryGrid.setCosts(new Uint8Array(400).fill(3));
check("setCosts invalidates", stable.error === "gridChanged" && queryGrid.revision !== revision);
stable.dispose(); queryGrid.setCosts(new Uint8Array(400).fill(1));
const timed = queryGrid.requestPath(.5, .5, 19.5, 19.5);
check("tiny clock budget still completes", finishRequest(queryGrid, timed, 64, .000001) > 10 && timed.state === "found");
timed.dispose();
throws("request extreme coords", () => queryGrid.requestPath(1e30, 0, 1, 1), RangeError);
for (const work of [0, -1, 1.5, Nav.MAX_CELLS + 1]) throws("work limit", () => queryGrid.stepRequests(work), RangeError);
for (const budget of [-1, 1001, Infinity, NaN]) throws("time budget", () => queryGrid.stepRequests(1, budget), RangeError);
for (const capacity of [0, 1, 2.5, Nav.MAX_CELLS + 2]) throws("output capacity", () => queryGrid.requestPath(1, 1, 2, 2, { maxPoints: capacity }), RangeError);
let gcRequest = queryGrid.requestPath(1, 1, 2, 2);
gcRequest = null; std.gc();
check("request finalizer unlinks queue", queryGrid.pendingRequests === 0);
const disposeGrid = new Nav.Grid(4, 4), cancelledByGrid = disposeGrid.requestPath(.5, .5, 3.5, 3.5);
disposeGrid.dispose(); disposeGrid.dispose();
check("grid dispose cancels retained query", cancelledByGrid.state === "cancelled");
cancelledByGrid.dispose();
const disposedByOption = new Nav.Grid(4, 4);
throws("request option getter disposes grid", () => disposedByOption.requestPath(1, 1, 2, 2, {
    get maxPoints() { disposedByOption.dispose(); return 8; }
}), TypeError);
const completed = queryGrid.requestPath(1, 1, 2, 2);
finishRequest(queryGrid, completed, 1);
const f32Descriptor = Object.getOwnPropertyDescriptor(globalThis, "Float32Array"), OriginalFloat32 = globalThis.Float32Array;
let safeCopy;
try {
    Object.defineProperty(globalThis, "Float32Array", { configurable: true, get() { completed.dispose(); return OriginalFloat32; } });
    safeCopy = completed.getPath();
} finally { Object.defineProperty(globalThis, "Float32Array", f32Descriptor); }
check("getPath copies before constructor getter disposes request", safeCopy.length === 4 && safeCopy[0] === 1);
const moving = new Nav.Crowd(queryGrid), movingAgent = moving.add({ x: .5, z: .5 });
movingAgent.moveTo(15.5, 15.5);
const movingCount = movingAgent.waypoints, conflict = queryGrid.requestPath(1, 1, 2, 2);
throws("agent scratch conflict", () => movingAgent.moveTo(2, 2), TypeError);
check("agent conflict preserves movement", movingAgent.state === "moving" && movingAgent.waypoints === movingCount);
conflict.dispose(); movingAgent.dispose(); moving.dispose();
queryGrid.dispose();

// Agent-owned plans: step adopts results; movement and lifecycle stay explicit.
const agentGrid = new Nav.Grid(32, 32);
agentGrid.fill(16, 0, 16, 27, 0);
const agentScene = new Scene3D.Scene(), agentNode = new Scene3D.Node();
agentScene.root.add(agentNode);
const agentCrowd = new Nav.Crowd(agentGrid);
const planner = agentCrowd.add({ x: 1.5, y: 2, z: 1.5, speed: 10, radius: 1, node: agentNode });
const closeAgent = agentCrowd.add({ x: 1.5, z: 1.5, radius: 1 });
check("agent has no initial incremental plan", planner.planningState === "none" && planner.planningError === null);
const agentExpected = agentGrid.findPath(1.5, 1.5, 30.5, 1.5);
let agentRequest = planner.requestMoveTo(30.5, 1.5);
check("agent enters planning", planner.state === "planning" && planner.planningState === "pending" && planner.waypoints === 0);
agentCrowd.update(1); agentScene.update();
check("planning freezes movement and separation", planner.x === 1.5 && planner.z === 1.5 && closeAgent.x === 1.5 && closeAgent.z === 1.5);
check("planning keeps node position and height", close(agentNode.getWorldTransform().get(12), 1.5) && close(agentNode.getWorldTransform().get(13), 2));
finishRequest(agentGrid, agentRequest, 1);
check("step adopts exact path without update", planner.state === "moving" && planner.planningState === "found" && planner.planningError === null && planner.waypoints * 2 === agentExpected.length && agentRequest.getPath().join() === agentExpected.join());
agentRequest.dispose(); closeAgent.dispose();
for (let i = 0; i < 1000 && planner.state !== "arrived"; i++) agentCrowd.update(.1);
check("incremental agent reaches exact target", planner.state === "arrived" && close(planner.x, 30.5) && close(planner.z, 1.5) && planner.y === 2);
planner.setPosition(1.5, 2, 1.5);
for (const action of ["stop", "position", "cancel", "disposeRequest"]) {
    agentRequest = planner.requestMoveTo(30.5, 1.5); agentGrid.stepRequests(2);
    if (action === "stop") planner.stop();
    if (action === "position") planner.setPosition(1.5, 2, 1.5);
    if (action === "cancel") agentRequest.cancel();
    if (action === "disposeRequest") agentRequest.dispose();
    check("agent cancellation " + action, planner.state === "idle" && planner.planningState === "cancelled" && agentGrid.pendingRequests === 0);
    if (action !== "disposeRequest") check("observer sees cancellation " + action, agentRequest.state === "cancelled");
    agentRequest.dispose();
}
agentRequest = planner.requestMoveTo(16.5, 1.5); finishRequest(agentGrid, agentRequest, 1);
check("unreachable plan stops agent", agentRequest.state === "noPath" && planner.state === "idle" && planner.planningState === "noPath");
agentRequest.dispose();
agentRequest = planner.requestMoveTo(30.5, 1.5);
agentGrid.setCost(0, 0, 2);
check("mutation invalidates agent plan immediately", agentRequest.error === "gridChanged" && planner.planningError === "gridChanged" && planner.state === "idle");
agentRequest.dispose(); planner.moveTo(30.5, 1.5);
const oldWaypoints = planner.waypoints;
throws("invalid incremental target", () => planner.requestMoveTo(1e30, 1e30), RangeError);
check("invalid creation preserves motion", planner.state === "moving" && planner.waypoints === oldWaypoints && planner.planningState === "none");
const externalQueue = [];
for (let i = 0; i < Nav.MAX_PENDING; i++) externalQueue.push(agentGrid.requestPath(1, 1, 2, 2));
throws("full queue preserves agent motion", () => planner.requestMoveTo(30.5, 1.5), RangeError);
check("full creation preserves motion", planner.state === "moving" && planner.waypoints === oldWaypoints);
externalQueue.pop().dispose();
agentRequest = planner.requestMoveTo(30.5, 1.5);
const replaced = planner.requestMoveTo(2.5, 2.5);
check("replace at full capacity", agentRequest.state === "cancelled" && agentGrid.pendingRequests === Nav.MAX_PENDING && planner.state === "planning");
agentGrid.stepRequests(1);
check("replacement joins FIFO tail", replaced.workUnits === 0 && externalQueue[0].workUnits === 1);
agentRequest.dispose(); replaced.dispose(); for (const r of externalQueue) r.dispose();
planner.requestMoveTo(2.5, 2.5); std.gc();
check("observer GC keeps planning", agentGrid.pendingRequests === 1 && planner.state === "planning");
while (agentGrid.pendingRequests) agentGrid.stepRequests(7);
check("unobserved result adopted", planner.state === "moving" && planner.planningState === "found");
let gcPlanner = agentCrowd.add({ x: 1.5, z: 1.5 });
const gcPlan = gcPlanner.requestMoveTo(30.5, 1.5); gcPlanner = null; std.gc();
check("agent GC cancels with live observer", gcPlan.state === "cancelled" && agentGrid.pendingRequests === 0); gcPlan.dispose();
const removed = agentCrowd.add({ x: 1.5, z: 1.5 });
const removedPlan = removed.requestMoveTo(30.5, 1.5); removed.dispose();
const reusedAgent = agentCrowd.add({ x: 1.5, z: 1.5 });
check("removed plan cannot affect reused slot", removedPlan.state === "cancelled" && reusedAgent.state === "idle" && reusedAgent.planningState === "none");
removedPlan.dispose(); reusedAgent.dispose();
const getterAgent = agentCrowd.add({ x: 1.5, z: 1.5 });
throws("argument coercion disposes agent", () => getterAgent.requestMoveTo({ valueOf() { getterAgent.dispose(); return 2; } }, 2), TypeError);
const otherCrowd = new Nav.Crowd(agentGrid), otherPlanner = otherCrowd.add({ x: 1.5, z: 1.5 });
const otherPlan = otherPlanner.requestMoveTo(2.5, 2.5), crowdPlan = planner.requestMoveTo(30.5, 1.5);
const independentPlan = agentGrid.requestPath(1, 1, 2, 2);
agentCrowd.dispose();
check("crowd dispose cancels only owned plans", crowdPlan.state === "cancelled" && otherPlan.state === "pending" && independentPlan.state === "pending" && agentGrid.pendingRequests === 2);
check("retained agent remains usable after crowd disposal", planner.stop() === planner && planner.state === "idle");
crowdPlan.dispose(); independentPlan.dispose(); finishRequest(agentGrid, otherPlan, 1);
check("other crowd still adopts result", otherPlanner.state === "moving"); otherPlan.dispose();
const gridPlan = otherPlanner.requestMoveTo(30.5, 1.5); agentGrid.dispose();
check("grid disposal cancels agent plans", gridPlan.state === "cancelled" && otherPlanner.planningState === "cancelled");
gridPlan.dispose(); otherPlanner.dispose(); otherCrowd.dispose(); planner.dispose(); agentNode.dispose(); agentScene.dispose();

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
