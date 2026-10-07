import {Image} from "Image";
import * as Particles2D from "Particles2D";
let checks = 0;
function assert(ok, label) { checks++; if (!ok) throw new Error(label); }
function throws(fn, type, label) {
    let error = null; try { fn(); } catch (e) { error = e; }
    assert(error !== null && (!type || error instanceof type), label);
}
__spriteDraws();

const image = new Image("spark.png");
const fire = new Particles2D.Emitter(image, {capacity: 64, rate: 40, life: [.5, 1], speed: [20, 40],
    angle: -Math.PI / 2, spread: .6, gravity: [0, -10], drag: .2, size: [8, 0], color: [0x80ffffff, 0x00000000],
    area: [4, 2], seed: 3});
assert(fire.count === 0 && fire.active === true, "fresh emitter");
assert(fire.setPosition(100, 200) === fire, "chainable");
fire.update(.25); assert(fire.count === 10, "rate * dt");
assert(fire.emit(100) === 54 && fire.count === 64, "bursts fill the pool");
fire.draw();
let draws = __spriteDraws();
assert(draws.particles === 64, "every live particle goes to the VU1 batch");
fire.active = false; fire.update(2); assert(fire.count === 0, "particles die; no new ones while inactive");

// Rotation needs no other path: the VU1 program turns every quad.
fire.configure({spin: [-3, 3], rate: 0}).emit(5);
fire.draw(); draws = __spriteDraws();
assert(draws.particles === 5, "rotated particles use the same batch");
fire.clear(); assert(fire.count === 0, "clear");

// Validation.
throws(() => new Particles2D.Emitter({}), TypeError, "image required");
throws(() => new Particles2D.Emitter(image, {capacity: 0}), RangeError, "capacity range");
throws(() => new Particles2D.Emitter(image, {life: [1, .5]}), RangeError, "min <= max");
throws(() => new Particles2D.Emitter(image, {gravity: 3}), TypeError, "gravity is a pair");
throws(() => new Particles2D.Emitter(image, {color: "red"}), TypeError, "colours are numbers");
throws(() => fire.emit(1.5), RangeError, "integer bursts");
throws(() => fire.update(-1), RangeError, "negative dt");
throws(() => { fire.active = 1; }, TypeError, "boolean active");

// The emitter holds its image; a freed image draws nothing.
const smoke = new Particles2D.Emitter(new Image("smoke.png"), {rate: 100});
std.gc();
smoke.update(.1); smoke.draw(); assert(__spriteDraws().particles === 10, "image kept alive by the emitter");

// Module update and Loop.
smoke.clear();
Particles2D.update(.1); assert(smoke.count === 10, "update() runs every emitter");
Particles2D.attachLoop(); Particles2D.attachLoop();
assert(Particles2D.isAttached() && Particles2D.detachLoop() && !Particles2D.isAttached(), "loop attach/detach");
Particles2D.attachLoop(); // left attached: runtime cleanup must detach it
assert(Particles2D.MAX_PARTICLES === 16384, "limit");

fire.dispose(); fire.dispose();
throws(() => fire.draw(), TypeError, "disposed emitter");
std.gc();
console.log("Particles2D tests passed (" + checks + " checks)");
