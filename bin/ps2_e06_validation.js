/* E06 validation for a physical PS2 (also runs on PCSX2): Audio3D voice policy, stolen/reused channels and occlusion.
 *
 * Writes `e06_ps2_validation.log` next to the ELF, rewritten after every phase, and `e06_ps2_results.json`.
 * Phases: FUNCTIONAL (PASS/FAIL checks with the real SPU2 voices), COST (Audio3D.update() in COP0 ms for
 * several source counts), AUDIO (5 listening stages of 14 s with an on-screen label: describe what you
 * hear in your notes). CROSS pauses the stage timer, START ends the run.
 * Needs `loop.adp` (tests/host/wav2adp/short.loop.adp), `pop.adp` (short.adp), `tone.adp` (rate16k.loop.adp)
 * next to the ELF. Optional `ps2_e06_config.json`: { "skip": ["cost"] }, { "stageSeconds": 14 }, { "quick": true }. */
import * as Loop from "Loop";
import { ticks, ticksToMilliseconds } from "ProfilerNative";

const LOG = "e06_ps2_validation.log", JSON_OUT = "e06_ps2_results.json";
let config = {};
try { const text = std.loadFile("ps2_e06_config.json"); if (text) config = JSON.parse(text); } catch (_) { /* optional */ }
const quick = !!config.quick, skip = new Set(config.skip ?? []);
const STAGE_SECONDS = config.stageSeconds ?? (quick ? 6 : 14);
/* Monotonic ms from the COP0 counter (2.5 us per call; now() costs 7 us and its clock is not reliable on the EE).
 * The counter wraps every 14.5 s: call now() at least that often (every frame does). */
let clockMs = 0, lastTick = ticks();
function now() { const t = ticks(); clockMs += ticksToMilliseconds((t - lastTick) >>> 0); lastTick = t; return clockMs; }
const spin = ms => { const end = now() + ms; while (now() < end) { /* wait */ } };
const D0 = 0, lines = [], results = { version: 1, checks: [], cost: [], stages: [] };
function log(text) { const line = `[${((now() - D0) / 1000).toFixed(3)}] ${text}`; lines.push(line); console.log(line); }
let flushFailed = false;
function writeFile(path, text) {
    const f = std.open(path, "w");
    if (!f) throw new Error(`cannot open ${path}`);
    try { f.puts(text); f.flush(); if (f.error()) throw new Error(`cannot write ${path}`); } finally { f.close(); }
}
function flush() {
    try { writeFile(LOG, lines.join("\n") + "\n"); writeFile(JSON_OUT, JSON.stringify(results)); }
    catch (e) { if (!flushFailed) console.log("LOG WRITE FAILED: " + e); flushFailed = true; }
}
let failures = 0;
function check(name, ok, detail = "") { if (!ok) failures++; results.checks.push({ name, ok: !!ok }); log(`${ok ? "PASS" : "FAIL"} ${name}${detail ? " (" + detail + ")" : ""}`); }
function throws(fn, type) { try { fn(); } catch (e) { return e instanceof type; } return false; }

const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
log(`SECTION SYSTEM mode=${mode.width}x${height}`);
try { log("SYSTEM memory=" + JSON.stringify(System.getMemoryStats())); } catch (_) { /* optional */ }
const font = new Font();
const label = (text, y) => font.print(16, y, text);

const loopSfx = new Sound.Sfx("loop.adp"), popSfx = new Sound.Sfx("pop.adp"), toneSfx = new Sound.Sfx("tone.adp");
log(`SAMPLES loop=${loopSfx.loop}/${loopSfx.length}ms pop=${popSfx.loop}/${popSfx.length}ms tone=${toneSfx.loop}/${toneSfx.length}ms`);
Audio3D.setListener(0, 0, 0, 1, 0, 0);
const src = (sfx, x, y, z, o = {}) => new Audio3D.Source(sfx, { x, y, z, minDistance: 1, maxDistance: 40, ...o });

