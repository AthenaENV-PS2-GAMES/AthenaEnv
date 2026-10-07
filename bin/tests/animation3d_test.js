import {Matrix4} from "Matrix4";
import * as Scene3D from "Scene3D";
import * as Animation3D from "Animation3D";
let checks = 0;
function assert(ok, label) { checks++; if (!ok) throw new Error(label); }
function throws(fn, type, label) {
    let error = null; try { fn(); } catch (e) { error = e; }
    assert(error !== null && (!type || error instanceof type), label);
}
function close(a, b) { return Math.abs(a - b) < 1e-4; }
function x(node) { return node.getLocalTransform().get(12); }
void Matrix4; // the import installs the Matrix4 methods used above

const times = new Float32Array([0, 1, 2]);
const positions = new Float32Array([0, 0, 0, 10, 0, 0, 10, 20, 0]);
const s = Math.sqrt(.5);
const clip = new Animation3D.Clip([
    {path: "position", times, values: positions},
    {target: 1, path: "rotation", times: new Float32Array([0, 1]), values: new Float32Array([0, 0, 0, 1, 0, s, 0, s])},
    {target: 1, path: "scale", interpolation: "step", times: new Float32Array([0, .5]),
        values: new Float32Array([1, 1, 1, 2, 2, 2])}]);
positions.fill(99); // the clip owns a copy
assert(close(clip.duration, 2) && clip.targetCount === 2, "clip duration and targets");

// Validation.
throws(() => new Animation3D.Clip([]), RangeError, "empty clip");
throws(() => new Animation3D.Clip({}), TypeError, "tracks array");
throws(() => new Animation3D.Clip([{path: "spin", times, values: positions}]), TypeError, "path name");
throws(() => new Animation3D.Clip([{path: "position", times, values: new Float32Array(8)}]), RangeError, "values per key");
throws(() => new Animation3D.Clip([{path: "position", times: new Float32Array([0, 1, 1]), values: positions}]),
    RangeError, "increasing times");
throws(() => new Animation3D.Clip([{path: "position", times: [0], values: [0, 0, 0]}]), TypeError, "Float32Array only");
throws(() => new Animation3D.Clip([{target: 1.5, path: "position", times, values: positions}]), RangeError, "integer target");
// A getter that detaches an earlier track's buffer cannot corrupt the copy.
const early = new Float32Array([0, 1]), earlyValues = new Float32Array([0, 0, 0, 5, 0, 0]);
const late = {path: "scale", times: new Float32Array([0]), get values() { earlyValues.buffer.transfer?.(); return new Float32Array([1, 1, 1]); }};
const safe = new Animation3D.Clip([{path: "position", times: early, values: earlyValues}, late]);
assert(close(safe.duration, 1), "tracks are copied as they are read"); safe.dispose();

const a = new Scene3D.Node(), b = new Scene3D.Node();
throws(() => new Animation3D.Player(clip, [a]), RangeError, "player needs every target");
throws(() => new Animation3D.Player(clip, [a, {}]), TypeError, "nodes only");
const player = new Animation3D.Player(clip, [a, b]);
clip.dispose(); b.dispose(); // the player keeps both
assert(!player.playing && player.time === 0 && player.speed === 1 && player.loop === false, "initial state");
assert(player.advance(1) === false && x(a) === 0, "paused players do nothing");
assert(player.play() === player && player.playing, "play");
assert(player.advance(.5) === false && close(x(a), 5), "linear position");
assert(player.advance(5) === true && !player.playing && close(player.time, 2), "non-looping end");
player.time = .25; assert(close(x(a), 2.5), "seeking applies the pose");
player.loop = true; player.play().advance(2); assert(close(player.time, .25), "loop wraps");
player.speed = -1; player.advance(.5); assert(close(player.time, 1.75), "backwards wraps");
player.stop(); assert(!player.playing && close(player.time, 2), "stop rewinds to the end when reversed");
throws(() => { player.speed = NaN; }, RangeError, "finite speed");
throws(() => { player.loop = 1; }, TypeError, "boolean loop");
throws(() => player.advance(-1), RangeError, "negative dt");

// A player nobody holds keeps playing until runtime cleanup.
const loose = new Scene3D.Node();
(function () { const p = new Animation3D.Player(new Animation3D.Clip([{path: "position", times, values: new Float32Array([0,0,0, 4,0,0, 4,0,0])}]), [loose]); p.play(); })();
std.gc();
Animation3D.advance(.5);
assert(close(x(loose), 2), "unheld players keep playing");
// Module-wide advance and Loop attachment.
player.speed = 1; player.stop(); player.play();
assert(Animation3D.advance(1) === 0 && close(x(a), 10), "advance() moves every player");
Animation3D.attachLoop(); Animation3D.attachLoop();
assert(Animation3D.isAttached(), "attached once");
assert(Animation3D.detachLoop() === true && !Animation3D.isAttached() && Animation3D.detachLoop() === false, "detach");
assert(typeof Animation3D.LOOP_PRIORITY === "number" && Animation3D.LOOP_PRIORITY < 0, "priority before Scene3D");
Animation3D.attachLoop(); // left attached: runtime cleanup must detach it

player.dispose(); player.dispose();
throws(() => player.play(), TypeError, "disposed player");
throws(() => clip.duration, TypeError, "disposed clip");
a.dispose();
std.gc();
console.log("Animation3D tests passed (" + checks + " checks)");
