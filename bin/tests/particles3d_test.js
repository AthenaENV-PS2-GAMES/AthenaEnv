import {Image} from "Image";
import {Camera} from "Camera3D";
import * as Particles3D from "Particles3D";
let checks = 0;
function assert(ok, label) { checks++; if (!ok) throw new Error(label); }
function throws(fn, type, label) {
    let error = null; try { fn(); } catch (e) { error = e; }
    assert(error !== null && (!type || error instanceof type), label);
}
__spriteDraws();

const camera = new Camera({near: 1, far: 50, aspect: 1});
camera.setPosition(0, 2, 10).lookAt(0, 2, 0);
const smoke = new Particles3D.Emitter(new Image("smoke.png"), {capacity: 128, rate: 60, life: [1, 2],
    speed: [1, 2], direction: [0, 1, 0], spread: .4, gravity: [0, .5, 0], drag: .3, size: [.5, 2],
    color: [0x80ffffff, 0x00808080], area: [1, 0, 1], seed: 9});
std.gc(); // the emitter keeps its image
assert(smoke.setPosition(0, 0, 0) === smoke && smoke.count === 0, "fresh emitter");
smoke.update(.5); assert(smoke.count === 30, "rate * dt");
assert(smoke.draw(camera) === 30 && __spriteDraws().particles === 30, "visible particles go to the VU1 batch");
smoke.setPosition(0, 0, 20).emit(10); // behind the camera
assert(smoke.draw(camera) === 30, "particles behind the camera are skipped");
assert(smoke.configure({rate: 0}) === smoke, "partial configure");
smoke.active = false; smoke.update(3); assert(smoke.count === 0, "particles die");

throws(() => new Particles3D.Emitter(new Image("a.png"), {direction: [0, 0, 0]}), RangeError, "zero direction");
throws(() => new Particles3D.Emitter(new Image("a.png"), {spread: 4}), RangeError, "spread up to PI");
throws(() => new Particles3D.Emitter(new Image("a.png"), {gravity: [0, 1]}), TypeError, "gravity has three numbers");
throws(() => smoke.draw({}), TypeError, "camera class");
throws(() => smoke.setPosition(0, 0), TypeError, "three coordinates");

smoke.active = true; smoke.configure({rate: 100});
Particles3D.update(.1); assert(smoke.count === 10, "update() runs every emitter");
Particles3D.attachLoop(); Particles3D.attachLoop();
assert(Particles3D.isAttached() && Particles3D.detachLoop() && !Particles3D.isAttached(), "loop attach/detach");
Particles3D.attachLoop(); // left attached: runtime cleanup must detach it
smoke.dispose(); smoke.dispose();
throws(() => smoke.draw(camera), TypeError, "disposed emitter");
camera.dispose(); std.gc();
console.log("Particles3D tests passed (" + checks + " checks)");
