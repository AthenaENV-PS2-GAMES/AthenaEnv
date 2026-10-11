/* E04 validation for a physical PS2 (also runs on PCSX2): Render3D tint and Sky day/night tint.
 *
 * Writes `e04_ps2_validation.log` next to the ELF, rewritten after every phase (a crash or power
 * cycle keeps the finished parts), and `e04_ps2_results.json`.
 * Phases: SYSTEM, FUNCTIONAL (PASS/FAIL checks), COST (setTint submission cost, COP0 ticks, three
 * alternating rounds), VISUAL (6 stages of 10 s with an on-screen label; look at the screen and
 * photograph each stage). In VISUAL, CROSS pauses/resumes the stage timer, and START ends the run.
 * Optional `ps2_e04_config.json`: { "skip": ["cost"] }, { "stageSeconds": 10 }, { "quick": true }.
 * Return the .log file (and the photos / notes of what each visual stage looked like). */
import * as Loop from "Loop";
import { ticks, ticksToMilliseconds } from "ProfilerNative";

const LOG = "e04_ps2_validation.log", JSON_OUT = "e04_ps2_results.json";
let config = {};
try { const text = std.loadFile("ps2_e04_config.json"); if (text) config = JSON.parse(text); } catch (_) { /* optional */ }
const quick = !!config.quick, skip = new Set(config.skip ?? []);
const STAGE_SECONDS = config.stageSeconds ?? (quick ? 5 : 10);
const D0 = Date.now(), lines = [], results = { version: 1, checks: [], cost: [], stages: [] };
function log(text) { const line = `[${((Date.now() - D0) / 1000).toFixed(3)}] ${text}`; lines.push(line); console.log(line); }
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
const near = (a, b) => Math.abs(a - b) < 1e-4;
const same3 = (a, b) => a.length === 3 && near(a[0], b[0]) && near(a[1], b[1]) && near(a[2], b[2]);
function throws(fn, type) { try { fn(); } catch (e) { return e instanceof type; } return false; }

/* ---- setup (assets and scene) ---- */
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
log(`SECTION SYSTEM mode=${mode.width}x${height} interlace=${mode.interlace} field=${mode.field} psmz=Z16S`);
try { log("SYSTEM memory=" + JSON.stringify(System.getMemoryStats())); } catch (_) { /* optional */ }
const font = new Font();
const camera = new Camera3D.Camera({ aspect: mode.width / height, near: .5, far: 200 }); camera.setViewport(mode.width, height);
const lights = new Lights.Set().setAmbient(.4, .4, .4).setDirectional(0, -.4, -.6, -1, 1, 1, 1);

/* Flat quads and an indexed cube: UNLIT, DIFFUSE and indexed vertex colors. */
const quadPos = new Float32Array([-.4, -.4, 0, .4, -.4, 0, .4, .4, 0, -.4, -.4, 0, .4, .4, 0, -.4, .4, 0]);
const ones = new Float32Array(24); for (let i = 0; i < 24; i++) ones[i] = 1; /* Float32Array.fill() writes zeros on the EE */
const unlitQuad = Model3D.Mesh.fromGeometry({ positions: quadPos, colors: ones, material: { baseColor: new Float32Array([.9, .8, .6, 1]) } });
const diffuseQuad = Model3D.Mesh.fromGeometry({ positions: quadPos, material: { shading: Model3D.DIFFUSE, baseColor: new Float32Array([.9, .8, .6, 1]) } });
const colorCube = Model3D.Mesh.fromGeometry({
    positions: new Float32Array([-1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1, -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1]),
    colors: new Float32Array([1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, .4, .6, 1, 1]),
    indices: new Uint32Array([0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5]) });

/* Skin, morph, textured DIFFUSE and textured UNLIT (GS MODULATE) through Scene3D. */
const scene = new Scene3D.Scene();
function place(asset, x, y, z, s) { const h = new Scene3D.Node().setPosition(x, y, z).setScale(s, s, s); h.add(asset.root); scene.root.add(h); }
const arm = GLTF3D.load("models/arm.glb", { shading: Model3D.DIFFUSE, baseColor: new Float32Array([.9, .6, .3, 1]) });
const bend = GLTF3D.load("models/bend.glb", { shading: Model3D.DIFFUSE, baseColor: new Float32Array([.4, .7, 1, 1]) });
const morph = GLTF3D.load("models/morph.glb", { shading: Model3D.DIFFUSE });
const texCube = Model3D.load("models/textured_cube.glb", { shading: Model3D.DIFFUSE });
const checker = Model3D.Texture.load("models/checker.png");
const texQuad = Model3D.Mesh.fromGeometry({ positions: new Float32Array([-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, -1, 0, 1, 1, 0, -1, 1, 0]),
    texcoords: new Float32Array([0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0]), material: { texture: checker } });
