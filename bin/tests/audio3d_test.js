/*
 * Audio3D module: attenuation curves, pan, the listener, sources playing on
 * voices with levels updated as they move, out-of-range plays and errors.
 * On the host the voices are the runner's stand-in (__fakeSfx/__soundLog);
 * on the console a real ADPCM sample (tests/host/wav2adp/short.loop.adp,
 * copied next to the script as loop.adp) plays on the SPU2.
 */
import * as Audio3D from "Audio3D";
import { Camera } from "Camera3D";

let passed = 0, failed = 0;
function check(name, condition) { if (condition) passed++; else { failed++; console.log("[FAIL] " + name); } }
function throws(name, callback, type) {
    let error = null; try { callback(); } catch (e) { error = e; }
    check(name + " throws" + (type ? " " + type.name : ""), error !== null && (!type || error instanceof type));
}
const host = typeof globalThis.__fakeSfx === "function";

// Listener at the origin looking down -Z: right is +X.
Audio3D.setListener(0, 0, 0, 1, 0, 0);
let l = Audio3D.levels(0, 0, -0.5);
check("inside minDistance: full volume, centred", l.volume === 100 && l.pan === 0 && Math.abs(l.distance - 0.5) < 1e-4);
l = Audio3D.levels(0, 0, -2);
check("inverse rolloff: min / d", l.volume === 50);
l = Audio3D.levels(0, 0, -40);
check("beyond maxDistance: silent", l.volume === 0);
l = Audio3D.levels(0, 0, -10, { rolloff: "linear", minDistance: 0.0001, maxDistance: 20 });
check("linear rolloff", Math.abs(l.volume - 50) <= 1);
check("pan right", Audio3D.levels(5, 0, 0).pan === 80 && Audio3D.levels(-5, 0, 0).pan === -80);
check("panStrength", Audio3D.levels(5, 0, 0, { panStrength: 1 }).pan === 100 && Audio3D.levels(5, 0, 0, { panStrength: 0 }).pan === 0);
check("volume option", Audio3D.levels(0, 0, -0.5, { volume: 40 }).volume === 40);
throws("bad distances", () => Audio3D.levels(0, 0, 0, { minDistance: 5, maxDistance: 2 }), RangeError);
throws("bad rolloff", () => Audio3D.levels(0, 0, 0, { rolloff: "log" }), RangeError);
throws("zero right vector", () => Audio3D.setListener(0, 0, 0, 0, 0, 0), RangeError);

// A camera listener: right follows the view.
const cam = new Camera(); cam.lookAt(1, 0, 0).setPosition(0, 0, 0);   // facing +X: right is +Z
Audio3D.setListener(cam);
check("camera listener", Audio3D.levels(0, 0, 5).pan > 0 && Audio3D.levels(0, 0, -5).pan < 0);
Audio3D.setListener(0, 0, 0, 1, 0, 0);

let sfx = null;
if (host) sfx = __fakeSfx();
else if (globalThis.Sound) { try { sfx = new Sound.Sfx("loop.adp"); } catch (e) { console.log("[SKIP] no sample: " + e.message); } }
throws("needs an Sfx", () => new Audio3D.Source({}), TypeError);
if (sfx) {
    if (host) __soundLog();
    const src = new Audio3D.Source(sfx, { x: 5, y: 0, z: 0, minDistance: 1, maxDistance: 20 });
    check("out of range does not play", new Audio3D.Source(sfx, { x: 50, y: 0, z: 0 }).play() === -1);
    const ch = src.play();
    check("plays on a voice", ch >= 0 && src.playing && src.channel === ch);
    check("initial levels", src.volume === 20 && src.pan === 80);
    if (host) { const log = __soundLog(); check("voice started with the levels", log.plays === 1 && log.levels[0][1] === 20 && log.levels[0][2] === 80); }
    src.setPosition(-2, 0, 0);
    Audio3D.update();
    check("levels follow the source", src.volume === 50 && src.pan === -80);
    if (host) { const log = __soundLog(); check("applied to the voice", log.levels.length === 1 && log.levels[0][0] === ch && log.levels[0][1] === 50); }
    if (host) { __soundEnd(ch); Audio3D.update(); check("ended voices are forgotten", !src.playing); src.play(); }
    src.stop();
    check("stop", !src.playing && src.channel === -1);
    check("force plays out of range", new Audio3D.Source(sfx, { x: 50, y: 0, z: 0 }).play({ force: true }) >= 0);
    src.dispose();
    throws("disposed", () => src.playing, TypeError);
    const reentrant = new Audio3D.Source(sfx);
    throws("configure getter disposes source", () => reentrant.configure({
        get minDistance() { reentrant.dispose(); return 1; },
    }), TypeError);
    if (!host) { std.gc(); }
}
cam.dispose();
console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("Audio3D module test passed");
