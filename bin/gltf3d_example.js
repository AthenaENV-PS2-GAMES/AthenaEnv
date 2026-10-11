// GLTF3D: a glTF scene with a node hierarchy and animations, played by
// Animation3D on Scene3D nodes, with an orbiting CameraRig3D. Everything per
// frame runs in native systems; the script only sets things up.
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect: mode.width / height, near: .5, far: 50});
const lights = new Lights.Set().setAmbient(.3, .3, .3).setDirectional(0, .4, .6, 1, .8, .8, .8);
const asset = GLTF3D.load("models/arm.glb", {shading: Model3D.DIFFUSE, baseColor: new Float32Array([.9, .6, .3, 1])});
console.log("GLTF3D example: nodes " + asset.names.join(", ") + "; clips " + Object.keys(asset.clips).join(", "));
const scene = new Scene3D.Scene(), root = scene.root;
root.add(asset.root); root.dispose();
const wave = new Animation3D.Player(asset.clips.wave, asset.nodes);
wave.loop = true; wave.play();
// A skinned column beside it, bent by its joints: deformed every frame in C.
const column = GLTF3D.load("models/bend.glb", {shading: Model3D.DIFFUSE, baseColor: new Float32Array([.4, .7, 1, 1])});
const holder = new Scene3D.Node().setPosition(-2.5, 0, 0);
holder.add(column.root);
scene.root.add(holder);
const bend = new Animation3D.Player(column.clips.bend, column.nodes);
bend.loop = true; bend.speed = .5; bend.play();
Animation3D.attachLoop();
scene.attachLoop();
new CameraRig3D.Orbit(camera, asset.nodes[0]).setCenter(.5, 1, 0).setAngles(.6, .35).zoom(3);
CameraRig3D.attachLoop();
const stats = {};
let frame = 0;
Loop.run({
    draw() {
        scene.draw(camera, Render3D.CULL_BACK, lights, stats);
        if (++frame % 60 === 0) console.log("GLTF3D example frame " + frame + ": t=" + wave.time.toFixed(2) +
            " drawn " + stats.drawPasses + " meshes");
        if (frame === 600) { console.log("GLTF3D example complete;"); Loop.stop(); }
    }
});
