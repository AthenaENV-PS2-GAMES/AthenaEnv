/*
 * Replay module: recording, quantized sources, binary round trip, playback
 * end and file save/load, driving an Input.Map. Runs on the host and on the
 * console (the file goes to the working directory).
 */
import * as Replay from "Replay";
import * as Input from "Input";

let passed = 0, failed = 0;
function check(name, condition) { if (condition) passed++; else { failed++; console.log("[FAIL] " + name); } }
function throws(name, callback, type) {
    let error = null; try { callback(); } catch (e) { error = e; }
    check(name + " throws" + (type ? " " + type.name : ""), error !== null && (!type || error instanceof type));
}
const pads = [{ buttons: 0, leftX: 0, leftY: 0, rightX: 0, rightY: 0 }, { connected: false, buttons: 0xFFFF, leftX: 1, leftY: 1, rightX: 1, rightY: 1 }];
const rec = new Replay.Recorder(pads, { seed: 0xC0FFEE });
const live = new Input.Map({ jump: Input.button(0x4000), move: Input.stick("left", { deadZone: 0 }) }, { source: rec.source(0) });

const script = f => { pads[0].buttons = f % 10 === 0 ? 0x4000 : 0; pads[0].leftX = Math.sin(f / 7); pads[0].leftY = -0.333; };
const liveTrace = [];
for (let f = 0; f < 600; f++) {
    script(f); rec.capture(); live.update();
    liveTrace.push(`${live.justPressed("jump") ? 1 : 0}:${live.x("move").toFixed(6)}`);
}
check("frames", rec.frames === 600);
check("sources are quantized", Math.abs(rec.source(0).leftY * 127 - Math.round(rec.source(0).leftY * 127)) < 1e-9 && rec.source(0).leftY !== -0.333);
check("disconnected source records idle", rec.source(1).buttons === 0 && rec.source(1).leftX === 0);
const bytes = rec.toArrayBuffer();
check("size: header + 6 bytes x players x frames", bytes.byteLength === 16 + 6 * 2 * 600);

const play = new Replay.Playback(bytes);
check("header", play.seed === 0xC0FFEE && play.frames === 600 && play.players === 2);
const replayed = new Input.Map({ jump: Input.button(0x4000), move: Input.stick("left", { deadZone: 0 }) }, { source: play.source(0) });
const trace = [];
while (play.advance()) { replayed.update(); trace.push(`${replayed.justPressed("jump") ? 1 : 0}:${replayed.x("move").toFixed(6)}`); }
check("playback reproduces the live run exactly", trace.length === 600 && trace.join() === liveTrace.join());
check("done", play.done && play.frame === 600 && play.source(0).buttons === 0);
play.restart(); play.advance();
check("restart", play.frame === 1 && play.source(0).buttons === 0x4000);

// Files need std.open (the console runtime; the host runner's std is a stub).
if (typeof std !== "undefined" && typeof std.open === "function") {
    const path = "replay_test.rpl";
    check("save", rec.save(path) === bytes.byteLength);
    const loaded = Replay.load(path);
    check("load", loaded.frames === 600 && loaded.seed === 0xC0FFEE);
} else {
    let error = null; try { rec.save("x.rpl"); } catch (e) { error = e; }
    check("save without std.open explains", error !== null && /std.open/.test(error.message));
}

// Version 1 stays byte for byte as before and flags must be 0 there.
const v1 = new DataView(bytes);
check("without pressure: version 1, flags 0", v1.getUint8(4) === 1 && v1.getUint16(6, true) === 0 && !rec.pressure && !play.pressure && play.version === 1);
check("version 1 source: digital pressure", !play.source(0).hasPressure && play.restart().advance() && play.source(0).pressure(0x4000) === 1 && play.source(0).pressure(0x0200) === 0);
const badFlags = bytes.slice(0); new DataView(badFlags).setUint16(6, 1, true);
throws("version 1 with a flag", () => new Replay.Playback(badFlags), RangeError);
const futureVersion = bytes.slice(0); new DataView(futureVersion).setUint8(4, 3);
throws("future version", () => new Replay.Playback(futureVersion), RangeError);

