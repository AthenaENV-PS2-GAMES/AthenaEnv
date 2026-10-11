// Equivalent stages and geometry to samples/native/3d_textures/main.c.
const mode=Screen.getMode();
const height=mode.height*(mode.interlace===Screen.INTERLACED&&mode.field===Screen.FRAME?2:1);
mode.height=height; mode.zbuffering=true; mode.psmz=Screen.Z16S; Screen.setMode(mode);
const camera=new Camera3D.Camera({aspect:mode.width/height,near:1,far:20}).lookAt(0,0,-1).setPosition(0,0,0);
const cameraB=new Camera3D.Camera({aspect:mode.width/height,near:1,far:20}).lookAt(.4,0,-1).setPosition(.4,0,0);
const lights=new Lights.Set().setAmbient(.2,.2,.2).setDirectional(0,0,0,1,.8,.8,.8);
const image=new Image('models/checker.png');
const tile=new TileMap.Instance({descriptor:new TileMap.Descriptor({materials:[{textureIndex:0,endOffset:0}],textures:[image]}),
    spriteBuffer:TileMap.SpriteBuffer.fromObjects([{x:0,y:0,w:48,h:48,u1:0,v1:0,u2:64,v2:64,r:128,g:128,b:128,a:128}])});
const names=['unlit','diffuse','perspective','clip-sweep','filters','camera-tile-camera','retained-texture','recreate','video-reset'];
const orange=Color.new(255,128,0),gray=Color.new(64,64,0),white=Color.new(255,255,255);
let objects=[],batch=null,texture=null,stage=-1,frame=0;
function release() {
    if(batch) batch.dispose(); batch=null;
    for(const o of objects) o.dispose(); objects=[];
    if(texture) texture.dispose(); texture=null;
}
function create() {
    const material={shading:stage===1?Model3D.DIFFUSE:Model3D.UNLIT};
    const left=Model3D.load('models/textured_cube.glb',material);
    if(stage===6) {
        const pixels=new Uint32Array(4096),palette=[[255,64,64],[64,255,64],[64,64,255],[255,255,64]];
        for(let y=0;y<64;y++) for(let x=0;x<64;x++) {
            const c=palette[(y>=32?2:0)+(x>=32?1:0)],factor=((x>>3)^(y>>3))&1?1:.35;
            pixels[y*64+x]=(0xff000000|Math.round(c[0]*factor)|(Math.round(c[1]*factor)<<8)|(Math.round(c[2]*factor)<<16))>>>0;
        }
        texture=Model3D.Texture.fromPixels({width:64,height:64,pixels});
    } else texture=Model3D.Texture.load('models/checker.png',stage===4?Model3D.Texture.LINEAR:Model3D.Texture.NEAREST);
    material.texture=texture;
    const right=Model3D.load('models/textured_cube.glb',material);
    const quad=Model3D.Mesh.fromGeometry({positions:new Float32Array([-.9,-.6,-2,.9,-.6,-5,.9,.6,-5,-.9,-.6,-2,.9,.6,-5,-.9,.6,-2]),
        texcoords:new Float32Array([0,1,1,1,1,0,0,1,1,0,0,0]),material});
    objects=[left.createInstance().setPosition(-1.7,.5,-6).setScale(.65,.65,.65),
        right.createInstance().setPosition(1.7,.5,-6).setScale(.65,.65,.65),quad.createInstance().setPosition(0,-.7,-1)];
    left.dispose(); right.dispose(); quad.dispose(); batch=new Render3D.Batch(); for(const o of objects) batch.add(o);
}
Loop.run({draw() {
    const next=Math.floor(frame/180),local=frame%180,angle=local*Math.PI/90;
    if(stage!==next) { release(); stage=next; create(); console.log('3D textures stage '+stage+': '+names[stage]); }
    if(!batch) create();
    objects[0].setRotationEuler(.3,angle*.25,0); objects[1].setRotationEuler(.3,-angle*.25,0);
    if(stage===2) objects[2].setRotationEuler(0,.35*Math.sin(angle),0);
    if(stage===3) objects[2].setPosition(.8*Math.sin(angle),-.7,1.5*Math.sin(angle));
    let stats;
    if(stage===5) {
        stats=Render3D.draw(objects[0],camera,Render3D.CULL_NONE,lights); tile.render(mode.width/2-24,36);
        Render3D.draw(objects[1],cameraB,Render3D.CULL_NONE,lights); Render3D.draw(objects[2],camera,Render3D.CULL_NONE,lights);
    } else stats=batch.draw(camera,Render3D.CULL_NONE,lights);
    if(local%60===0) console.log('3D textures: stage='+names[stage]+' '+JSON.stringify(stats));
    if(stage===6&&local===90) { texture.dispose(); texture=null; console.log('3D textures: texture handle released; meshes retain it'); }
    if(stage===7&&local%30===29) release();
    if(stage===8&&local===90) { Screen.setMode(mode); console.log('3D textures: video reset; retained pixels will re-upload'); }
    for(let i=0;i<names.length;i++) Draw.rect(12+i*20,12,14,14,i===stage?orange:gray);
    Draw.rect(12,height-20,Math.max(1,Math.floor((mode.width-24)*(local+1)/180)),8,white);
    if(++frame===names.length*180) {
        release(); lights.dispose(); camera.dispose(); cameraB.dispose();
        console.log('3D textures complete; inspect images before recording visual PASS.'); Loop.stop();
    }
}});
