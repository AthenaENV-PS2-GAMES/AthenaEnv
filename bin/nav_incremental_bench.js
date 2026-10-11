// Same maze/endpoints as world_systems_bench.js. Prepare metadata with
// tools/benchmark_3d.py, then use this script in an isolated boot directory.
// One ELF, six sequential rounds: sync/incremental, incremental/sync,
// sync/incremental. Every round has 3 warmup queries and 30 measured queries.
const config = JSON.parse(std.loadFile("world_systems_bench_config.json"));
if (!config.path || !config.metadata?.hashes) throw new Error("Missing benchmark configuration");
const agentSubject = config.subject === "agent";
if (config.subject !== undefined && config.subject !== "grid" && !agentSubject) throw new Error("Invalid subject");
const grid = new Nav.Grid(128, 128);
for (let x = 8; x < 128; x += 16) {
    const odd = Math.floor(x / 16) % 2;
    grid.fill(x, odd ? 8 : 0, x, odd ? 127 : 119, 0);
}
const reference = grid.findPath(1.5, 1.5, 126.5, 126.5);
if (!reference) throw new Error("Expected reference path");
const referenceExpanded = grid.lastExpanded;
const reusable = new Float32Array(Nav.MAX_PATH * 2);
const crowd = agentSubject ? new Nav.Crowd(grid) : null;
const agent = crowd ? crowd.add({ x: 1.5, z: 1.5, radius: 0 }) : null;
const phases = ["queued", "initialize", "clearStamps", "search", "countPath", "tracePath", "emitPath", "smoothPath", "finished"];
const modes = ["sync", "incremental", "incremental", "sync", "sync", "incremental"];
const maxWork = 4096, budgetMs = 1, warmup = 3, samples = 30, capacity = 15000;
const report = { version: 1, label: "nav-incremental", status: "running",
    metadata: { ...config.metadata, videoMode: Screen.getMode(),
        timing: "COP0 CPU ticks include binding/timer overhead; no explicit GS wait",
        experiment: "same ELF/API comparison; three alternating sync/incremental pairs",
        subject: agentSubject ? "Agent.moveTo / Agent.requestMoveTo; adoption included in step, Crowd.update(0) in full frame" : "Grid.findPath / Grid.requestPath",
        scene: { width: 128, depth: 128, start: [1.5, 1.5], end: [126.5, 126.5] },
        maxWork, budgetMs, warmup, samples, reference: Array.from(reference), referenceExpanded,
        phaseNames: phases, phaseScope: "start/end of each call; mixed-phase calls are not individual phase timings",
        frameScope: "all query frames including creation, result validation/copy/disposal; one-time grid/reference setup and final report serialization excluded",
        memoryBefore: System.getMemoryStats() }, rounds: [] };
let roundIndex = 0, queryIndex = 0, request = null, count = 0, previous = -1;
let queryFirst = 0, queryWork = 0, queryTicks = 0, queryFrames = 0;
let raw, queries;
function startRound() {
    raw = { ticks: new Uint32Array(capacity), work: new Uint32Array(capacity),
        cpuMs: new Float32Array(capacity), wallMs: new Float32Array(capacity),
        query: new Uint8Array(capacity), phaseBefore: new Uint8Array(capacity),
        phaseAfter: new Uint8Array(capacity), final: new Uint8Array(capacity) };
    queries = []; count = 0; queryIndex = 0;
}
function samePath(values, length) {
    if (length !== reference.length) throw new Error("Result length differs");
    for (let i = 0; i < length; i++) if (values[i] !== reference[i]) throw new Error("Result differs at " + i);
}
startRound();
Loop.run(() => {
    // Observe the completed preceding frame before measuring the next step.
    if (previous >= 0) {
        const stats = Loop.getStats(); raw.cpuMs[previous] = stats.cpuMs; raw.wallMs[previous] = stats.frameMs;
        previous = -1;
    }
    if (queryIndex === warmup + samples) {
        const arrays = {};
        for (const key in raw) arrays[key] = Array.from(raw[key].subarray(0, count));
        report.rounds.push({ pair: Math.floor(roundIndex / 2) + 1, order: roundIndex % 2 + 1,
            mode: modes[roundIndex], frames: count, raw: arrays, queries });
        console.log("[NavBench] round " + (roundIndex + 1) + " " + modes[roundIndex] + ": " + count + " frames");
        if (++roundIndex === modes.length) {
            report.status = "completed"; report.frames = report.rounds.reduce((n, r) => n + r.frames, 0);
            if (agent) agent.dispose(); if (crowd) crowd.dispose();
            grid.dispose(); report.metadata.memoryAfter = System.getMemoryStats();
            Bench.save(report, config.path); Loop.stop();
            console.log("[NavBench] report written: " + config.path); return;
        }
        startRound();
    }
    if (count >= capacity) throw new Error("Benchmark frame capacity exceeded");
    const incremental = modes[roundIndex] === "incremental";
    if (!queryFrames) {
        queryFirst = count; queryWork = 0; queryTicks = 0;
        if (agent) agent.setPosition(1.5, 0, 1.5);
        if (incremental) request = agent ? agent.requestMoveTo(126.5, 126.5) : grid.requestPath(1.5, 1.5, 126.5, 126.5);
    }
    raw.query[count] = queryIndex;
    raw.phaseBefore[count] = incremental ? phases.indexOf(request.phase) : 0;
    const begin = Profiler.ticks();
    let path, work;
    if (incremental) work = grid.stepRequests(maxWork, budgetMs);
    else if (agent) { if (!agent.moveTo(126.5, 126.5)) throw new Error("Expected agent path"); }
    else path = grid.findPath(1.5, 1.5, 126.5, 126.5);
    const ticks = (Profiler.ticks() - begin) >>> 0;
    raw.ticks[count] = ticks;
    raw.work[count] = incremental ? work : grid.lastExpanded;
    raw.phaseAfter[count] = incremental ? phases.indexOf(request.phase) : 8;
    queryTicks += ticks; queryWork += raw.work[count]; queryFrames++;
    if (crowd) crowd.update(0);
    previous = count++;
    if (!incremental || request.state !== "pending") {
        raw.final[previous] = 1;
        if (incremental) {
            if (request.state !== "found" || request.expanded !== referenceExpanded) throw new Error("Incremental search differs: " + request.state);
            samePath(reusable, request.copyPath(reusable));
        } else if (!agent) samePath(path, path.length);
        if (agent && (agent.state !== "moving" || agent.waypoints * 2 !== reference.length ||
            (incremental && agent.planningState !== "found"))) throw new Error("Agent did not adopt path");
        queries.push({ index: queryIndex, warmup: queryIndex < warmup, first: queryFirst,
            frames: queryFrames, totalTicks: queryTicks, totalWork: queryWork,
            expanded: incremental ? request.expanded : grid.lastExpanded,
            pointCount: reference.length / 2, cost: incremental ? request.cost : null,
            storageBytes: incremental ? request.storageBytes : 0 });
        if (incremental) { request.dispose(); request = null; }
        queryIndex++;
        if (queryIndex === warmup) console.log("[NavBench] warmup " + modes[roundIndex] + ": " + queryFrames + " frames/query, " + queryWork + " units");
        queryFrames = 0;
    }
}, { maxDelta: 0 });
