import {Matrix4} from "Matrix4";
import {Camera} from "Camera3D";
import * as Model3D from "Model3D";
import * as Scene3D from "Scene3D";
import * as Tween3D from "Tween3D";
let checks = 0;
function assert(ok, label) { checks++; if (!ok) throw new Error(label); }
function throws(fn, type, label) {
    let error = null; try { fn(); } catch (e) { error = e; }
    assert(error !== null && (!type || error instanceof type), label);
}
function close(a, b) { return Math.abs(a - b) < 1e-3; }
function x(node) { return node.getLocalTransform(new Matrix4()).get(12); }

async function run() {
    const node = new Scene3D.Node();
    // Linear move, awaited: true on completion.
    const move = Tween3D.to(node, {position: [10, 0, 0]}, 1, {ease: "linear"});
    assert(move.active && !move.paused && move.progress === 0, "fresh handle");
    Tween3D.advance(.5); assert(close(x(node), 5) && close(move.progress, .5), "half way");
    move.pause(); Tween3D.advance(1); assert(close(x(node), 5) && move.paused, "paused");
    move.resume(); Tween3D.advance(.6);
    assert(close(x(node), 10) && !move.active, "exact end");
    assert(await move === true, "await resolves true when completed");
    assert(await move.finished === true, "finished promise");

    // Killed: resolves false; kill(true) applies the end.
    const killed = Tween3D.to(node, {position: [0, 0, 0]}, 1);
    Tween3D.advance(.1); killed.kill();
    assert(await killed === false, "await resolves false when killed");
    const completed = Tween3D.to(node, {position: [3, 0, 0]}, 1, {ease: "easeOutBack"});
    completed.kill(true); assert(close(x(node), 3) && await completed === true, "kill(true) completes");

    // Options: delay, yoyo + repeat, overwrite, Infinity, isTweening/killTweensOf.
    node.setPosition(0, 0, 0);
    const yoyo = Tween3D.to(node, {position: [8, 0, 0]}, 1, {ease: "linear", delay: .5, yoyo: true, repeat: 1});
    Tween3D.advance(.5); assert(close(x(node), 0), "delayed");
    Tween3D.advance(1.25); assert(close(x(node), 6), "yoyo runs back");
    Tween3D.advance(1); assert(!yoyo.active && close(x(node), 0), "yoyo ends at the start");
    const forever = Tween3D.to(node, {position: [1, 0, 0]}, .5, {repeat: Infinity});
    Tween3D.advance(100); assert(forever.active && Tween3D.isTweening(node), "repeat Infinity");
    const winner = Tween3D.to(node, {scale: [2, 2, 2]}, 1, {overwrite: true});
    Tween3D.advance(.1); assert(!forever.active && winner.active, "overwrite");
    assert(await forever === false, "overwritten tweens resolve false");
    assert(Tween3D.killTweensOf(node, true) === 1 && !Tween3D.isTweening(node), "killTweensOf");

    // Instance rotation and camera eye/look.
    const mesh = Model3D.Mesh.fromGeometry({positions: new Float32Array([0, 0, 0, 1, 0, 0, 0, 1, 0])});
    const instance = mesh.createInstance(); mesh.dispose();
    Tween3D.to(instance, {rotation: [0, Math.PI / 2, 0]}, 1, {ease: "linear"});
    instance.dispose(); // the tween keeps it
    Tween3D.advance(1);
    const camera = new Camera();
    const pan = Tween3D.to(camera, {position: new Float32Array([0, 0, 15]), target: [0, 0, -1]}, 1, {ease: "linear"});
    camera.dispose(); // and the camera
    Tween3D.advance(.5); assert(pan.active, "camera tween survives dispose");
    Tween3D.advance(.5); assert(!pan.active && await pan === true, "camera tween ends");

    // Validation.
    throws(() => Tween3D.to({}, {position: [0, 0, 0]}, 1), TypeError, "native targets only");
    throws(() => Tween3D.to(node, {position: [0, 0]}, 1), TypeError, "three numbers");
    throws(() => Tween3D.to(node, {target: [0, 0, 0]}, 1), RangeError, "target is camera-only");
    throws(() => Tween3D.to(node, {position: [0, 0, 0]}, -1), RangeError, "negative duration");
    throws(() => Tween3D.to(node, {position: [0, 0, 0]}, 1, {ease: "wobble"}), RangeError, "unknown curve");
    throws(() => Tween3D.to(node, {position: [0, 0, 0]}, 1, {repeat: 1.5}), RangeError, "integer repeat");
    throws(() => Tween3D.to(node, {position: [0, 0, 0]}, 1, {yoyo: 1}), TypeError, "boolean yoyo");
    throws(() => Tween3D.advance(-1), RangeError, "negative dt");

    // Loop system; a tween left running is ended by runtime cleanup.
    Tween3D.attachLoop(); Tween3D.attachLoop();
    assert(Tween3D.isAttached() && Tween3D.detachLoop() && !Tween3D.isAttached(), "loop attach/detach");
    Tween3D.attachLoop();
    Tween3D.to(node, {position: [5, 5, 5]}, 10);
    node.dispose();
    std.gc();
    console.log("Tween3D tests passed (" + checks + " checks)");
}
run().catch(e => console.log("Tween3D test failed: " + e + "\n" + e.stack));
