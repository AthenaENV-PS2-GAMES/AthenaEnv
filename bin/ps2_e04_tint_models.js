/* E04 visual check 2: Render3D tint on textured (DIFFUSE + UNLIT), skinned and morph draws through
 * Scene3D, then a Voxel world (baked light) at noon and at night with Render3D.setTint(...Sky.tint()).
 * Stages of 90 frames, "E04B_SNAP n" logged at frame 60 of each: 0 white, 1 tinted, 2 voxel noon, 3 voxel night. */
const mode=Screen.getMode();
const height=mode.height*(mode.interlace===Screen.INTERLACED&&mode.field===Screen.FRAME?2:1);
mode.height=height; mode.zbuffering=true; mode.psmz=Screen.Z16S; Screen.setMode(mode);
const camera=new Camera3D.Camera({aspect:mode.width/height,near:.5,far:200}); camera.setViewport(mode.width,height);
const lights=new Lights.Set().setAmbient(.4,.4,.4).setDirectional(0,-.4,-.6,-1,1,1,1);
const scene=new Scene3D.Scene();
function place(asset,x,y,z,s) { const h=new Scene3D.Node().setPosition(x,y,z).setScale(s,s,s); h.add(asset.root); scene.root.add(h); }
const dm={shading:Model3D.DIFFUSE};
const arm=GLTF3D.load("models/arm.glb",{shading:Model3D.DIFFUSE,baseColor:new Float32Array([.9,.6,.3,1])});
const bend=GLTF3D.load("models/bend.glb",{shading:Model3D.DIFFUSE,baseColor:new Float32Array([.4,.7,1,1])});
const morph=GLTF3D.load("models/morph.glb",{shading:Model3D.DIFFUSE});
const cube=Model3D.load("models/textured_cube.glb",dm);
const checker=Model3D.Texture.load("models/checker.png");
const quad=Model3D.Mesh.fromGeometry({positions:new Float32Array([-1,-1,0,1,-1,0,1,1,0,-1,-1,0,1,1,0,-1,1,0]),
    texcoords:new Float32Array([0,1,1,1,1,0,0,1,1,0,0,0]),material:{texture:checker}});
place(arm,-4.5,0,0,1); place(bend,-2.5,0,0,1); place(morph,0,0,0,1);
const cn=new Scene3D.Node(cube).setPosition(2.5,0,0).setScale(.8,.8,.8); scene.root.add(cn);
const qn=new Scene3D.Node(quad).setPosition(5,0,0); scene.root.add(qn);
const armP=new Animation3D.Player(arm.clips.wave,arm.nodes); armP.loop=true; armP.play();
const bendP=new Animation3D.Player(bend.clips.bend,bend.nodes); bendP.loop=true; bendP.speed=.5; bendP.play();
const morphP=new Animation3D.Player(morph.clips.morph,morph.nodes); morphP.loop=true; morphP.play();
Animation3D.attachLoop();
scene.attachLoop();
const world=new Voxel.World({size:[32,24,32],chunk:8});
world.setMaterial(1,{color:{top:[.35,.72,.25],side:[.47,.36,.22],bottom:[.47,.36,.22]}});
world.setMaterial(2,{color:[.52,.52,.55]});
world.generate({seed:7,caves:0,frequency:1/20});
world.rebuild(0);
scene.update();
const stats={};
let frame=0;
Loop.run(()=>{
    const stage=Math.floor(frame/90),f=frame%90;
    if(stage<2) {
        Screen.clear(Color.new(40,40,60));
        camera.setPosition(0,1.5,11).lookAt(0,.8,0);
        if(stage===1) Render3D.setTint(.3,.5,1);
        scene.draw(camera,Render3D.CULL_NONE,lights,stats);
        Render3D.setTint(1,1,1);
    } else {
        Sky.setTime(stage===2?12:1);
        Screen.clear(Sky.clearColor());
        camera.setPosition(16,22,40).lookAt(16,6,16);
        Render3D.setTint(...Sky.tint());
        world.draw(camera,Render3D.CULL_BACK,undefined,stats,120);
        Render3D.setTint(1,1,1);
    }
    if(f===60) console.log("E04B_SNAP "+stage+" tint="+Render3D.getTint().join(",")+" draws="+stats.drawPasses);
    if(++frame>=360) { console.log("E04B_DONE"); Loop.stop(); }
});
