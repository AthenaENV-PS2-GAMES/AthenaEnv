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

// ---- E06: voice policy, ownership and occlusion (host: the runner's fake voices) ----
if (host) {
    Audio3D.setListener(0, 0, 0, 1, 0, 0);
    const shot = () => __fakeSfx(), loopSfx = () => __fakeSfx(true);
    const at = (sfx, z, o = {}) => new Audio3D.Source(sfx, { x: 0, y: 0, z: -z, minDistance: 1, maxDistance: 60, ...o });
    const drain = () => { Audio3D.update(); return __soundLog(); };
    const stopAll = list => { for (const s of list) s.stop(); drain(); };
    check("voice limit constant", Audio3D.MAX_VOICES === 24);
    throws("max voices range", () => Audio3D.setMaxVoices(0), RangeError);
    throws("max voices range high", () => Audio3D.setMaxVoices(25), RangeError);
    throws("max voices integer", () => Audio3D.setMaxVoices(2.5), RangeError);
    throws("priority range", () => new Audio3D.Source(shot(), { priority: 5000 }), RangeError);
    throws("cull name", () => new Audio3D.Source(shot(), { cull: "mute" }), RangeError);

    // Priority beats loudness; the lowest rank loses its voice (one-shots stop, loops wait).
    Audio3D.setMaxVoices(2);
    const sA = shot(), sB = shot(), sC = shot();
    const a = at(sA, 2), b = at(sB, 10), c = at(sC, 30, { priority: 5 });
    __soundLog();
    check("two voices fill", a.play() >= 0 && b.play() >= 0 && a.playing && b.playing);
    const chC = c.play();
    check("a higher priority takes the lowest-ranked voice", chC >= 0 && c.playing && !b.playing && a.playing);
    check("a stopped one-shot does not wait", !b.pending);
    let log = __soundLog(); check("exactly the stolen voice was stopped", log.stops === 1);
    stopAll([a, b, c]);

    // Equal rank: the older source keeps the voice (stable tie-break).
    Audio3D.setMaxVoices(1);
    const t1 = at(shot(), 5), t2 = at(shot(), 5);
    check("tie: first source holds", t1.play() >= 0 && t2.play() === -1 && t1.playing && !t2.playing);
    stopAll([t1, t2]);

    // Louder (nearer) wins at equal priority, even when it comes later.
    const far = at(shot(), 20), near = at(shot(), 3);
    far.play(); near.play();
    check("nearer outranks farther", near.playing && !far.playing);
    stopAll([far, near]);

    // Virtual loops: wait without a voice, start when a voice is free, and are re-ranked by distance.
    const l1 = at(loopSfx(), 4), l2 = at(loopSfx(), 12);
    __soundLog();
    check("loop gets the voice", l1.play() >= 0 && l1.playing);
    check("outranked loop waits (virtual)", l2.play() === -1 && l2.pending && !l2.playing);
    l1.stop(); let st = Audio3D.update();
    check("pending loop starts when a voice frees", l2.playing && !l2.pending && st.started === 1);
    l1.play();
    check("nearer loop steals again; the loser is virtual", l1.playing && !l2.playing && l2.pending);
    st = Audio3D.update(); check("stats: one voice, one waiting", st.voices === 1 && st.sources === 2 && st.candidates === 2);
    l1.setPosition(0, 0, -50); l2.setPosition(0, 0, -3);   // the ranks swap as they move
    st = Audio3D.update();
    check("ranks follow movement", l2.playing && l1.pending && st.stolen === 1);
    // Leaving range releases the voice and a virtual loop comes back when it re-enters.
    l2.setPosition(0, 0, -200); Audio3D.update();
    check("out of range: voice released, still pending", !l2.playing && l2.pending);
    l2.setPosition(0, 0, -3); Audio3D.update();
    check("back in range: restarts", l2.playing);
    stopAll([l1, l2]);
    check("stop clears pending", !l1.pending && !l2.pending);

    // cull overrides: a loop that stops, a one-shot that waits.
    Audio3D.setMaxVoices(1);
    const ls = at(loopSfx(), 9, { cull: "stop" }), kv = at(loopSfx(), 2);
    ls.play(); kv.play();
    check("cull stop on a loop: gone for good", !ls.playing && !ls.pending && kv.playing);
    stopAll([ls, kv]);
    const ov = at(shot(), 9, { cull: "virtual" }), nv = at(shot(), 2);
    ov.play(); nv.play();
    check("cull virtual on a one-shot waits", !ov.playing && ov.pending);
    stopAll([ov, nv]);
    Audio3D.setMaxVoices(24);

    // Other Sound users keep their channels, and Audio3D never stops a voice it does not own.
    __soundLog();
    const foreign = shot(), fch = __soundPlay(foreign);
    Audio3D.setMaxVoices(2);
    const f1 = at(shot(), 2), f2 = at(shot(), 4), f3 = at(shot(), 6, { priority: 9 });
    f1.play(); f2.play(); f3.play();
    check("foreign voice untouched while Audio3D steals", __soundLog().stops === 1 && fch >= 0);
    stopAll([f1, f2, f3]);
    Audio3D.setMaxVoices(24);

    // A channel reused by another source must not be controlled by the old one.
    const same = shot();
    const r1 = at(same, 4), r2 = at(same, 4);
    const rc = r1.play(); __soundEnd(rc);
    const rc2 = r2.play();
    check("the finished channel is reused", rc2 === rc);
    __soundLog(); r1.stop();
    check("a stale source does not stop the new voice", r2.playing && __soundLog().stops === 0);
    r1.play({ force: true }); r1.stop(); r2.stop();
    // The sample freed under a playing source: the source forgets it quietly.
    const gone = shot(), g1 = at(gone, 3); g1.play();
    drain(); stopAll([g1]);
    check("stats are reported without throwing", typeof Audio3D.stats().voices === "number");

    // ---- Occlusion: a budget of queries, smoothing, errors, removal ----
    Audio3D.setMaxVoices(24);
    const oc = []; for (let i = 0; i < 6; i++) { const s = at(shot(), 5, { x: i < 3 ? 4 : -4 }); s.play(); oc.push(s); }
    let queries = 0, calls = [];
    Audio3D.setOcclusion((x0, y0, z0, x1) => { calls.push(x1); return x1 > 0 ? 1 : 0; }, { maxQueries: 2, interval: 1, strength: 1, smoothing: 1 });
    let worst = 0;
    for (let f = 0; f < 4; f++) { st = Audio3D.update(); queries += st.queries; worst = Math.max(worst, st.queries); }
    check("query budget per update", worst === 2 && queries === 8 && calls.length === 8);
    check("every voiced source was served in turn", oc.every(s => s.occlusion === 0 || s.occlusion === 1) && oc.slice(0, 3).every(s => s.occlusion === 1) && oc.slice(3).every(s => s.occlusion === 0));
    check("occlusion lowers the level", oc[0].volume === 0 && oc[3].volume > 0);
    st = Audio3D.stats(); check("oldest result stays bounded", st.oldestResult <= 4);
    // Failing provider: counted, previous values kept, nothing thrown.
    Audio3D.setOcclusion(() => { throw new Error("boom"); }, { maxQueries: 6, interval: 1, smoothing: 1 });
    st = Audio3D.update(); check("provider errors are counted", st.queryErrors > 0 && oc[0].occlusion === 1);
    Audio3D.setOcclusion(() => NaN, { maxQueries: 6, interval: 1 });
    st = Audio3D.update(); check("non-finite answers are errors", st.queryErrors > 0);
    Audio3D.setOcclusion(null); Audio3D.update();
    check("occlusion off clears it", oc.every(s => s.occlusion === 0) && Audio3D.update().queries === 0);
    throws("provider type", () => Audio3D.setOcclusion({}), TypeError);
    throws("occlusion options", () => Audio3D.setOcclusion(() => 0, { maxQueries: 0 }), RangeError);
    throws("occlusion strength", () => Audio3D.setOcclusion(() => 0, { strength: 2 }), RangeError);
    // Smoothing eases toward the answer.
    Audio3D.setOcclusion(() => 1, { maxQueries: 6, interval: 1, strength: 1, smoothing: 0.5 });
    Audio3D.update(); const first = oc[0].occlusion; Audio3D.update(); const second = oc[0].occlusion;
    check("smoothing eases in", first > 0 && first < 1 && second > first);
    Audio3D.setOcclusion(null);
    // A Collision3D world as provider: a wall between the listener and the source blocks it.
    if (globalThis.Collision3D) {
        const world = new Collision3D.World(); world.addBox([-1, -5, -8], [1, 5, -7]);
        Audio3D.setOcclusion(world, { maxQueries: 6, interval: 1, strength: 1, smoothing: 1 });
        const behind = at(shot(), 12, { priority: 1 }), beside = at(shot(), 5, { x: 6, z: 0 });
        behind.play(); beside.play(); Audio3D.update(); Audio3D.update();
        check("world provider: wall blocks, open path does not", behind.occlusion === 1 && beside.occlusion === 0);
        behind.stop(); beside.stop(); Audio3D.setOcclusion(null);
        const dead = new Collision3D.World(); dead.dispose();
        throws("disposed world is rejected", () => Audio3D.setOcclusion(dead), TypeError);
        world.dispose();
    }
    stopAll(oc);
    Audio3D.setMaxVoices(24);
}

cam.dispose();
console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("Audio3D module test passed");