/* ---- FUNCTIONAL ---- */
log("SECTION FUNCTIONAL");
function functional() {
    Audio3D.setMaxVoices(2);
    const a = src(loopSfx, 0, 0, -2), b = src(loopSfx, 0, 0, -12), c = src(loopSfx, 0, 0, -30, { priority: 5 });
    check("two loops take two voices", a.play() >= 0 && b.play() >= 0 && a.playing && b.playing);
    const cc = c.play();
    check("higher priority steals the lowest-ranked voice", cc >= 0 && c.playing && a.playing && !b.playing && b.pending, `channel=${cc}`);
    let st = Audio3D.update(); check("update keeps two voices", st.voices === 2 && st.sources === 3, JSON.stringify(st));
    a.setPosition(0, 0, -50); b.setPosition(0, 0, -3); st = Audio3D.update();
    check("out of range releases a voice", !a.playing && st.voices <= 2);
    spin(400);
    st = Audio3D.update();
    check("pending loop starts when a voice frees", b.playing, JSON.stringify(st));
    a.stop(); b.stop(); c.stop(); spin(300); Audio3D.update();
    Audio3D.setMaxVoices(24);
    // Another Sound user keeps its channel while Audio3D churns.
    const foreign = new Sound.Sfx("tone.adp"), fch = foreign.play();
    Audio3D.setMaxVoices(2);
    const q = [src(loopSfx, 0, 0, -2), src(loopSfx, 0, 0, -6), src(loopSfx, 0, 0, -9, { priority: 3 })];
    for (const s of q) s.play(); Audio3D.update(); Audio3D.update();
    check("foreign voice survives stealing", fch >= 0 && foreign.playing(fch), `channel=${fch}`);
    for (const s of q) s.stop(); foreign.stop(); foreign.free();
    spin(300); Audio3D.setMaxVoices(24);
    // Occlusion API.
    check("occlusion provider type is validated", throws(() => Audio3D.setOcclusion({}), TypeError));
    check("occlusion options are validated", throws(() => Audio3D.setOcclusion(() => 0, { maxQueries: 0 }), RangeError));
    let calls = 0; Audio3D.setOcclusion(() => { calls++; return 1; }, { maxQueries: 1, interval: 1, strength: 1, smoothing: 1 });
    const o = [src(loopSfx, 3, 0, 0), src(loopSfx, -3, 0, 0), src(loopSfx, 0, 0, -3)]; for (const s of o) s.play();
    for (let i = 0; i < 3; i++) st = Audio3D.update();
    check("occlusion budget: one query per update", calls === 3 && st.queries === 1, `calls=${calls}`);
    Audio3D.setOcclusion(null); for (const s of o) { s.stop(); s.dispose(); } spin(300); Audio3D.update();
    for (const s of [a, b, c]) s.dispose();
    flush();
}

/* ---- STALE CHANNEL (needs time to pass: counted in frames) ---- */
let stale = null;
function staleStep() {
    if (!stale) {
        Audio3D.setMaxVoices(24);
        stale = { p1: src(popSfx, 0, 0, -3), p2: src(popSfx, 0, 0, -3), frames: 0, stage: 0, blockers: [] };
        // Foreign loops hold the lowest channels, so the freed one is the first idle channel again.
        for (let ch = 0; ch < 6; ch++) { const f = new Sound.Sfx("tone.adp"); f.play(ch); stale.blockers.push(f); }
        stale.ch1 = stale.p1.play(); log(`STALE p1 started on channel ${stale.ch1} (length ${popSfx.length} ms)`);
        return false;
    }
    const w = stale; w.frames++;
    if (w.stage === 0 && (!popSfx.playing(w.ch1) || w.frames > 600)) {
        log(`STALE one-shot ${popSfx.playing(w.ch1) ? "STILL PLAYING" : "ended"} after ${w.frames} frames`);
        w.stage = 1; w.at = w.frames; return false;
    }
    if (w.stage === 1 && w.frames > w.at + 2) {   // the Loop system has run Audio3D's update since
        const ended = !popSfx.playing(w.ch1);
        check("Audio3D forgets a one-shot that ended", ended ? !w.p1.playing : w.p1.playing, `source.playing=${w.p1.playing}`);
        w.ch2 = w.p2.play(); w.reused = w.ch2 === w.ch1;
        check("second one-shot plays", w.ch2 >= 0 && w.p2.playing, `channel=${w.ch2} reusedChannel=${w.reused}`);
        w.p1.stop();   // p1's voice is gone: must never mute p2's
        check("stale source does not stop the reused channel", w.p2.playing && popSfx.playing(w.ch2), `reused=${w.reused}`);
        w.p2.stop(); w.p1.dispose(); w.p2.dispose(); for (const f of w.blockers) { f.stop(); f.free(); } stale = null; flush(); return true;
    }
    return false;
}

