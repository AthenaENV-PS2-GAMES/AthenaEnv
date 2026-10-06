// Deterministic counterpart of samples/native/3d_lighting/main.c.
// Nine 180-frame stages. GLB mesh shared by two independent instances.
const mode = Screen.getMode();
const height = mode.height * (mode.interlace === Screen.INTERLACED && mode.field === Screen.FRAME ? 2 : 1);
mode.height = height; mode.zbuffering = true; mode.psmz = Screen.Z16S; Screen.setMode(mode);
const camera = new Camera3D.Camera({aspect:mode.width/height,near:1,far:20});
camera.lookAt(0,0,-1).setPosition(0,0,0);
const cameraB = new Camera3D.Camera({aspect:mode.width/height,near:1,far:20});
cameraB.lookAt(.4,0,-1).setPosition(.4,0,0);
const lights = new Lights.Set(), otherLights = new Lights.Set().setAmbient(.1,.1,.3).setDirectional(0,0,0,1,.2,.3,.7);
const tile = new TileMap.Instance({
    descriptor:new TileMap.Descriptor({materials:[{textureIndex:-1,endOffset:0}]}),
    spriteBuffer:TileMap.SpriteBuffer.fromObjects([{x:0,y:0,w:32,h:16,r:255,g:128,b:0,a:128}])
});
const names = ["unlit","ambient","directional","rotating-light","four-lights",
    "nonuniform","clip-sweep","camera-tile-camera","recreate"];
const white = Color.new(255,255,255), orange = Color.new(255,128,0), gray = Color.new(64,64,64);
let stage = -1, frame = 0, objects = [], batch = null;
function release() {
    if (batch) batch.dispose(); batch = null;
    for (const object of objects) object.dispose(); objects = [];
}
function create() {
    const material = {shading:stage === 0 ? Model3D.UNLIT : Model3D.DIFFUSE,
        baseColor:new Float32Array([.75,.9,1,1])};
    const mesh = Model3D.load("models/lit_cube.glb",material);
    const triangle = Model3D.Mesh.fromGeometry({
        positions:new Float32Array([-.18,-.18,-.4,.5,-.18,-2.5,-.18,.5,-2.5]),
        colors:new Float32Array([1,0,0,1,0,1,0,1,0,0,1,1]), material
    });
    objects = [mesh.createInstance().setPosition(-1.7,0,-6).setScale(.75,.75,.75),
        mesh.createInstance().setPosition(1.7,0,-6).setScale(.75,.75,.75),
        triangle.createInstance().setPosition(0,-.6,-2)];
    mesh.dispose(); triangle.dispose();
    batch = new Render3D.Batch(); for (const object of objects) batch.add(object);
}
function begin(next) {
    release(); stage = next; lights.clear(); lights.setAmbient(.2,.2,.2);
    if (stage >= 2) lights.setDirectional(0,0,0,1,.8,.8,.8);
    if (stage === 4) {
        lights.setDirectional(0,0,0,1,.6,.1,.1).setDirectional(1,1,0,0,.1,.6,.1)
            .setDirectional(2,0,1,0,.1,.1,.6).setDirectional(3,-1,0,1,.3,.3,.3);
    }
    create(); console.log("3D lighting stage " + stage + ": " + names[stage]);
}
Loop.run({draw() {
    const next = Math.floor(frame/180), local = frame%180, angle = local * Math.PI / 90;
    if (stage !== next) begin(next);
    if (objects.length === 0) create();
    objects[0].setRotationEuler(.35,angle*.25,0);
    objects[1].setRotationEuler(.35,-angle*.25,0);
    if (stage === 3) lights.setDirectional(0,Math.sin(angle),.4,Math.cos(angle),.8,.8,.8);
    if (stage === 4 && local === 90) { lights.disable(3); console.log("3D lighting: slot 3 disabled"); }
    if (stage === 5) objects[1].setScale(.35,1.2,.6);
    if (stage === 6) objects[2].setPosition(0,-.6,-1.6+1.6*Math.sin(angle));
    let stats;
    if (stage === 7) {
        stats = Render3D.draw(objects[0],camera,Render3D.CULL_BACK,lights);
        tile.render(mode.width/2-16,36);
        Render3D.draw(objects[1],cameraB,Render3D.CULL_BACK,otherLights);
        Render3D.draw(objects[2],camera,Render3D.CULL_NONE,lights);
    } else stats = batch.draw(camera,Render3D.CULL_NONE,lights);
    if (local%60 === 0) console.log("3D lighting: stage=" + names[stage] + " " + JSON.stringify(stats));
    if (stage === 8 && local%30 === 29) release(); // Submitted normals/uniforms survive wrapper disposal.
    for (let i = 0; i < names.length; i++) Draw.rect(12+i*20,12,14,14,i === stage ? orange : gray);
    Draw.rect(12,height-20,Math.max(1,Math.floor((mode.width-24)*(local+1)/180)),8,white);
    if (++frame === names.length*180) {
        release(); lights.dispose(); otherLights.dispose(); camera.dispose(); cameraB.dispose();
        console.log("3D lighting complete; inspect images before recording visual PASS."); Loop.stop();
    }
}});
