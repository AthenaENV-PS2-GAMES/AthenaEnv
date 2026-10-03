/*
 * Script switching soak test: runs the heavy scripts of tests/soak/ one
 * after another, ROUNDS times. Each one switches back here with std.reload()
 * in the middle of its work (music streaming, jobs in flight, threads
 * running, a video playing, glyphs being preloaded), so every switch tears
 * a busy VM down. The EE heap and VRAM are sampled here, first thing in each
 * new VM, and compared across rounds: memory that grows round after round
 * is a leak in the teardown, and a hang or crash is a teardown bug.
 *
 * Run it from the launcher (tests/index.js), which shows the report when it
 * ends. The progress is kept in tests/soak/state.json between VMs, so the
 * tests directory must be writable (USB, host:), not a disc.
 */
const SELF = "tests/soak_test.js";
const LAUNCHER = "tests/index.js";
const STATE = "tests/soak/state.json";
const ROUNDS = 6;
const WORKLOADS = [
    "tests/soak/font.js",
    "tests/soak/sound.js",
    "tests/soak/imagelist.js",
    "tests/soak/video.js",
    "tests/soak/jobs.js",
    "tests/soak/threads.js",
];
// Growth allowed from the end of the first round (caches warm, drivers loaded) to the end of the last one.
const MAX_HEAP_GROWTH = 64 * 1024;
const MAX_VRAM_GROWTH = 0;

// Before this script allocates anything: the previous VM is gone, this one is fresh.
const stats = System.getMemoryStats();
const sample = { heap: stats.allocs, vram: Screen.getMemoryStats(Screen.VRAM_USED_TOTAL) };

const kb = bytes => (bytes / 1024).toFixed(1) + " KB";
const baseName = path => path.slice(path.lastIndexOf("/") + 1);

function readState() {
    const text = std.loadFile(STATE);
    try { return text ? JSON.parse(text) : null; } catch (error) { return null; }
}

function writeState(state) {
    const file = std.open(STATE, "w");
    if (!file) throw new Error(`cannot write ${STATE}: run the soak test from a writable tests directory`);
    file.puts(JSON.stringify(state));
    file.close();
}

function show(text) {
    const font = new Font();
    Screen.clear(Color.new(0, 0, 0));
    font.print(40, 40, text);
    Screen.flip();
    font.free();
}

function report(state) {
    let failed = state.errors.length;
    console.log(`Soak: ${ROUNDS} rounds of ${WORKLOADS.length} scripts, ${ROUNDS * WORKLOADS.length} VM switches`);
    state.rounds.forEach((round, index) =>
        console.log(`  round ${index}: heap ${kb(round.heap)}, VRAM ${kb(round.vram)}`));

    // Round 0 is before any workload; round 1 has every cache and driver warm.
    const first = state.rounds[1], last = state.rounds[state.rounds.length - 1];
    const heapGrowth = last.heap - first.heap, vramGrowth = last.vram - first.vram;
    const heapOk = heapGrowth <= MAX_HEAP_GROWTH, vramOk = vramGrowth <= MAX_VRAM_GROWTH;
    console.log(`${heapOk ? "[PASS]" : "[FAIL]"} heap growth from round 1: ${kb(heapGrowth)} (limit ${kb(MAX_HEAP_GROWTH)})`);
    console.log(`${vramOk ? "[PASS]" : "[FAIL]"} VRAM growth from round 1: ${kb(vramGrowth)} (limit ${kb(MAX_VRAM_GROWTH)})`);
    if (!heapOk) failed++;
    if (!vramOk) failed++;

    // Heap after each script, round by round (relative to its first round): a leak climbs every round.
    for (const script of WORKLOADS) {
        const after = state.after[script] || [];
        if (after.length < 2) continue;
        const series = after.map(entry => ((entry.heap - after[0].heap) / 1024).toFixed(1));
        const vram = after[after.length - 1].vram - after[0].vram;
        console.log(`  after ${baseName(script)}: heap KB ${series.join(", ")}; VRAM ${kb(vram)}`);
    }
    for (const error of state.errors)
        console.log("[FAIL] " + error);
    console.log(`Result: ${failed ? "failed" : "passed"}, ${state.errors.length} script errors`);
}

const last = std.lastRun();
let state = readState();
const returning = last && state && !state.done && state.running === last.script;

if (!returning) {
    state = { round: 0, next: 0, rounds: [], after: {}, errors: [] };
} else {
    const name = baseName(last.script);
    if (last.status === "error") {
        // The error, and the end of what the script printed before it.
        const output = (last.output || "").trim().split("\n").slice(-3).join(" | ");
        state.errors.push(`${name}: ${(last.error || "").split("\n")[0]}${output ? " (output: " + output + ")" : ""}`);
    } else if (last.status !== "reloaded")
        state.errors.push(`${name}: ended (${last.status}) instead of switching back`);
    (state.after[last.script] = state.after[last.script] || []).push(sample);
    if (last.status === "exited") {
        // SELECT+START: the player stopped the soak.
        state.done = true;
        writeState(state);
        console.log("Soak stopped with SELECT+START");
        std.reload(LAUNCHER);
    }
}

if (state.next === 0)
    state.rounds.push(sample);

if (state.round >= ROUNDS) {
    state.done = true;
    writeState(state);
    report(state);
    std.reload(LAUNCHER);
}

state.running = WORKLOADS[state.next];
const label = `Soak round ${state.round + 1}/${ROUNDS}: ${baseName(state.running)}`;
if (++state.next === WORKLOADS.length) {
    state.next = 0;
    state.round++;
}
writeState(state);
show(label);
std.reload(state.running, { returnTo: SELF });
