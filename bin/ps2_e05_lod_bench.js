/* E05 LOD grid benchmark (PCSX2 / PS2). For each scenario, LOD.update() is timed with COP0 ticks, grid off vs on,
 * ABA order x3. Scenarios: small (300 groups), static (5000), moving (5000, 5% move per frame; scene.update timed
 * too, since the moved hooks run there). Camera fixed at one corner; groups spread over 800 x 800, until 20/40/80,
 * so most are beyond their last threshold. Log lines "E05 ..."; "E05_DONE" at the end. */
import { ticks, ticksToMilliseconds } from "ProfilerNative";
const mode = Screen.getMode(); mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
const quad = Model3D.Mesh.fromGeometry({ positions: new Float32Array([-1,-1,0, 1,-1,0, 1,1,0, -1,-1,0, 1,1,0, -1,1,0]) });
const tri = Model3D.Mesh.fromGeometry({ positions: new Float32Array([-1,-1,0, 1,-1,0, 0,1,0]) });
const camera = new Camera3D.Camera({ near: .5, far: 500, aspect: 1 }); camera.lookAt(0, 0, -100).setPosition(-380, 10, 380);
let seed = 4242; const rnd = () => (seed = (Math.imul(seed, 1103515245) + 12345) >>> 0) / 4294967296;
const WARM = 10, FRAMES = 60, CELL = 80;
function build(n, spread, dense) {
    const scene = new Scene3D.Scene(), nodes = [], gs = [], px = [], pz = [];
    for (let i = 0; i < n; i++) {
        px[i] = dense ? -380 + rnd() * spread : rnd() * 800 - 400; pz[i] = dense ? 380 - rnd() * spread : rnd() * 800 - 400;
        const node = new Scene3D.Node(quad).setPosition(px[i], 0, pz[i]); scene.root.add(node); nodes.push(node);
        gs.push(new LOD.Group(node, [{ mesh: quad, until: 20 }, { mesh: tri, until: 40 }, { mesh: null, until: 80 }]));
    }
    scene.update();
    return { scene, nodes, gs, px, pz, n };
}
function free(w) { for (const g of w.gs) g.dispose(); w.scene.dispose(); }
const timeMs = start => ticksToMilliseconds((ticks() - start) >>> 0);
function run(w, grid, movers) {
    LOD.setGrid(grid);
    let upd = 0, sc = 0, max = 0, skipped = 0, eval_ = 0, cells = 0, groupsSeen = 0; const stats = {};
    const step = Math.max(1, Math.floor(w.n / Math.max(1, movers)));
    for (let f = 0; f < WARM + FRAMES; f++) {
        if (movers) for (let i = f % step; i < w.n; i += step) { w.px[i] += 2; w.nodes[i].setPosition(w.px[i], 0, w.pz[i]); }
        let t = ticks(); LOD.update(camera, stats); const u = timeMs(t);
        t = ticks(); w.scene.update(); const s = timeMs(t);
        if (f >= WARM) { upd += u; sc += s; if (u > max) max = u; skipped += stats.cellsSkipped; eval_ += stats.evaluated; cells += stats.cells; groupsSeen += stats.groups; }
    }
    LOD.setGrid(0);
    return { lod: upd / FRAMES, scene: sc / FRAMES, max, skipped: skipped / FRAMES, evaluated: eval_ / FRAMES, cells: cells / FRAMES, groups: groupsSeen / FRAMES };
}
const scenarios = [["small300", 300, 0], ["static5000", 5000, 0], ["moving5000", 5000, 250], ["dense300", 300, 0, 60], ["dense5000", 5000, 0, 70], ["dense5000moving", 5000, 250, 70]];   // dense: every group within `spread` of the camera, nothing to skip
const order = [[0, 1], [1, 0], [0, 1]];   // grid off/on per round
let si = 0, round = 0, world = null, started = false, done = false; const sums = {};
Loop.run(() => {
    Screen.clear(Color.new(20, 20, 30));
    if (done) return;
    if (!world) { const [name, n, , spread] = scenarios[si]; world = build(n, spread, !!spread); console.log(`E05 BUILD ${name} groups=${n} memory=${JSON.stringify(System.getMemoryStats())}`); return; }
    const [name, , movers] = scenarios[si];
    for (const mode of order[round]) {
        const r = run(world, mode ? CELL : 0, movers);
        console.log(`E05 ${name} round=${round + 1} grid=${mode ? CELL : "off"} lodMs=${r.lod.toFixed(3)} lodMax=${r.max.toFixed(3)} sceneUpdateMs=${r.scene.toFixed(3)} evaluated=${r.evaluated.toFixed(0)}/${r.groups.toFixed(0)} cells=${r.cells.toFixed(0)} skipped=${r.skipped.toFixed(0)}`);
        const k = name + (mode ? "-grid" : "-off"); (sums[k] ||= []).push([r.lod, r.scene]);
    }
    if (++round >= order.length) {
        for (const m of ["off", "grid"]) { const a = sums[name + "-" + m]; console.log(`E05SUM ${name} grid=${m} lodMs=${(a.reduce((x, y) => x + y[0], 0) / a.length).toFixed(3)} sceneUpdateMs=${(a.reduce((x, y) => x + y[1], 0) / a.length).toFixed(3)}`); }
        free(world); world = null; round = 0;
        if (++si >= scenarios.length) { console.log("E05_DONE"); done = true; }
    }
});
