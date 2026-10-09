// Build with screen,loop,render3d,scene3d,lights,meshbuilder,lod,nav,triggers3d,sky,debug3d
// (plus a boot-device module, e.g. usbmass). Phase 4 systems together: a day
// cycle (Sky), trees with levels of detail (LOD), NPCs walking around a wall
// (Nav), a zone that lights up when an NPC enters it (Triggers3D) and the
// paths drawn with Debug3D. Run this file as the entry script.
const mode = Screen.getMode();
const viewportHeight = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = viewportHeight;
mode.zbuffering = true;
mode.psmz = Screen.Z16S;
Screen.setMode(mode);

const camera = new Camera3D.Camera({ aspect: mode.width / viewportHeight, near: 0.5, far: 200 });
camera.setViewport(mode.width, viewportHeight).lookAt(0, 0, 0).setPosition(0, 22, 34);
const lights = new Lights.Set().setFog(40, 90, 0.6, 0.7, 0.8);
const LIT = { shading: Model3D.DIFFUSE };

// Meshes: ground, a wall, trees in three levels, NPCs.
const b = new MeshBuilder.Builder();
const ground = b.color(0.35, 0.6, 0.3).plane(0, 0, 0, 64, 64, 8, 8).build(LIT)[0];
const wall = b.clear().color(0.6, 0.55, 0.5).box(-0.5, 0, -12, 0.5, 2, 10).build(LIT)[0];
const treeHigh = b.clear().color(0.45, 0.3, 0.15).cylinder(0, 0, 0, 0.25, 1.5, 8)
    .color(0.2, 0.5, 0.2).sphere(0, 2.3, 0, 1.1, 12, 8).build(LIT)[0];
const treeLow = b.clear().color(0.2, 0.5, 0.2).box(-0.8, 0, -0.8, 0.8, 3, 0.8).build(LIT)[0];
const npcMesh = b.clear().color(0.9, 0.3, 0.2).box(-0.3, 0, -0.3, 0.3, 1.6, 0.3).build(LIT)[0];
b.dispose();

const scene = new Scene3D.Scene();
scene.root.add(new Scene3D.Node(ground)).add(new Scene3D.Node(wall));
const lodGroups = [];                                   // keep handles alive until leaving the world
for (let i = 0; i < 80; i++) {
    const x = ((i * 37) % 60) - 30, z = ((i * 53) % 60) - 30;
    if (Math.abs(x) < 3) continue;                       // keep the wall clear
    const tree = new Scene3D.Node(treeHigh).setPosition(x, 0, z);
    scene.root.add(tree);
    lodGroups.push(new LOD.Group(tree, [{ mesh: treeHigh, until: 25 }, { mesh: treeLow, until: 60 }]));
}
scene.attachLoop();
LOD.setCamera(camera);
LOD.setDrawDistance(90);

// Navigation: the wall blocks x = 0 from z = -12 to 10.
const grid = new Nav.Grid(64, 64, { cellSize: 1, x: -32, z: -32 });
grid.fill(31, 20, 32, 42, 0);
const crowd = new Nav.Crowd(grid);
const npcs = [];
for (let i = 0; i < 6; i++) {
    const node = new Scene3D.Node(npcMesh);
    scene.root.add(node);
    npcs.push(crowd.add({ x: -20 + i * 2, y: 0, z: -20, speed: 4 + i * 0.4, radius: 0.5, node }));
}
const triggers = new Triggers3D.World();
let inside = 0;
const zone = triggers.box(8, 0, -4, 14, 3, 4, { onEnter: () => inside++, onExit: () => inside-- });
const bodies = npcs.map((agent, i) => triggers.body(agent.x, 0.5, agent.z, { radius: 0.4, data: i }));

Debug3D.setCamera(camera);
let hours = 7, t = 0;
const random = (seed => () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff)(7);

Loop.run({
    update(dt) {
        t += dt;
        hours = (hours + dt * 0.5) % 24;                     // an in-game hour every 2 seconds
        Sky.setTime(hours);
        Sky.apply(lights);
        for (const agent of npcs) {
            if (agent.state !== "moving") agent.moveTo(random() * 50 - 25, random() * 50 - 25);
        }
        crowd.update(dt);
        npcs.forEach((agent, i) => bodies[i].setPosition(agent.x, 0.5, agent.z));
        triggers.update();
        const a = t * 0.1;
        camera.lookAt(0, 0, 0).setPosition(Math.sin(a) * 38, 22, Math.cos(a) * 38);
    },
    draw() {
        Screen.clear(Sky.clearColor());
        Sky.draw(camera);
        scene.draw(camera, Render3D.CULL_BACK, lights, null);
        Debug3D.box(8, 0, -4, 14, 3, 4, inside ? Color.new(255, 255, 0) : Color.new(80, 80, 255));
    },
});