/* ---- COST ---- */
const COST_WARM = 20, COST_FRAMES = quick ? 60 : 120;
const costCases = [
    { name: "24v-static", n: 24, voices: 24, moving: false }, { name: "24v-moving", n: 24, voices: 24, moving: true },
    { name: "200src-8v-static", n: 200, voices: 8, moving: false }, { name: "200src-8v-moving", n: 200, voices: 8, moving: true },
    { name: "24v-occlusion-fn", n: 24, voices: 24, moving: false, occ: true },
];
let costIdx = 0, costFrame = 0, costSum = 0, costMax = 0, costN = 0, costStat = null, costSources = null, costAngle = 0;
function costSetup(c) {
    Audio3D.setMaxVoices(c.voices);
    costSources = [];
    for (let i = 0; i < c.n; i++) { const s = src(loopSfx, ((i * 7) % 29) - 14, 0, -2 - (i % 30), { priority: i % 3 }); s.play(); costSources.push(s); }
    if (c.occ) Audio3D.setOcclusion((x0, y0, z0, x1, y1, z1) => (z1 < -10 ? 0.5 : 0), { maxQueries: 4, interval: 6 });
    costFrame = 0; costSum = 0; costMax = 0; costN = 0; costAngle = 0; costStat = { stolen: 0, started: 0, queries: 0, culled: 0 };
}
function costTeardown(c) {
    Audio3D.setOcclusion(null);
    for (const s of costSources) { s.stop(); s.dispose(); }
    costSources = null; Audio3D.setMaxVoices(24); Audio3D.update();
}
function costFrameStep(c) {
    if (c.moving) { costAngle += 0.05; for (let i = 0; i < costSources.length; i++) costSources[i].setPosition(((i * 7) % 29) - 14 + Math.sin(costAngle + i) * 3, 0, -2 - (i % 30) - Math.cos(costAngle + i) * 3); }
    const start = ticks(); const st = Audio3D.update(); const dt = ticksToMilliseconds((ticks() - start) >>> 0);
    if (++costFrame > COST_WARM) { costSum += dt; costN++; if (dt > costMax) costMax = dt; for (const k in costStat) costStat[k] += st[k]; }
}