// Version 2: pressure recorded per button.
const analog = { buttons: 0, leftX: 0, leftY: 0, rightX: 0, rightY: 0, levels: {}, pressure(b) { return (this.buttons & b) ? (this.levels[b] ?? 1) : 0; } };
const plain = { buttons: 0x0200, leftX: 0, leftY: 0, rightX: 0, rightY: 0 };
const prec = new Replay.Recorder([analog, plain, { connected: false, buttons: 0xFFFF }], { seed: 7, pressure: true });
const pmap = new Input.Map({ fire: Input.pressure(0x0200, { threshold: 0.5, release: 0.25 }) }, { source: prec.source(0) });
const plive = [];
for (let f = 0; f < 120; f++) {
    analog.buttons = f % 40 < 30 ? 0x0200 | 0x4000 : 0; analog.levels[0x0200] = (f % 40) / 30; analog.levels[0x4000] = 0.5;
    prec.capture(); pmap.update(); plive.push(`${pmap.justPressed("fire") ? 1 : 0}${pmap.pressed("fire") ? 1 : 0}:${pmap.value("fire").toFixed(6)}`);
}
const pbytes = prec.toArrayBuffer(), pv = new DataView(pbytes);
check("pressure: version 2, flag 1", pv.getUint8(4) === 2 && pv.getUint16(6, true) === 1 && prec.pressure);
check("pressure size: 18 bytes x players x frames", pbytes.byteLength === 16 + 18 * 3 * 120);
check("recorder source is quantized to 1/255", prec.source(0).hasPressure && Math.abs(prec.source(0).pressure(0x0200) * 255 - Math.round(prec.source(0).pressure(0x0200) * 255)) < 1e-9);
check("a source without pressure() records its digital bit", prec.source(1).pressure(0x0200) === 1 && prec.source(1).pressure(0x4000) === 0);
check("disconnected records zero pressure", prec.source(2).pressure(0x0200) === 0);
const pplay = new Replay.Playback(pbytes);
check("pressure header", pplay.version === 2 && pplay.pressure && pplay.players === 3 && pplay.frames === 120 && pplay.source(0).hasPressure);
const pmap2 = new Input.Map({ fire: Input.pressure(0x0200, { threshold: 0.5, release: 0.25 }) }, { source: pplay.source(0) });
const ptrace = [];
while (pplay.advance()) { pmap2.update(); ptrace.push(`${pmap2.justPressed("fire") ? 1 : 0}${pmap2.pressed("fire") ? 1 : 0}:${pmap2.value("fire").toFixed(6)}`); }
check("pressure playback reproduces the live run exactly", ptrace.length === 120 && ptrace.join() === plive.join());
check("pressure released at the end", pplay.source(0).pressure(0x0200) === 0 && pplay.source(0).buttons === 0);
pplay.restart(); pplay.advance();
check("pressure values restart", Math.abs(pplay.source(0).pressure(0x0200) - 0) < 1e-9 && Math.abs(pplay.source(0).pressure(0x4000) - 128/255) < 1e-9);
throws("version 2 truncated", () => new Replay.Playback(pbytes.slice(0, pbytes.byteLength - 1)), RangeError);
const badFlags2 = pbytes.slice(0); new DataView(badFlags2).setUint16(6, 3, true);
throws("version 2 unknown flag", () => new Replay.Playback(badFlags2), RangeError);

throws("bad magic", () => new Replay.Playback(new ArrayBuffer(32)), RangeError);
throws("truncated", () => new Replay.Playback(bytes.slice(0, bytes.byteLength - 1)), RangeError);
throws("not a buffer", () => new Replay.Playback([]), TypeError);
throws("bad seed", () => new Replay.Recorder(pads, { seed: -1 }), RangeError);
throws("no sources", () => new Replay.Recorder([]), RangeError);
const capped = new Replay.Recorder([pads[0]], { maxFrames: 2 });
check("maxFrames", capped.capture() && capped.capture() && !capped.capture() && capped.frames === 2);

console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("Replay module test passed");
