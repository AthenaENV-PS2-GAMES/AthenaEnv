// Perf round check (PCSX2): idle bodies/agents bound to Scene3D nodes, and Particles3D update.
// Prints PERF lines with mean COP0 ticks per frame.
import { ticks, ticksToMilliseconds } from "ProfilerNative";
const ms = t => ticksToMilliseconds((ticks() - t) >>> 0);
const mean = v => v.reduce((a, b) => a + b, 0) / v.length;
const scene = new Scene3D.Scene(), root = scene.root, level = new Collision3D.World();
level.addBox([-40, -1, -40], [40, 0, 40]);
const world = new Physics3D.World(level), bodies = [], nodes = [];
for (let i = 0; i < 200; i++) {
    const node = new Scene3D.Node(); root.add(node); nodes.push(node);
    const b = world.addBox({halfExtents: [.4, .4, .4], position: [(i % 14) * 1.5 - 10, .4 + .02, Math.floor(i / 14) * 1.5 - 10]});
    b.bind(node); bodies.push(b);
}
const grid = new Nav.Grid(64, 64), crowd = new Nav.Crowd(grid), agents = [];
for (let i = 0; i < 64; i++) {
    const node = new Scene3D.Node(); root.add(node); nodes.push(node);
    agents.push(crowd.add({x: 2 + i % 8 * 3, z: 2 + (i >> 3) * 3, radius: .5, node}));
}
scene.update();
let frame = 0; const s = {step: [], upd: [], crowd: [], upd2: []};
Loop.run(() => {
    frame++;
    let t = ticks(); world.step(1 / 60); const a = ms(t);
    t = ticks(); scene.update(); const b = ms(t);
    t = ticks(); crowd.update(1 / 60); const c = ms(t);
    t = ticks(); scene.update(); const d = ms(t);
    if (frame > 300) { s.step.push(a); s.upd.push(b); s.crowd.push(c); s.upd2.push(d); }
    if (frame === 300) console.log("PERF asleep " + bodies.filter(x => x.sleeping).length + "/200");
    if (frame === 600) {
        console.log("PERF step " + mean(s.step).toFixed(3) + " ms; scene.update after step " + mean(s.upd).toFixed(3) +
            " ms; crowd.update " + mean(s.crowd).toFixed(3) + " ms; scene.update after crowd " + mean(s.upd2).toFixed(3) + " ms");
        console.log("PERF done;"); Loop.stop();
    }
});
