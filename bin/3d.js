// Build with screen,loop,render3d (plus a boot-device module, e.g. usbmass).
// Experimental opaque color pipeline. Run this file as the entry script.
const mode = Screen.getMode();
// getMode exposes the internal height; setMode expects the complete frame
// height in interlaced/frame mode.
const viewportHeight = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = viewportHeight;
mode.zbuffering = true;
mode.psmz = Screen.Z16S;
Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect: mode.width / viewportHeight});
const geometry = {
    positions: new Float32Array([
        -1,-1,-1, 1,-1,-1, 1,1,-1, -1,1,-1,
        -1,-1, 1, 1,-1, 1, 1,1, 1, -1,1, 1
    ]),
    colors: new Float32Array([
        1,0,0,1, 0,1,0,1, 0,0,1,1, 1,1,0,1,
        1,0,1,1, 0,1,1,1, 1,1,1,1, 0.4,0.6,1,1
    ]),
    indices: new Uint32Array([
        0,2,1, 0,3,2, 4,5,6, 4,6,7,
        0,1,5, 0,5,4, 3,7,6, 3,6,2,
        0,4,7, 0,7,3, 1,2,6, 1,6,5
    ])
};
const mesh = Model3D.Mesh.fromGeometry(geometry);
const left = mesh.createInstance().setPosition(-1.25,0,0).setScale(0.7,0.7,0.7);
const right = mesh.createInstance().setPosition(1.25,0,0).setScale(0.7,0.7,0.7);
const batch = new Render3D.Batch().add(left).add(right);
mesh.dispose(); // Both instances keep the same native geometry alive.
let angle = 0;
Loop.run({
    update(dt) {
        angle += dt;
        left.setRotationEuler(angle * 0.3, angle, 0);
        right.setRotationEuler(0, -angle, angle * 0.2);
    },
    draw() { batch.draw(camera, Render3D.CULL_NONE); }
});
// Handles are also released at runtime teardown. For scene changes call
// batch.dispose(), left.dispose(), right.dispose(), camera.dispose().
