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
w2.dispose(); world.dispose(); scene.dispose();
console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("Triggers3D module test passed");
