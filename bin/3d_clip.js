// Run with the same modules and ELF as 3d.js. No JS vertex processing per frame.
// Near-plane triangle (center) and a shared mesh sweeping four viewport edges.
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S;
Screen.setMode(mode);
const aspect = mode.width / height;
const horizontalExtent = 4 * Math.tan(Math.PI / 6) * aspect + .8;
const verticalExtent = 4 * Math.tan(Math.PI / 6) + .8;
const camera = new Camera3D.Camera({aspect, near: 1, far: 20});
camera.lookAt(0, 0, -1).setPosition(0, 0, 0);
const colors = new Float32Array([1,0,0,1, 0,1,0,1, 0,0,1,1]);
const nearMesh = Model3D.Mesh.fromGeometry({
    positions: new Float32Array([-.18,-.18,-.4, .5,-.18,-2.5, -.18,.5,-2.5]), colors
});
const edgeMesh = Model3D.Mesh.fromGeometry({
    positions: new Float32Array([-.6,-.6,-4, .6,-.6,-4, 0,.6,-4]), colors
});
const near = nearMesh.createInstance();
const horizontal = edgeMesh.createInstance();
const vertical = edgeMesh.createInstance();
const batch = new Render3D.Batch().add(horizontal).add(vertical).add(near);
nearMesh.dispose(); edgeMesh.dispose();
let time = 0, report = 0;
Loop.run({
    update(dt) {
        time += dt; report += dt;
        near.setPosition(0, 0, Math.sin(time) * .8);
        horizontal.setPosition(Math.sin(time * .7) * horizontalExtent, 0, 0);
        vertical.setPosition(0, Math.cos(time * .9) * verticalExtent, 0);
    },
    draw() {
        const stats = batch.draw(camera, Render3D.CULL_NONE);
        if (report >= 5) {
            report = 0;
            console.log("3D clip: input=" + stats.sourceTriangles + " output=" + stats.triangles +
                " clipped=" + stats.clippedTriangles + " rejected=" + stats.rejectedTriangles +
                " culledObjects=" + stats.culledObjects + " VU batches=" + stats.vuBatches);
        }
    }
});
