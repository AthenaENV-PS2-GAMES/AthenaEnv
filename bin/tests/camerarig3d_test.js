import {Matrix4} from "Matrix4";
import {Camera} from "Camera3D";
import * as Scene3D from "Scene3D";
import * as CameraRig3D from "CameraRig3D";
let checks = 0;
function assert(ok, label) { checks++; if (!ok) throw new Error(label); }
function throws(fn, type, label) {
    let error = null; try { fn(); } catch (e) { error = e; }
    assert(error !== null && (!type || error instanceof type), label);
}
function close(a, b) { return Math.abs(a - b) < 1e-3; }
// The view matrix places the eye: its inverse translation is the position.
function eye(camera) {
    const v = camera.getView(new Matrix4());
    const tx = v.get(12), ty = v.get(13), tz = v.get(14);
    return [-(v.get(0) * tx + v.get(1) * ty + v.get(2) * tz), -(v.get(4) * tx + v.get(5) * ty + v.get(6) * tz),
        -(v.get(8) * tx + v.get(9) * ty + v.get(10) * tz)];
}
function eyeIs(camera, x, y, z, label) { const e = eye(camera); assert(close(e[0], x) && close(e[1], y) && close(e[2], z), label); }

const scene = new Scene3D.Scene(), root = scene.root, hero = new Scene3D.Node();
root.add(hero); root.dispose();
hero.setPosition(10, 0, 0).setRotationEuler(0, Math.PI / 2, 0); scene.update();

const camera = new Camera();
const follow = new CameraRig3D.Follow(camera, hero);
assert(follow.setOffset(0, 2, 6).setLookOffset(0, 1, 0) === follow, "chainable setters");
assert(follow.update(1 / 60) === true, "follow moves the camera");
eyeIs(camera, 16, 2, 0, "local offset turns with the target");
follow.setOffset(0, 2, 6, false).snap().update(0);
eyeIs(camera, 10, 2, 6, "world offset");
hero.setPosition(20, 0, 0);
assert(follow.update(1) === false, "stale target skips the frame");
scene.update();
follow.setSharpness(1, 0).update(1);
assert(close(eye(camera)[0], 10 + 10 * (1 - Math.exp(-1))), "exponential smoothing");
follow.enabled = false; assert(follow.update(1) === false && follow.enabled === false, "disabled");
follow.enabled = true;
throws(() => follow.setSharpness(-1, 0), RangeError, "negative sharpness");
throws(() => follow.setOffset(0, 0, 0, 1), TypeError, "boolean local flag");
throws(() => new CameraRig3D.Follow(camera, null), TypeError, "follow needs a target");
throws(() => new CameraRig3D.Follow({}, hero), TypeError, "camera class");

// Orbit: fixed centre, limits, input and auto-rotation.
const orbitCamera = new Camera();
const orbit = new CameraRig3D.Orbit(orbitCamera);
orbit.setCenter(0, 1, 0); orbit.distance = 5; orbit.update(0);
eyeIs(orbitCamera, 0, 1, 5, "yaw 0 sits on +Z");
orbit.setLimits(-.5, .5, 2, 8).rotate(0, 3).zoom(100);
assert(close(orbit.pitch, .5) && close(orbit.distance, 8), "limits clamp input");
orbit.setAngles(0, 0); orbit.autoRotate = 1; orbit.snap().update(.5);
assert(close(orbit.yaw, .5), "auto-rotation");
throws(() => orbit.setLimits(-2, 0, 1, 1), RangeError, "pitch limit range");
throws(() => CameraRig3D.Orbit.prototype.setCenter.call(follow, 0, 0, 0), TypeError, "orbit-only method");
orbit.autoRotate = 0; orbit.setTarget(hero).snap().update(0);

// A rig nobody holds keeps working until dispose().
(function () { new CameraRig3D.Orbit(new Camera()).setCenter(0, 0, 0); })();
std.gc();
assert(CameraRig3D.update(0) === 3, "unheld rigs stay active");
// The rig keeps a disposed camera alive.
orbitCamera.dispose();
assert(orbit.update(0) === true, "rig keeps its camera after dispose");
assert(CameraRig3D.update(0) === 3, "update() runs every rig");
CameraRig3D.attachLoop(); CameraRig3D.attachLoop();
assert(CameraRig3D.isAttached() && CameraRig3D.detachLoop() && !CameraRig3D.isAttached(), "loop attach/detach");
assert(CameraRig3D.LOOP_PRIORITY > 0, "after the scene update");
CameraRig3D.attachLoop(); // left attached: runtime cleanup must detach it

follow.dispose(); follow.dispose(); orbit.dispose();
throws(() => follow.snap(), TypeError, "disposed rig");
hero.dispose(); scene.dispose(); camera.dispose();
std.gc();
console.log("CameraRig3D tests passed (" + checks + " checks)");
