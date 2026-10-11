import * as Render3D from "Render3D";
let checks = 0;
function assert(ok, label) { checks++; if (!ok) throw new Error(label); }
function throws(fn, label) { let r = false; try { fn(); } catch (e) { r = e instanceof RangeError || e instanceof TypeError; } assert(r, label); }
let t = Render3D.getTint(); assert(t.length === 3 && t[0] === 1 && t[1] === 1 && t[2] === 1, "default white");
Render3D.setTint(.5, .25, 0); t = Render3D.getTint(); assert(t[0] === .5 && t[1] === .25 && t[2] === 0, "set/get");
for (const bad of [[-.1,0,0],[1.1,0,0],[NaN,0,0],[0,Infinity,0]]) throws(() => Render3D.setTint(...bad), "range "+bad);
throws(() => Render3D.setTint("1",0,0), "strict numbers");
t = Render3D.getTint(); assert(t[0] === .5 && t[1] === .25 && t[2] === 0, "failed set is atomic");
Render3D.setTint(1, 1, 1);
console.log("TINT_TEST: PASS " + checks + " checks");
