/*
 * Sky module: colors by elevation, time-of-day keyframes, sun, applying to
 * Lights, the clear color and the screen-space bands.
 */
import * as Sky from "Sky";
import * as Lights from "Lights";
import { Camera } from "Camera3D";

if (globalThis.Screen && typeof globalThis.__nativeDraws !== "function") {
    const mode = Screen.getMode(); mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
}
let passed = 0, failed = 0;
function check(name, condition) { if (condition) passed++; else { failed++; console.log("[FAIL] " + name); } }
function throws(name, callback, type) {
    let error = null; try { callback(); } catch (e) { error = e; }
    check(name + " throws" + (type ? " " + type.name : ""), error !== null && (!type || error instanceof type));
}
const host = typeof globalThis.__nativeDraws === "function";
const close = (a, b) => Math.abs(a - b) < 1e-3;
const same = (a, b) => a.every((v, i) => close(v, b[i]));

// First use must not be overwritten by lazy defaults; repeated runtimes test it too.
Sky.setTime(6);
check("setTime survives first state access", close(Sky.state().time, 6));

Sky.setColors({ zenith: [0, 0, 1], horizon: [1, 1, 1], ground: [0, 1, 0] });
check("horizon", same(Sky.colorAt(0), [1, 1, 1]));
check("zenith", same(Sky.colorAt(Math.PI / 2), [0, 0, 1]));
check("ground", same(Sky.colorAt(-1), [0, 1, 0]));
const mid = Sky.colorAt(Math.PI / 8);
check("blend between", mid[2] === 1 && mid[0] < 1 && mid[0] > 0);
check("clear color is the horizon", Sky.clearColor() === 0x80FFFFFF);
throws("color range", () => Sky.setColors({ zenith: [2, 0, 0] }), RangeError);

Sky.setTime(12);
let s = Sky.state();
check("noon: sun high, bright", s.sunDirection[1] > 0.9 && s.light[0] > 0.9 && s.zenith[2] > s.zenith[0]);
Sky.setTime(6.2);
s = Sky.state();
check("dawn: sun low in +X, warm horizon", s.sunDirection[0] > 0.9 && s.sunDirection[1] < 0.2 && s.horizon[0] > s.horizon[2]);
Sky.setTime(24 + 1);
s = Sky.state();
check("night wraps and dims", close(s.time, 1) && s.ambient[0] < 0.2 && s.lightDirection[1] > 0);
check("night has no sun disc", s.sunColor[0] === 0);

const lights = new Lights.Set().setFog(10, 50, 0, 0, 0);
Sky.setTime(12); Sky.apply(lights);
check("apply changes the lights", lights.revision > 0);
throws("apply needs lights", () => Sky.apply({}), TypeError);
Sky.setSun(0, 1, 0, { color: [1, 1, 0], size: 30 });
check("setSun", same(Sky.state().sunDirection, [0, 1, 0]) && same(Sky.state().sunColor, [1, 1, 0]));
throws("zero sun", () => Sky.setSun(0, 0, 0), RangeError);

// Bands: looking up gives zenith-ish rows, a horizon line splits sky and ground.
const camera = new Camera({ aspect: 640 / 448, near: 0.1, far: 300 });
camera.lookAt(0, 2, -10).setPosition(0, 2, 0);
Sky.setColors({ zenith: [0, 0, 1], horizon: [1, 1, 1], ground: [0, 1, 0] });
Sky.setSun(0, 0.2, -1, { color: [1, 0.9, 0.6] });   // in front of the camera
if (host) __nativeDraws();
check("draw returns bands", Sky.draw(camera, 8) === 8);
if (host) {
    const d = __nativeDraws();
    check("one gouraud quad per band and the sun", d.quads === 8 && d.circles === 2);
    check("last band top is below the horizon: greenish", (d.lastQuadTop & 0xff00) > (d.lastQuadTop & 0xff) * 256);
}
throws("bands range", () => Sky.draw(camera, 0), RangeError);
camera.lookAt(0, -10, -0.1);   // looking down: no sun drawn
Sky.setSun(0, 1, 0, { color: [1, 1, 1] });
if (host) { __nativeDraws(); Sky.draw(camera); check("sun behind the camera is not drawn", __nativeDraws().circles === 0); }
camera.dispose(); lights.dispose();
console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("Sky module test passed");
