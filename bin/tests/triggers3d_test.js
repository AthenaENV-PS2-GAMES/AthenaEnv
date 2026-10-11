/*
 * Triggers3D module: box and sphere zones, enter/exit events, layer masks,
 * disabling, node following, removal callbacks and errors.
 */
import * as Triggers3D from "Triggers3D";
import * as Scene3D from "Scene3D";

let passed = 0, failed = 0;
function check(name, condition) { if (condition) passed++; else { failed++; console.log("[FAIL] " + name); } }
function throws(name, callback, type) {
    let error = null; try { callback(); } catch (e) { error = e; }
    check(name + " throws" + (type ? " " + type.name : ""), error !== null && (!type || error instanceof type));
}
const log = [];
const world = new Triggers3D.World();
const PLAYER = 1, ENEMY = 2;
const door = world.box(0, 0, 0, 2, 2, 2, { mask: PLAYER, onEnter: (b, z) => log.push(`enter ${b.data}`), onExit: b => log.push(`exit ${b.data}`) });
const pool = world.sphere(10, 0, 0, 2, { mask: PLAYER | ENEMY, data: "pool" });
const hero = world.body(-1, 1, 1, { radius: 0.5, layers: PLAYER, data: "hero" });
const orc = world.body(1, 1, 1, { layers: ENEMY, data: "orc" });

check("first update: orc inside the door but masked out", world.update() === 0 && log.length === 0);
check("contains respects the mask", !door.contains(orc));
hero.setPosition(-0.4, 1, 1);
check("sphere overlap enters", world.update() === 1 && log.join() === "enter hero" && door.contains(hero));
check("no repeated events", world.update() === 0);
hero.setPosition(5, 1, 1);
world.update();
check("exit", log.join() === "enter hero,exit hero" && !door.contains(hero));
log.length = 0;
const seen = [];
world.onEnter = (z, b) => seen.push(`${z.data}:${b.data}`);
orc.setPosition(10, 0, 1.5);
world.update();
check("world callback and sphere zone", seen.join() === "pool:orc" && pool.occupants()[0] === orc);
pool.enabled = false;
world.update();
check("disabled zones release their bodies", !pool.contains(orc));
pool.enabled = true; world.update();
orc.layers = 0; world.update();
check("layers change", !pool.contains(orc));

// Following a node.
const scene = new Scene3D.Scene(), mover = new Scene3D.Node();
scene.root.add(mover); scene.update();
const zone = world.sphere(0, 0, 0, 1, { mask: PLAYER, onEnter: () => log.push("caught") }).follow(mover, 0, 0, 0);
mover.setPosition(5, 1, 1); scene.update();
world.update();
check("zone follows its node", log.includes("caught") && zone.contains(hero));
const tracked = world.body(0, 0, 0, { layers: PLAYER, data: "tracked" }).follow(mover);
world.update();
check("body follows its node", zone.contains(tracked));

// Removal runs onExit for the occupants.
log.length = 0;
door.setBox(4, 0, 0, 6, 2, 2);
world.update();
check("door moved over the hero", door.contains(hero) && log.includes("enter hero"));
log.length = 0;
hero.dispose();
check("body removal exits its zones", log.includes("exit hero") && !door.occupants().some(b => b.data === "hero"));
log.length = 0;
const other = world.body(5, 1, 1, { layers: PLAYER, data: "other" }); world.update();
door.dispose();
check("zone removal exits its bodies", log.includes("exit other"));
throws("disposed zone", () => door.contains(other), TypeError);
throws("bad box", () => world.box(1, 0, 0, 0, 1, 1), RangeError);
throws("bad radius", () => world.sphere(0, 0, 0, -1), RangeError);
throws("bad callback", () => world.box(0, 0, 0, 1, 1, 1, { onEnter: 3 }), TypeError);