place(arm, -4.5, 0, 0, 1); place(bend, -2.5, 0, 0, 1); place(morph, 0, 0, 0, 1);
scene.root.add(new Scene3D.Node(texCube).setPosition(2.5, 0, 0).setScale(.8, .8, .8));
scene.root.add(new Scene3D.Node(texQuad).setPosition(5, 0, 0));
const armP = new Animation3D.Player(arm.clips.wave, arm.nodes); armP.loop = true; armP.play();
const bendP = new Animation3D.Player(bend.clips.bend, bend.nodes); bendP.loop = true; bendP.speed = .5; bendP.play();
const morphP = new Animation3D.Player(morph.clips.morph, morph.nodes); morphP.loop = true; morphP.play();
Animation3D.attachLoop(); scene.attachLoop(); scene.update();

/* Voxel world: baked light (UNLIT), drawn with Sky's day/night tint and no rebuild. */
const world = new Voxel.World({ size: [32, 24, 32], chunk: 8 });
world.setMaterial(1, { color: { top: [.35, .72, .25], side: [.47, .36, .22], bottom: [.47, .36, .22] } });
world.setMaterial(2, { color: [.52, .52, .55] });
world.generate({ seed: 7, caves: 0, frequency: 1 / 20 }); world.rebuild(0);

/* Cost fixture: 300 DIFFUSE cubes in one group. */
const N = 300, instances = [];
for (let i = 0; i < N; i++) instances.push(texCube.createInstance().setPosition((i % 20) - 9.5, Math.floor(i / 20) - 7, 0).setScale(.4, .4, .4));

const stats = {};
const pad = () => Gamepad.player(0);

/* ---- FUNCTIONAL ---- */
log("SECTION FUNCTIONAL");
function functional() {
    check("default tint is white", same3(Render3D.getTint(), [1, 1, 1]));
    Render3D.setTint(.5, .25, 0); check("set/get tint", same3(Render3D.getTint(), [.5, .25, 0]));
    check("out-of-range tint throws RangeError", throws(() => Render3D.setTint(1.1, 0, 0), RangeError) && throws(() => Render3D.setTint(NaN, 0, 0), RangeError));
    check("failed setTint leaves the previous tint", same3(Render3D.getTint(), [.5, .25, 0]));
    Render3D.setTint(1, 1, 1);
    for (const h of [0, 3, 6, 9, 12, 15, 18, 21]) {
        Sky.setTime(h); const t = Sky.tint();
        log(`SKYTINT hour=${h} tint=${t.map(v => v.toFixed(3)).join(",")}`);
        check(`Sky.tint hour ${h} in [0,1]`, t.length === 3 && t.every(v => v >= 0 && v <= 1));
    }
    Sky.setTime(12); check("noon tint is white", same3(Sky.tint(), [1, 1, 1]));
    Sky.setTime(0); check("midnight tint is dim and bluish", Sky.tint()[0] < .3 && Sky.tint()[2] > Sky.tint()[0]);
    /* Every program draws with tint; stats must report the draw. */
    Render3D.setTint(.3, .5, 1);
    for (const [name, mesh, lit] of [["UNLIT quad", unlitQuad, undefined], ["DIFFUSE quad", diffuseQuad, lights], ["indexed cube", colorCube, undefined], ["textured cube", texCube, lights], ["textured quad", texQuad, undefined]]) {
        const inst = mesh.createInstance().setPosition(0, 0, -3);
        const s = Render3D.draw(inst, camera, Render3D.CULL_NONE, lit, {});
        check(`tinted draw: ${name}`, s.drawPasses >= 1); inst.dispose();
    }
    const s = scene.draw(camera, Render3D.CULL_NONE, lights, {}); check("tinted scene draw (skin, morph, textured)", s.drawPasses >= 1, `drawPasses=${s.drawPasses}`);
    Render3D.setTint(1, 1, 1);
    check("tint restored to white", same3(Render3D.getTint(), [1, 1, 1]));
    flush();
}

