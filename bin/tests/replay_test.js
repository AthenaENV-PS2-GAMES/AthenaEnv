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

throws("bad magic", () => new Replay.Playback(new ArrayBuffer(32)), RangeError);
throws("truncated", () => new Replay.Playback(bytes.slice(0, bytes.byteLength - 1)), RangeError);
throws("not a buffer", () => new Replay.Playback([]), TypeError);
throws("bad seed", () => new Replay.Recorder(pads, { seed: -1 }), RangeError);
throws("no sources", () => new Replay.Recorder([]), RangeError);
const capped = new Replay.Recorder([pads[0]], { maxFrames: 2 });
check("maxFrames", capped.capture() && capped.capture() && !capped.capture() && capped.frames === 2);

console.log(`Result: ${passed} passed, ${failed} failed`);
if (!failed) console.log("Replay module test passed");
