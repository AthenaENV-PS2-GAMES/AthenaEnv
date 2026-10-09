// Configure with --modules=bench,lod,nav,triggers3d,meshbuilder,usbmass.
// Run on a writable boot device. This measures CPU work, without rendering
// or GS waits; report the platform and revision alongside results.json.
const metadata = { platform: "set-to-PCSX2-or-PS2", revision: "record-build-commit",
    timing: "CPU, JS bindings included; no GS wait", note: "single run; compare repeated runs of the same scene" };
let report = null;
const tasks = [
    { name: "empty", iterations: 100, run() {} },
    {
        name: "lod.300",
        setup() {
            const scene = new Scene3D.Scene(), camera = new Camera3D.Camera();
            const builder = new MeshBuilder.Builder();
            const mesh = builder.box(-.5, -.5, -.5, .5, .5, .5).build()[0];
            builder.dispose();
            const groups = [];
            for (let i = 0; i < 300; i++) {
                const node = new Scene3D.Node(mesh).setPosition(i % 20, 0, -(i / 20 | 0));
                scene.root.add(node);
                groups.push(new LOD.Group(node, [{ mesh, until: 10 }, { mesh, until: 25 }]));
            }
            scene.update();
            return { scene, camera, mesh, groups, frame: 0 };
        },
        run(s) { s.camera.setPosition(0, 0, 10 + s.frame++ % 10); LOD.update(s.camera); },
        teardown(s) { if (s) { for (const g of s.groups) g.dispose(); s.scene.dispose(); s.mesh.dispose(); s.camera.dispose(); } },
    },
    {
        name: "nav.maze128", warmup: 3, samples: 30,
        setup() {
            const grid = new Nav.Grid(128, 128);
            for (let x = 8; x < 128; x += 16) {
                const odd = Math.floor(x / 16) % 2;
                grid.fill(x, odd ? 8 : 0, x, odd ? 127 : 119, 0);
            }
            return grid;
        },
        run(grid) { if (!grid.findPath(1.5, 1.5, 126.5, 126.5)) throw new Error("Expected maze path"); },
        teardown(grid) { if (grid) grid.dispose(); },
    },
    {
        name: "triggers.50x20",
        setup() {
            const world = new Triggers3D.World(), bodies = [];
            for (let i = 0; i < 50; i++) world.sphere(i * 3, 0, 0, 1);
            for (let i = 0; i < 20; i++) bodies.push(world.body(i * 7, 0, 0, { radius: .5 }));
            return { world, bodies, frame: 0 };
        },
        run(s) { const f = s.frame++; s.bodies[f % 20].setPosition((f * 1.3) % 150, 0, 0); s.world.update(); },
        teardown(s) { if (s) s.world.dispose(); },
    },
];
Bench.run(tasks, { label: "world-systems", metadata, path: "world_systems_results.json",
    onResult(r) { console.log(`[Bench] ${r.name}: ${r.averageMs.toFixed(3)} ms, p95 ${r.p95Ms.toFixed(3)}, max ${r.maxMs.toFixed(3)} (${r.status})`); },
}).then(r => { report = r; console.log("[Bench] report written: world_systems_results.json (" + report.status + ")"); Loop.stop(); })
  .catch(e => { console.log("[FAIL] Bench: " + e); Loop.stop(); });
Loop.run(() => {});