/* ---- phases (run inside the Loop, one state machine) ---- */
const costModes = [["white", new Uint8Array(N)], ["every", new Uint8Array(N)], ["every10", new Uint8Array(N)]];
for (let i = 0; i < N; i++) { costModes[1][1][i] = i & 1; costModes[2][1][i] = Math.floor(i / 10) & 1; }
const tints = [[1, 1, 1], [.4, .6, 1]];
const costOrder = [];
for (let round = 0; round < (quick ? 2 : 3); round++) for (const m of (round & 1 ? [2, 1, 0] : [0, 1, 2])) costOrder.push({ round, m });
const COST_WARM = 30, COST_FRAMES = quick ? 60 : 150;

const rowsFlat = [[unlitQuad, 1.2], [diffuseQuad, 0], [colorCube, -1.5]];
const flatTints = [[1, 1, 1], [1, 0, 0], [.5, .5, .5], [0, .7, 1]];
const stageDefs = [
    { name: "FLAT-ROWS", expect: "3 rows x 4 columns. Columns tints: white, red, half grey, blue-green. Row1 UNLIT beige quads, row2 DIFFUSE quads (darker), row3 indexed colored cubes. White column = untinted colors; every other column visibly tinted, same shapes.",
      draw() { camera.setPosition(0, 0, 6).lookAt(0, 0, 0); Render3D.group(() => { rowsFlat.forEach(([mesh, y]) => flatTints.forEach((t, i) => {
          const inst = mesh.createInstance().setPosition(-3 + i * 2, y, 0);
          if (mesh === colorCube) inst.setScale(.35, .35, .35).setRotationQuaternion(.2, .3, 0, .93);
          Render3D.setTint(t[0], t[1], t[2]); Render3D.draw(inst, camera, Render3D.CULL_NONE, mesh === diffuseQuad ? lights : undefined); inst.dispose(); })); Render3D.setTint(1, 1, 1); }); } },
    { name: "MODELS-WHITE", expect: "Animated brown arm (skin), grey bend column (skin), morphing box, textured cube, checker quad (red/green/blue/yellow). Reference colors, no tint.",
      draw() { camera.setPosition(0, 1.5, 11).lookAt(0, .8, 0); scene.draw(camera, Render3D.CULL_NONE, lights, stats); } },
    { name: "MODELS-TINTED", expect: "Same scene tinted (0.3, 0.5, 1): everything bluish/darker: arm near black-green, bend column blue, checker quad red->dark purple, yellow->green. Animation continues.",
      draw() { camera.setPosition(0, 1.5, 11).lookAt(0, .8, 0); Render3D.setTint(.3, .5, 1); scene.draw(camera, Render3D.CULL_NONE, lights, stats); Render3D.setTint(1, 1, 1); } },
    { name: "VOXEL-NOON", expect: "Block island seen from above: bright green top, brown sides, grey stone, light blue sky. Reference (tint = Sky.tint() at 12:00 = white).",
      setup() { Sky.setTime(12); }, draw() { Screen.clear(Sky.clearColor()); camera.setPosition(16, 22, 40).lookAt(16, 6, 16); Render3D.setTint(...Sky.tint()); world.draw(camera, Render3D.CULL_BACK, undefined, stats, 120); Render3D.setTint(1, 1, 1); } },
    { name: "VOXEL-NIGHT", expect: "Same island at 01:00: very dark green/blue, almost black sides, dark navy sky. Geometry identical to VOXEL-NOON (no rebuild).",
      setup() { Sky.setTime(1); }, draw() { Screen.clear(Sky.clearColor()); camera.setPosition(16, 22, 40).lookAt(16, 6, 16); Render3D.setTint(...Sky.tint()); world.draw(camera, Render3D.CULL_BACK, undefined, stats, 120); Render3D.setTint(1, 1, 1); } },
    { name: "DAY-CYCLE", expect: "Sky gradient and island cycle 0h->24h over the stage: night, warm dawn, bright day, warm dusk, night. Smooth, no flicker or stuck tint, sun disc moves.",
      setup() { this.t0 = Date.now(); }, draw() { const h = ((Date.now() - this.t0) / (STAGE_SECONDS * 1000)) * 24; Sky.setTime(h);
          Screen.clear(Sky.clearColor()); camera.setPosition(16, 22, 40).lookAt(16, 6, 16); Sky.draw(camera);
          Render3D.setTint(...Sky.tint()); world.draw(camera, Render3D.CULL_BACK, undefined, stats, 120); Render3D.setTint(1, 1, 1); this.hour = h; } },
];

