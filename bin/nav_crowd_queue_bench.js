// Four FIFO bursts of 16 agents: one warmup and three measured repeats.
// Drops result observers deliberately; completion includes path adoption/free.
const config = JSON.parse(std.loadFile("world_systems_bench_config.json"));
if (!config.path || !config.metadata?.hashes) throw new Error("Missing benchmark configuration");
const grid = new Nav.Grid(128, 128);
for (let x = 8; x < 128; x += 16) {
    const odd = Math.floor(x / 16) % 2;
    grid.fill(x, odd ? 8 : 0, x, odd ? 127 : 119, 0);
}
const reference = grid.findPath(1.5, 1.5, 126.5, 126.5);
if (!reference) throw new Error("Expected maze path");
const referenceQuery = grid.requestPath(1.5, 1.5, 126.5, 126.5);
while (referenceQuery.state === "pending") grid.stepRequests(4096);
if (referenceQuery.state !== "found") throw new Error("Expected incremental reference");
const referenceWorkUnits = referenceQuery.workUnits, referenceExpanded = referenceQuery.expanded;
referenceQuery.dispose();
const scene = new Scene3D.Scene(), root = scene.root, nodes = [];
const crowd = new Nav.Crowd(grid), agents = [];
for (let i = 0; i < 16; i++) {
    const node = new Scene3D.Node(); root.add(node); nodes.push(node);
    agents.push(crowd.add({ x: 1.5, z: 1.5, speed: 3, radius: 0, node }));
}
root.dispose(); scene.attachLoop();
const capacity = 2500, maxWork = 4096, budgetMs = 1;
const report = { version: 1, label: "nav-crowd-queue", status: "running",
    metadata: { ...config.metadata, videoMode: Screen.getMode(), agents: 16, warmup: 1, samples: 3,
        maxWork, budgetMs, reference: Array.from(reference), referenceWorkUnits, referenceExpanded,
        scene: "same maze128; 16 bound Scene3D nodes, radius 0; movement at fixed dt 1/60 after adoption",
        timing: "COP0 CPU; JS/timer included; no explicit GS wait; step includes adoption and free with no result observer",
        enqueue: "one agent per frame; one shared grid budget per frame",
        frames: "enqueue, step, Crowd.update(1/60), state inspection and Scene3D transforms; initial scene setup/final serialization excluded",
        memoryBefore: System.getMemoryStats() }, bursts: [] };
let burst = 0, count = 0, previous = -1, enqueue = 0, completed = 0, raw, latency;
function startBurst() {
    raw = { ticks: new Uint32Array(capacity), cpuMs: new Float32Array(capacity),
        wallMs: new Float32Array(capacity), work: new Uint32Array(capacity),
        completed: new Uint8Array(capacity), pending: new Uint8Array(capacity) };
    latency = []; count = enqueue = completed = 0;
    for (const a of agents) { a.stop(); a.setPosition(1.5, 0, 1.5); }
}
startBurst();
Loop.run(() => {
    if (previous >= 0) {
        const stats = Loop.getStats(); raw.cpuMs[previous] = stats.cpuMs; raw.wallMs[previous] = stats.frameMs;
        previous = -1;
    }
    if (completed === agents.length) {
        const arrays = {};
        for (const key in raw) arrays[key] = Array.from(raw[key].subarray(0, count));
        report.bursts.push({ index: burst, warmup: burst === 0, frames: count, raw: arrays, latency });
        console.log("[CrowdBench] burst " + (burst + 1) + ": " + count + " frames; last latency " + latency[15].frames);
        if (++burst === 4) {
            report.status = "completed"; report.frames = report.bursts.reduce((n, b) => n + b.frames, 0);
            for (const a of agents) a.dispose(); crowd.dispose(); scene.dispose();
            for (const node of nodes) node.dispose(); grid.dispose();
            report.metadata.memoryAfter = System.getMemoryStats(); Bench.save(report, config.path);
            Loop.stop(); console.log("[CrowdBench] report written: " + config.path); return;
        }
        startBurst();
    }
    if (count >= capacity) throw new Error("Queue benchmark capacity exceeded");
    if (enqueue < agents.length) {
        // The temporary JS observer is collected; the native agent owns the plan.
        agents[enqueue].requestMoveTo(126.5, 126.5);
        latency.push({ id: enqueue, enqueued: count, completed: null, frames: null }); enqueue++;
    }
    const begin = Profiler.ticks();
    const work = grid.stepRequests(maxWork, budgetMs);
    const ticks = (Profiler.ticks() - begin) >>> 0;
    if (work < 1 || work > maxWork) throw new Error("Work budget violation");
    crowd.update(1/60);
    while (completed < enqueue && agents[completed].planningState !== "pending") {
        const a = agents[completed];
        if (a.planningState !== "found" || a.waypoints * 2 !== reference.length) throw new Error("Agent plan differs");
        latency[completed].completed = count;
        latency[completed].frames = count - latency[completed].enqueued + 1;
        completed++;
    }
    raw.ticks[count] = ticks; raw.work[count] = work;
    raw.completed[count] = completed; raw.pending[count] = grid.pendingRequests;
    previous = count++;
}, { maxDelta: 0 });