// Invalid callback options must not reserve hidden native zones.
const invalid = new Triggers3D.World();
for (let i = 0; i < Triggers3D.MAX + 1; i++) {
    try { invalid.sphere(0, 0, 0, 1, { onEnter: 3 }); } catch (e) { /* expected */ }
}
check("invalid callbacks leave no zones", invalid.zones().length === 0 && invalid.sphere(0, 0, 0, 1).id === 0);
invalid.dispose();

// A callback removes a body and immediately reuses its id. Old queued
// events must never be delivered to the replacement body.
const changing = new Triggers3D.World();
let replacementEvents = 0, replacement = null;
const victim = changing.body(0, 0, 0);
changing.sphere(0, 0, 0, 2, { onEnter() {
    victim.dispose();
    replacement = changing.body(10, 0, 0);
} });
const stale = changing.sphere(0, 0, 0, 2, { onEnter(body) { if (body === replacement) replacementEvents++; } });
changing.update();
check("reused ids do not receive stale events", replacementEvents === 0 && !stale.contains(replacement));
changing.dispose();
throws("world disposal invalidates retained body", () => replacement.setPosition(0, 0, 0), TypeError);
replacement.dispose();

const recursive = new Triggers3D.World();
recursive.body(0, 0, 0);
const rz = recursive.sphere(0, 0, 0, 2, { onEnter() {
    throws("recursive update", () => recursive.update(), TypeError);
} });
recursive.update();
rz.onExit = () => { throw new Error("callback failure"); };
rz.enabled = false;
throws("callback errors propagate", () => recursive.update(), Error);
check("dispatch guard resets after callback error", recursive.update() === 0);
recursive.dispose();

// Cost: 50 zones x 20 bodies.
const w2 = new Triggers3D.World(), bodies = [];
for (let i = 0; i < 50; i++) w2.sphere(i * 3, 0, 0, 1);
for (let i = 0; i < 20; i++) bodies.push(w2.body(i * 7, 0, 0, { radius: 0.5 }));
const t0 = Date.now();
for (let f = 0; f < 100; f++) { bodies[f % 20].setPosition((f * 1.3) % 150, 0, 0); w2.update(); }
if (typeof globalThis.__nativeDraws !== "function") console.log(`[Triggers3D] 50 zones x 20 bodies: ${((Date.now() - t0) * 10).toFixed(0)} us per update`);
// ---- E07: onStay ----
{
    const w = new Triggers3D.World(), seq = [];
    const a = w.box(0, 0, 0, 4, 4, 4, { onEnter: b => seq.push(`A+${b.data}`), onExit: b => seq.push(`A-${b.data}`), onStay: b => seq.push(`A=${b.data}`) });
    const q = w.box(10, 0, 0, 14, 4, 4, { onEnter: b => seq.push(`Q+${b.data}`), onExit: b => seq.push(`Q-${b.data}`) });   // no stay
    const x = w.body(2, 2, 2, { data: "x" }), y = w.body(20, 2, 2, { data: "y" });
    w.update();
    check("a body that enters gets ENTER only", seq.join() === "A+x");
    seq.length = 0; w.update();
    check("the next update reports STAY", seq.join() === "A=x");
    seq.length = 0; w.update(); w.update();
    check("STAY every update while inside", seq.join() === "A=x,A=x");
    // One update with enter, exit and stay: ENTER/EXIT first (zone, body order), then STAY.
    seq.length = 0;
    y.setPosition(12, 2, 2); x.setPosition(11, 2, 2);   // x leaves A and enters Q, y enters Q
    const z = w.body(1, 1, 1, { data: "z" }); w.update();
    check("order: exits and enters by zone/body, then no stay for new pairs", seq.join() === "A-x,A+z,Q+x,Q+y" );
    seq.length = 0; w.update();
    check("stay only for zones that opted in", seq.join() === "A=z");
    q.onStay = b => seq.push(`Q=${b.data}`);
    seq.length = 0; w.update();
    check("assigning onStay opts a zone in; bodies in id order", seq.join() === "A=z,Q=x,Q=y");
    q.onStay = undefined; seq.length = 0; w.update();
    check("clearing onStay opts out", seq.join() === "A=z");
    const worldStays = []; w.onStay = (zone, body) => worldStays.push(`${zone.id}:${body.data}`);
    seq.length = 0; w.update();
    check("world onStay covers every zone and runs with the zone's own", worldStays.join() === "0:z,1:x,1:y" && seq.join() === "A=z");
    w.onStay = undefined;
    // Disabled zone: occupants leave, nobody stays.
    seq.length = 0; a.enabled = false; w.update();
    check("disabling a zone exits instead of staying", seq.join() === "A-z");
    a.enabled = true; seq.length = 0; w.update(); seq.length = 0; w.update();
    check("re-enabled zone: ENTER then STAY", seq.join() === "A=z");
    // Callbacks that dispose objects during STAY dispatch.
    const w3 = new Triggers3D.World(); const got = [];
    const b1 = w3.body(1, 1, 1, { data: 1 }), b2 = w3.body(1, 1, 1, { data: 2 });
    const z1 = w3.box(0, 0, 0, 3, 3, 3, { onStay(b) { got.push(b.data); if (b.data === 1) { b2.dispose(); } } });
    w3.update(); got.length = 0; w3.update();
    check("a body disposed by an earlier STAY callback gets none", got.join() === "1" && !b2.occupied);
    z1.dispose(); w3.dispose();
    const w4 = new Triggers3D.World(); let count = 0;
    const doomed = w4.body(1, 1, 1);
    w4.box(0, 0, 0, 3, 3, 3, { onStay() { count++; w4.dispose(); } });
    w4.update(); w4.update();
    check("disposing the world inside a STAY callback is safe", count === 1);
    throws("onStay must be a function", () => w.box(0, 0, 0, 1, 1, 1, { onStay: 3 }), TypeError);
    throws("onStay setter type", () => { a.onStay = 4; }, TypeError);
    w.dispose();
}