let phase = skip.has("functional") ? "cost" : "functional", frame = 0, costIdx = 0, costSum = 0, costMax = 0, costCount = 0;
let stage = -1, stageStart = 0, stageFrames = 0, paused = false, pausedAt = 0, ended = false;
const costRows = {};
function startStage(i) {
    stage = i; const d = stageDefs[i]; stageStart = Date.now(); stageFrames = 0; paused = false;
    if (d.setup) d.setup.call(d);
    log(`VISUAL stage ${i + 1}/${stageDefs.length} ${d.name} BEGIN (photograph it)`); log(`VISUAL expect: ${d.expect}`); flush();
}
function finish() {
    if (ended) return; ended = true;
    log(`RESULT ${failures ? "FAILED" : "OK"} failures=${failures}`); flush();
    results.failures = failures; flush();
}
function label(text, y) { font.print(16, y, text); }
Loop.run(() => {
    Gamepad.update();
    Screen.clear(Color.new(40, 40, 60));
    if (phase === "functional") {
        functional(); phase = skip.has("cost") ? "visual" : "cost"; log("SECTION COST (300 DIFFUSE cubes in one group; ticks = COP0 ms per frame of setTint+draw JS loop)");
        label("E04: functional done", 20); return;
    }
    if (phase === "cost") {
        const { round, m } = costOrder[costIdx], pick = costModes[m][1];
        if (frame === 0) camera.setPosition(0, 0, 30).lookAt(0, 0, 0);
        const start = ticks();
        Render3D.group(() => { for (let i = 0; i < N; i++) { const t = tints[pick[i]]; Render3D.setTint(t[0], t[1], t[2]); Render3D.draw(instances[i], camera, Render3D.CULL_BACK, lights, null); } });
        Render3D.setTint(1, 1, 1);
        const dt = ticksToMilliseconds((ticks() - start) >>> 0);
        label(`E04 cost ${costIdx + 1}/${costOrder.length}: ${costModes[m][0]}`, 20);
        if (++frame > COST_WARM) { costSum += dt; costCount++; if (dt > costMax) costMax = dt; }
        if (frame === COST_WARM + COST_FRAMES) {
            const avg = costSum / costCount, name = costModes[m][0];
            (costRows[name] ||= []).push(avg);
            log(`COST round=${round + 1} mode=${name} draws=${N} avgMs=${avg.toFixed(3)} maxMs=${costMax.toFixed(3)} frames=${costCount}`);
            results.cost.push({ round: round + 1, mode: name, avgMs: avg, maxMs: costMax }); flush();
            frame = 0; costSum = 0; costMax = 0; costCount = 0;
            if (++costIdx >= costOrder.length) {
                for (const [name, a] of Object.entries(costRows)) log(`COSTSUMMARY mode=${name} avgOfRounds=${(a.reduce((x, y) => x + y, 0) / a.length).toFixed(3)} rounds=${a.map(v => v.toFixed(3)).join("/")}`);
                phase = "visual"; log("SECTION VISUAL (CROSS pauses the stage timer, START ends the run)"); startStage(0);
            }
        }
        return;
    }
    if (phase === "done") { label(`E04 DONE: RESULT ${failures ? "FAILED" : "OK"} (failures=${failures})`, 20); label("Log: e04_ps2_validation.log next to athena.elf. Power off when ready.", 44); return; }
    /* visual */
    const d = stageDefs[stage];
    if (pad().justPressed(Gamepad.CROSS)) { if (paused) { stageStart += Date.now() - pausedAt; paused = false; } else { paused = true; pausedAt = Date.now(); } }
    d.draw.call(d);
    stageFrames++;
    label(`E04 ${stage + 1}/${stageDefs.length} ${d.name}${paused ? "  [PAUSED - CROSS to resume]" : ""}${d.hour !== undefined && d.name === "DAY-CYCLE" ? "  hour " + d.hour.toFixed(1) : ""}`, 20);
    label(`tint=${Render3D.getTint().map(v => v.toFixed(2)).join(",")} sky=${Sky.tint().map(v => v.toFixed(2)).join(",")}`, 44);
    const elapsed = paused ? pausedAt - stageStart : Date.now() - stageStart;
    if (pad().justPressed(Gamepad.START) || elapsed >= STAGE_SECONDS * 1000) {
        const ms = elapsed, row = { stage: d.name, frames: stageFrames, wallMs: ms, avgFrameMs: ms / Math.max(1, stageFrames) };
        results.stages.push(row); log(`VISUAL stage ${stage + 1} ${d.name} END frames=${stageFrames} avgFrameMs=${row.avgFrameMs.toFixed(2)}`); flush();
        if (pad().justPressed(Gamepad.START) || stage + 1 >= stageDefs.length) { finish(); phase = "done"; }
        else startStage(stage + 1);
    }
});
