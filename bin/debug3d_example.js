// Build with screen,loop,render3d,scene3d,debug3d,profiler (plus a boot-device module, e.g. usbmass).
// Debug3D shapes over a scene, mouse-free picking with the screen centre ray,
// and Profiler lines in the Debug overlay. Run this file as the entry script.
const mode = Screen.getMode();
const viewportHeight = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = viewportHeight;
mode.zbuffering = true;
mode.psmz = Screen.Z16S;
Screen.setMode(mode);

const camera = new Camera3D.Camera({aspect: mode.width / viewportHeight, near: .5, far: 100});
camera.setViewport(mode.width, viewportHeight).setPosition(6, 5, 9).lookAt(0, 0, 0);
const other = new Camera3D.Camera({near: 1, far: 4});
other.setPosition(-3, 1, 2).lookAt(0, 0, 0);

const quad = Model3D.Mesh.fromGeometry({
    positions: new Float32Array([-1,-1,0, 1,-1,0, 1,1,0, -1,-1,0, 1,1,0, -1,1,0]),
    colors: new Float32Array(Array(6).fill([0.2, 0.4, 0.9, 1]).flat()),
});
const scene = new Scene3D.Scene();
const nodes = [-3, 0, 3].map(x => new Scene3D.Node(quad).setPosition(x, 0, 0));
for (const node of nodes) scene.root.add(node);
scene.attachLoop();

Debug3D.setCamera(camera);                    // draws the queue after every frame
Profiler.auto();
Profiler.attachRender3D(Render3D);
Profiler.overlay(true);

const GRID = Color.new(80, 80, 80), BOUNDS = Color.new(255, 255, 0), HIT = Color.new(255, 64, 0);
const ray = {}, hit = {};
let time = 0;

Loop.run({
    update(dt) {
        time += dt;
        Profiler.begin("spin");
        nodes[1].setRotationEuler(0, time, 0);
        Profiler.end("spin");
    },
    draw() {
        Screen.clear(Color.new(20, 20, 30));
        Profiler.measure("scene", () => scene.draw(camera, Render3D.CULL_NONE, undefined, null));

        Debug3D.grid(0, -1, 0, 5, 1, GRID);
        Debug3D.frustum(other, Color.new(0, 255, 255));
        Debug3D.axes(nodes[1].getWorldTransform(), 1.5);
        for (const node of nodes) {
            const b = node.getWorldBounds();
            if (b) Debug3D.box(b.min[0], b.min[1], b.min[2], b.max[0], b.max[1], b.max[2], BOUNDS);
        }
        // Picking through the screen centre: mark the hit point and normal.
        camera.screenToRay(mode.width / 2, viewportHeight / 2, ray);
        if (scene.raycast(ray, undefined, undefined, hit)) {
            Debug3D.sphere(hit.x, hit.y, hit.z, .1, HIT);
            Debug3D.line(hit.x, hit.y, hit.z, hit.x + hit.nx, hit.y + hit.ny, hit.z + hit.nz, HIT);
        }
    },
});
