/* E07 Triggers3D benchmark (PCSX2 / PS2): world.update() timed with COP0 ticks, grid off vs on, ABA order x3.
 * Cases (zones x bodies): small 10x50; sparse 50x500 static; sparse with 25 bodies moving per frame; sparse with 5% of the bodies
 * following nodes (scene.update timed separately: the moved hooks run there); stay (onStay on every zone); dense 50x500 all within 40 units.
 * Zones/bodies are spread over 800 x 800; zones are 10-60 wide. Log: "E07 ..." and "E07_DONE". */
import { ticks, ticksToMilliseconds } from "ProfilerNative";
const mode = Screen.getMode(); mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
let seed = 99; const rnd = () => (seed = (Math.imul(seed, 1103515245) + 12345) >>> 0) / 4294967296;
const WARM = 10, FRAMES = 60, CELL = 40;
const cases = [
    { name: "small10x50", z: 10, b: 50, spread: 800 },
    { name: "sparse50x500-static", z: 50, b: 500, spread: 800 },
    { name: "sparse50x500-moving25", z: 50, b: 500, spread: 800, moving: 25 },
    { name: "sparse50x500-nodes25", z: 50, b: 500, spread: 800, nodes: 25 },
    { name: "sparse50x500-stay", z: 50, b: 500, spread: 800, stay: true },
    { name: "dense50x500", z: 50, b: 500, spread: 40 },
];
const ms = t => ticksToMilliseconds((ticks() - t) >>> 0);
function build(c) {
    const w = new Triggers3D.World(), scene = new Scene3D.Scene(), bodies = [], nodes = [], px = [], pz = [];
    const half = c.spread / 2;
    for (let i = 0; i < c.z; i++) {
        const x = rnd() * c.spread - half, z = rnd() * c.spread - half, o = { mask: 1 };
        if (c.stay) o.onStay = () => {};
        if (i % 2) w.box(x, 0, z, x + 10 + rnd() * 50, 6, z + 10 + rnd() * 50, o); else w.sphere(x, 2, z, 5 + rnd() * 30, o);
    }
    for (let i = 0; i < c.b; i++) {
        px[i] = rnd() * c.spread - half; pz[i] = rnd() * c.spread - half;
        bodies.push(w.body(px[i], 1, pz[i], { radius: 0.5, layers: 1 }));
        if (c.nodes && i < c.nodes) { const n = new Scene3D.Node(); scene.root.add(n); n.setPosition(px[i], 1, pz[i]); nodes.push(n); bodies[i].follow(n); }
    }
    scene.update();
    return { w, scene, bodies, nodes, px, pz, c };
}
function run(world, grid) {
    const { w, scene, bodies, nodes, px, pz, c } = world;
    if (grid && !w.setGrid) return null;   // a build without the grid (the baseline)
    if (w.setGrid) w.setGrid(grid);
    let upd = 0, sc = 0, max = 0, cand = 0, tested = 0, ev = 0, stay = 0;
    for (let f = 0; f < WARM + FRAMES; f++) {
        if (c.moving) for (let k = 0; k < c.moving; k++) { const i = (f * 7 + k * 13) % c.b; px[i] += 1.5; bodies[i].setPosition(px[i], 1, pz[i]); }
        if (c.nodes) for (let k = 0; k < c.nodes; k++) { px[k] += 1.5; nodes[k].setPosition(px[k], 1, pz[k]); }
        let t = ticks(); scene.update(); const s = ms(t);
        t = ticks(); const n = w.update(); const u = ms(t);
        if (f >= WARM) { upd += u; sc += s; if (u > max) max = u; const st = w.stats ? w.stats() : { candidates: 0, tested: 0, events: n, stayEvents: 0 }; cand += st.candidates; tested += st.tested; ev += st.events; stay += st.stayEvents; }
    }
    if (w.setGrid) w.setGrid(0);
    return { upd: upd / FRAMES, scene: sc / FRAMES, max, cand: cand / FRAMES, tested: tested / FRAMES, ev: ev / FRAMES, stay: stay / FRAMES };
}
const order = [[0, 1], [1, 0], [0, 1]];
let ci = 0, round = 0, world = null, done = false; const sums = {};
Loop.run(() => {
    Screen.clear(Color.new(20, 20, 30));
    if (done) return;
    const c = cases[ci];
    if (!world) { world = build(c); console.log(`E07 BUILD ${c.name} zones=${c.z} bodies=${c.b}`); return; }
    for (const g of order[round]) {
        const r = run(world, g ? CELL : 0); if (!r) continue;
        console.log(`E07 ${c.name} round=${round + 1} grid=${g ? CELL : "off"} updateMs=${r.upd.toFixed(3)} max=${r.max.toFixed(3)} sceneUpdateMs=${r.scene.toFixed(3)} candidates=${r.cand.toFixed(0)} tested=${r.tested.toFixed(0)} events=${r.ev.toFixed(1)} stay=${r.stay.toFixed(1)}`);
        (sums[c.name + (g ? "-grid" : "-off")] ||= []).push([r.upd, r.scene]);
    }
    if (++round >= order.length) {
        for (const m of ["off", "grid"]) { const a = sums[c.name + "-" + m]; if (!a) continue; console.log(`E07SUM ${c.name} grid=${m} updateMs=${(a.reduce((x, y) => x + y[0], 0) / a.length).toFixed(3)} sceneUpdateMs=${(a.reduce((x, y) => x + y[1], 0) / a.length).toFixed(3)}`); }
        world.w.dispose(); world.scene.dispose(); world = null; round = 0;
        if (++ci >= cases.length) { console.log("E07_DONE"); done = true; }
    }
});