// ---- E07: broad phase grid is identical to the full test ----
function triggerRun(cell, capacity) {
    let seed = 777; const rnd = () => (seed = (Math.imul(seed, 1103515245) + 12345) >>> 0) / 4294967296;
    const w = new Triggers3D.World(), trace = [];
    const sc = new Scene3D.Scene(), nodes = [];
    const stayAll = new Triggers3D.World();   // a second world: the grid is per world
    if (cell !== 0) w.setGrid(cell, capacity === undefined ? {} : { capacity });
    const zones = [], bodies = [];
    const rec = (t, zone, body) => trace.push(`${t}${zone.id}:${body.id}`);
    w.onEnter = (z, b) => rec("+", z, b); w.onExit = (z, b) => rec("-", z, b);
    const mkZone = i => {
        const x = rnd() * 200 - 100, y = rnd() * 10, zz = rnd() * 200 - 100, big = i % 17 === 0;
        const zone = i % 2 ? w.box(x, y, zz, x + 2 + rnd() * (big ? 150 : 8), y + 4, zz + 2 + rnd() * 8, { mask: 1 + (i % 3), onStay: i % 5 === 0 ? ((b, z) => trace.push(`=${z.id}:${b.id}`)) : undefined })
            : w.sphere(x, y, zz, 1 + rnd() * (big ? 90 : 6), { mask: 1 + (i % 3), onStay: i % 5 === 0 ? ((b, z) => trace.push(`=${z.id}:${b.id}`)) : undefined });
        zones[i] = zone;
    };
    const mkBody = i => {
        bodies[i] = w.body(rnd() * 200 - 100, rnd() * 6, rnd() * 200 - 100, { radius: i % 11 === 0 ? 30 : rnd() * 1.5, layers: 1 << (i % 3) });
        if (i % 4 === 0) { const n = new Scene3D.Node(); sc.root.add(n); n.setPosition(rnd() * 200 - 100, 1, rnd() * 200 - 100); nodes[i] = n; bodies[i].follow(n, 0, 0.5, 0); }
    };
    for (let i = 0; i < 40; i++) mkZone(i);
    for (let i = 0; i < 120; i++) mkBody(i);
    sc.update();
    for (let f = 0; f < 70; f++) {
        for (let i = 0; i < 120; i++) {
            if (!bodies[i]) continue;
            if (nodes[i] && (i + f) % 3 === 0) nodes[i].setPosition(rnd() * 200 - 100, 1, rnd() * 200 - 100);
            else if (!nodes[i] && (i + f) % 5 === 0) bodies[i].setPosition(rnd() * 200 - 100, rnd() * 6, rnd() * 200 - 100, rnd() * 2);
        }
        for (let i = 0; i < 40; i++) if (zones[i] && (i * 3 + f) % 11 === 0) { if (i % 2) zones[i].setBox(rnd() * 100 - 50, 0, rnd() * 100 - 50, rnd() * 100 + 60, 6, rnd() * 100 + 60); else zones[i].setSphere(rnd() * 100 - 50, 2, rnd() * 100 - 50, 1 + rnd() * 20); }
        if (f === 15) for (let i = 0; i < 40; i += 6) zones[i].enabled = false;
        if (f === 25) for (let i = 0; i < 40; i += 6) zones[i].enabled = true;
        if (f === 30) for (let i = 0; i < 40; i += 5) zones[i].mask = 7;
        if (f === 35) for (let i = 0; i < 120; i += 9) bodies[i].layers = 4;
        if (f === 40) for (let i = 1; i < 120; i += 13) { bodies[i].dispose(); nodes[i] = undefined; bodies[i] = undefined; mkBody(i); }
        if (f === 45) for (let i = 2; i < 40; i += 7) { zones[i].dispose(); mkZone(i); }
        if (f === 50) w.setGrid(cell, capacity === undefined ? {} : { capacity });   // runtime reconfiguration (a no-op when off)
        if (f === 55) for (let i = 0; i < 120; i += 8) if (bodies[i]) bodies[i].setPosition(1000 + i, 0, -1000, 0);   // teleported far away
        sc.update();
        trace.push(`#${f}:${w.update()}`);
        trace.push(`i${[...zones.keys()].map(i => zones[i] ? zones[i].occupants().map(b => b.id).join(".") : "x").join("|")}`);
    }
    const st = w.stats();
    w.dispose(); stayAll.dispose(); sc.dispose();
    return { trace, st };
}
{
    const ref = triggerRun(0), same = (a, b) => {
        const i = a.trace.findIndex((l, k) => l !== b.trace[k]);
        if (i >= 0) console.log(`[Triggers3D] traces differ at ${i}: ${a.trace[i]} <> ${b.trace[i]}`);
        return i < 0 && a.trace.length === b.trace.length;
    };
    check("reference run produced events", ref.trace.filter(l => l[0] === "+").length > 10 && ref.trace.some(l => l[0] === "=") && ref.trace.some(l => l[0] === "-"));
    const on = triggerRun(20);
    check("grid: events, STAY and occupancy identical to the full test", same(ref, on));
    check("grid: fewer pairs considered than zones x bodies", on.st.candidates < ref.st.candidates && on.st.cells > 0, `candidates ${on.st.candidates} < ${ref.st.candidates}`);
    check("grid: tiny cells identical", same(ref, triggerRun(2)));
    check("grid: one huge cell identical", same(ref, triggerRun(10000)));
    const over = triggerRun(6, 24);
    check("grid: capacity overflow (large list) identical", same(ref, over) && over.st.largeBodies > 0);
    const fine = triggerRun(4);
    check("grid: large zones fall back to testing every body (identical)", same(ref, fine) && fine.st.largeZones > 0, `largeZones ${fine.st.largeZones}`);
    throws("grid arguments", () => new Triggers3D.World().setGrid(-1), RangeError);
    throws("grid capacity", () => new Triggers3D.World().setGrid(5, { capacity: 70000 }), RangeError);
}

w2.dispose(); world.dispose(); scene.dispose();
console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("Triggers3D module test passed");
