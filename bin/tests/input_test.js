/*
 * Input module: bindings, edges, sticks (dead zone, curves, inversion,
 * d-pad), rebinding and saved bindings, over a scripted source. Runs on the
 * host (tests/js/run.sh) and on the console.
 */
import * as Input from "Input";

let passed = 0, failed = 0;
function check(name, condition) { if (condition) passed++; else { failed++; console.log("[FAIL] " + name); } }
function throws(name, callback, type) {
    let error = null; try { callback(); } catch (e) { error = e; }
    check(name + " throws" + (type ? " " + type.name : ""), error !== null && (!type || error instanceof type));
}
const close = (a, b) => Math.abs(a - b) < 1e-6;
const CROSS = 0x4000, CIRCLE = 0x2000, L1 = 0x0400, R1 = 0x0800, UP = 0x10, RIGHT = 0x20, DOWN = 0x40, LEFT = 0x80;

const pad = { buttons: 0, leftX: 0, leftY: 0, rightX: 0, rightY: 0 };
const map = new Input.Map({
    jump: Input.button(CROSS),
    dash: Input.button(L1 | R1),
    alt: Input.button(CROSS, CIRCLE),
    turn: Input.axis(LEFT, RIGHT),
    move: Input.stick("left", { dpad: true }),
    look: Input.stick("right", { deadZone: 0.2, curve: "quadratic", sensitivity: 2, invertY: true }),
}, { source: pad });

check("actions", map.actions().join() === "jump,dash,alt,turn,move,look");
map.update();
check("idle", !map.pressed("jump") && map.value("turn") === 0 && map.x("move") === 0);
pad.buttons = CROSS; map.update();
check("justPressed", map.pressed("jump") && map.justPressed("jump") && map.heldFrames("jump") === 1);
check("alternatives", map.pressed("alt"));
map.update();
check("held, no edge", map.pressed("jump") && !map.justPressed("jump") && map.heldFrames("jump") === 2);
pad.buttons = 0; map.update();
check("justReleased", !map.pressed("jump") && map.justReleased("jump") && map.heldFrames("jump") === 0);
pad.buttons = L1; map.update();
check("combo needs every bit", !map.pressed("dash"));
pad.buttons = L1 | R1; map.update();
check("combo held", map.justPressed("dash"));
pad.buttons = LEFT; map.update();
check("digital axis", map.value("turn") === -1 && map.pressed("turn"));
pad.buttons = LEFT | RIGHT; map.update();
check("opposite cancel", map.value("turn") === 0);

pad.buttons = 0; pad.leftX = 0.1; map.update();
check("inside the dead zone", map.x("move") === 0 && !map.pressed("move"));
pad.leftX = 1; map.update();
check("full deflection is 1", close(map.x("move"), 1) && close(map.value("move"), 1));
pad.leftX = 0.575; map.update();
check("rescaled past the dead zone", close(map.x("move"), (0.575 - 0.15) / 0.85));
const vec = map.axis("move");
pad.leftX = 0; pad.leftY = 0; pad.buttons = UP | RIGHT; map.update();
check("same vector object", map.axis("move") === vec);
check("d-pad fallback, normalized diagonal", close(vec.x, Math.SQRT1_2) && close(vec.y, -Math.SQRT1_2));
pad.buttons = 0; pad.rightX = 0; pad.rightY = -0.6; map.update();
const s = (0.6 - 0.2) / 0.8;
check("curve, sensitivity, invertY", close(map.y("look"), s * s * 2) && close(map.value("look"), s * s * 2));
pad.rightY = 0;

throws("unknown action", () => map.pressed("nope"), RangeError);
throws("axis of a button", () => map.axis("jump"), TypeError);
throws("bad mask", () => Input.button(0), TypeError);
throws("bad stick", () => Input.stick("middle"), TypeError);
throws("bad dead zone", () => Input.stick("left", { deadZone: 1 }), RangeError);
throws("bad curve", () => Input.stick("left", { curve: "sine" }), RangeError);
throws("bad source", () => map.setSource({}), TypeError);

map.rebind("jump", CIRCLE);
pad.buttons = CIRCLE; map.update();
check("rebind", map.justPressed("jump"));
const saved = JSON.parse(JSON.stringify(map.bindings()));
check("bindings are plain data", saved.jump.button[0] === CIRCLE && saved.look.curve === "quadratic" && saved.look.invertY === true);
const other = new Input.Map(saved, { source: pad });
other.update();
check("loaded bindings work", other.pressed("jump") && other.has("look"));
throws("load validates", () => other.load({ x: { stick: "left", sensitivity: Infinity } }), RangeError);
check("failed load keeps bindings", other.has("look"));
for (const name of ["platformer", "shooter", "menu"]) {
    const m = new Input.Map(Input.preset(name), { source: pad }); m.update();
    check(`preset ${name}`, m.actions().length >= 5);
}
throws("unknown preset", () => Input.preset("racing"), RangeError);
if (!globalThis.Gamepad) throws("no Gamepad and no source", () => new Input.Map({ a: Input.button(1) }).update(), Error);

console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("Input module test passed");