/* ---- AUDIO stages ---- */
const world = new Collision3D.World();
const stageDefs = [
    { name: "PAN-ORBIT", expect: "A steady tone circles the listener every 8 s: left -> centre -> right -> centre. Volume constant, no clicks.",
      setup() { this.s = src(loopSfx, 0, 0, -6, { minDistance: 8, maxDistance: 20 }); this.s.play(); },
      tick(t) { const a = t * Math.PI * 2 / 8; this.s.setPosition(Math.sin(a) * 6, 0, -Math.cos(a) * 6); }, end() { this.s.stop(); this.s.dispose(); } },
    { name: "DISTANCE", expect: "A tone recedes from 1 to 40 units and returns (period 14 s): fades out smoothly to silence, no pop at the cut, centred.",
      setup() { this.s = src(loopSfx, 0, 0, -1, { maxDistance: 40 }); this.s.play(); },
      tick(t) { const d = 1 + 39 * (0.5 - 0.5 * Math.cos(t * Math.PI * 2 / STAGE_SECONDS)); this.s.setPosition(0, 0, -d); this.d = d; },
      end() { this.s.stop(); this.s.dispose(); } },
    { name: "PRIORITY-STEAL", expect: "Only 3 voices for 6 tones at different distances (nearer = louder). Every 3 s a different tone gets priority 10 and joins, the quietest drops out. Never more than 3 tones audible; the log shows voices<=3.",
      setup() { Audio3D.setMaxVoices(3); this.list = []; for (let i = 0; i < 6; i++) { const s = src(loopSfx, (i % 2 ? 1 : -1) * (2 + i), 0, -(3 + i * 2), { maxDistance: 60 }); s.play(); this.list.push(s); } this.hi = -1; },
      tick(t, st) { const k = (Math.floor(t / 3) + 3) % 6; if (k !== this.hi) { if (this.hi >= 0) this.list[this.hi].configure({ priority: 0 }); this.list[k].configure({ priority: 10 }); this.hi = k; }
          this.max = Math.max(this.max || 0, st.voices); this.stolen = (this.stolen || 0) + st.stolen; },
      end() { log(`STEAL maxVoicesHeld=${this.max} stolenTotal=${this.stolen}`); for (const s of this.list) { s.stop(); s.dispose(); } Audio3D.setMaxVoices(24); } },
    { name: "OCCLUSION", expect: "A tone behind a wall: quiet (muffled by volume only) for 5 s, the wall disappears and it swells in about half a second, then the wall returns. No clicks. Log: queries <= 2 per update.",
      setup() { this.wall = world.addBox([-2, -5, -6], [2, 5, -5]); this.on = true; this.s = src(loopSfx, 0, 0, -10, { minDistance: 4, maxDistance: 40 }); this.s.play();
          Audio3D.setOcclusion(world, { maxQueries: 2, interval: 4, strength: 0.85, smoothing: 0.2 }); },
      tick(t, st) { const on = Math.floor(t / 5) % 2 === 0; if (on !== this.on) { if (on) this.wall = world.addBox([-2, -5, -6], [2, 5, -5]); else world.remove(this.wall); this.on = on; }
          this.q = Math.max(this.q || 0, st.queries); this.occ = this.s.occlusion; },
      end() { log(`OCCLUSION maxQueriesPerUpdate=${this.q}`); Audio3D.setOcclusion(null); this.s.stop(); this.s.dispose(); if (this.on) world.remove(this.wall); } },
    { name: "FOREIGN-VOICE", expect: "A different, higher-pitched tone plays continuously (not part of Audio3D) while 5 Audio3D tones compete for 2 voices: the high tone must never stop or change volume.",
      setup() { this.f = new Sound.Sfx("tone.adp"); this.fch = this.f.play(); Audio3D.setMaxVoices(2); this.list = []; for (let i = 0; i < 5; i++) { const s = src(loopSfx, i - 2, 0, -(2 + i * 3), { maxDistance: 60 }); s.play(); this.list.push(s); } },
      tick(t, st) { if (Math.floor(t * 2) !== this.last) { this.last = Math.floor(t * 2); this.list[this.last % 5].configure({ priority: this.last % 3 }); this.alive = (this.alive ?? true) && this.f.playing(this.fch); } },
      end() { log(`FOREIGN stillPlaying=${this.alive} channel=${this.fch}`); for (const s of this.list) { s.stop(); s.dispose(); } this.f.stop(); this.f.free(); Audio3D.setMaxVoices(24); } },
];

let phase = skip.has("functional") ? "cost" : "functional", stage = -1, stageStart = 0, stageFrames = 0, paused = false, pausedAt = 0, ended = false, startedCost = false;
function startStage(i) {
    stage = i; const d = stageDefs[i]; stageStart = now(); stageFrames = 0; paused = false; d.setup.call(d);
    log(`AUDIO stage ${i + 1}/${stageDefs.length} ${d.name} BEGIN (listen)`); log(`AUDIO expect: ${d.expect}`); flush();
}
function finish() { if (ended) return; ended = true; log(`RESULT ${failures ? "FAILED" : "OK"} failures=${failures}`); results.failures = failures; flush(); }
Loop.run(() => {
    Gamepad.update();
    Screen.clear(Color.new(30, 30, 50));
    if (phase === "functional") { functional(); phase = "stale"; label("E06: functional done", 20); return; }
    if (phase === "stale") {
        label("E06: waiting for a one-shot to end", 20);
        if (staleStep()) { phase = skip.has("cost") ? "audio" : "cost"; if (phase === "audio") { log("SECTION AUDIO (CROSS pauses the stage timer, START ends the run)"); startStage(0); } else log("SECTION COST (Audio3D.update() COP0 ms per frame)"); }
        return;
    }
    if (phase === "cost") {
        const c = costCases[costIdx];
        if (!startedCost) { costSetup(c); startedCost = true; }
        costFrameStep(c); label(`E06 cost ${costIdx + 1}/${costCases.length}: ${c.name}`, 20);
        if (costFrame === COST_WARM + COST_FRAMES) {
            const avg = costSum / costN, row = { name: c.name, sources: c.n, voices: c.voices, avgMs: avg, maxMs: costMax };
            for (const k in costStat) row[k + "PerUpdate"] = costStat[k] / costN;
            results.cost.push(row);
            log(`COST case=${c.name} sources=${c.n} maxVoices=${c.voices} avgMs=${avg.toFixed(3)} maxMs=${costMax.toFixed(3)} stolen/upd=${row.stolenPerUpdate.toFixed(2)} started/upd=${row.startedPerUpdate.toFixed(2)} culled/upd=${row.culledPerUpdate.toFixed(1)} queries/upd=${row.queriesPerUpdate.toFixed(2)}`);
            flush(); costTeardown(c); startedCost = false;
            if (++costIdx >= costCases.length) { phase = "audio"; log("SECTION AUDIO (CROSS pauses the stage timer, START ends the run)"); startStage(0); }
        }
        return;
    }
    if (phase === "done") { label(`E06 DONE: RESULT ${failures ? "FAILED" : "OK"} (failures=${failures})`, 20); label("Log: e06_ps2_validation.log next to athena.elf. Power off when ready.", 44); return; }
    const d = stageDefs[stage];
    if (Gamepad.player(0).justPressed(Gamepad.CROSS)) { if (paused) { stageStart += now() - pausedAt; paused = false; } else { paused = true; pausedAt = now(); } }
    const elapsed = paused ? pausedAt - stageStart : now() - stageStart;
    const st = Audio3D.stats();   // of the update the Loop system ran last (the only one per frame in a game)
    if (!paused) d.tick.call(d, elapsed / 1000, st);
    stageFrames++;
    label(`E06 ${stage + 1}/${stageDefs.length} ${d.name}${paused ? "  [PAUSED - CROSS to resume]" : ""}`, 20);
    label(`voices=${st.voices} sources=${st.sources} stolen=${st.stolen} queries=${st.queries}`, 44);
    if (Gamepad.player(0).justPressed(Gamepad.START) || elapsed >= STAGE_SECONDS * 1000) {
        d.end.call(d); const row = { stage: d.name, frames: stageFrames, wallMs: elapsed, avgFrameMs: elapsed / Math.max(1, stageFrames) };
        results.stages.push(row); log(`AUDIO stage ${stage + 1} ${d.name} END frames=${stageFrames} avgFrameMs=${row.avgFrameMs.toFixed(2)}`); flush();
        if (Gamepad.player(0).justPressed(Gamepad.START) || stage + 1 >= stageDefs.length) { finish(); phase = "done"; } else startStage(stage + 1);
    }
});
