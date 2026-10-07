// Visual checks for the 3D improvements of portingDocs/3D_IMPROVEMENTS.md.
// Fixtures: node tools/make_3d_features_asset.js. Each stage lasts 180 frames
// and logs "3D_FEATURES {json}" once; the run ends with "3D features complete".
//   0 wrap         left: CLAMP, right: REPEAT, same UVs -1..2 (3x3 tiles right)
//   1 palette      8-bit paletted PNG: red, green, blue, yellow bands (any
//                  magenta means a wrong CLUT unswizzle)
//   2 alpha-mask   blue/white checker with round holes over a red quad; the
//                  holes show red (no colour and no depth written)
//   3 embedded     GLB with its PNG in the binary chunk, default sampler
//                  (repeat, UVs 0..2: 2x2 tiles), alphaMode MASK
//   4 cubic        two cubes moving left-right: top LINEAR, bottom CUBIC
//                  (eases at the ends)
//   5 group        64 diffuse cubes drawn one by one inside Render3D.group:
//                  pipelinePasses 1, lights uploaded once
//   6 camera-group same textured quad twice: move the same camera inside a
//                  group; two separated quads and pipelinePasses 2
//   7 churn        a new texture every 10 frames, the old one disposed:
//                  deferred VRAM release, no stall, memory flat
const mode=Screen.getMode();
const height=mode.height*(mode.interlace===Screen.INTERLACED&&mode.field===Screen.FRAME?2:1);
mode.height=height; mode.zbuffering=true; mode.psmz=Screen.Z16S; Screen.setMode(mode);
const camera=new Camera3D.Camera({aspect:mode.width/height,near:.5,far:50}).lookAt(0,0,-1).setPosition(0,0,0);
const lights=new Lights.Set().setAmbient(.25,.25,.25).setDirectional(0,-.4,.6,1,.8,.8,.8);
const timer=Timer.new();
const names=['wrap','palette','alpha-mask','embedded','cubic','group','camera-group','churn','churn-long'];
const orange=Color.new(255,128,0),gray=Color.new(64,64,64),white=Color.new(255,255,255);
function quad(material,uv,scale) {
    const s=scale||1;
    return Model3D.Mesh.fromGeometry({positions:new Float32Array([-s,-s,0, s,-s,0, s,s,0, -s,-s,0, s,s,0, -s,s,0]),
        texcoords:new Float32Array(uv||[0,1, 1,1, 1,0, 0,1, 1,0, 0,0]),material});
}
const tiled=[-1,2, 2,2, 2,-1, -1,2, 2,-1, -1,-1];
const cube=Model3D.load('models/textured_cube.glb',{shading:Model3D.DIFFUSE});
let res=[],stage=-1,frame=0,samples=[],last=null,churn=0;
function release() { for(const r of res) r.dispose(); res=[]; }
function keep(...items) { res.push(...items); return items[0]; }
let draw=null;
function create() {
    if(stage===0) {
        const clamp=keep(Model3D.Texture.load('models/checker.png',Model3D.Texture.NEAREST,Model3D.Texture.CLAMP));
        const repeat=keep(Model3D.Texture.load('models/checker.png',Model3D.Texture.NEAREST,Model3D.Texture.REPEAT));
        const a=keep(quad({texture:clamp},tiled,.8).createInstance().setPosition(-1,0,-4));
        const b=keep(quad({texture:repeat},tiled,.8).createInstance().setPosition(1,0,-4));
        draw=()=>{ const s=Render3D.draw(a,camera,Render3D.CULL_NONE); Render3D.draw(b,camera,Render3D.CULL_NONE); return s; };
    } else if(stage===1) {
        const t=keep(Model3D.Texture.load('models/palette8.png'));
        const a=keep(quad({texture:t},null,1.2).createInstance().setPosition(0,0,-4));
        draw=()=>Render3D.draw(a,camera,Render3D.CULL_NONE);
    } else if(stage===2) {
        const red=keep(Model3D.Texture.fromPixels({width:1,height:1,pixels:new Uint32Array([0xff2020e0])}));
        const t=keep(Model3D.Texture.load('models/alpha_mask.png'));
        const back=keep(quad({texture:red},null,1.4).createInstance().setPosition(0,0,-5));
        const front=keep(quad({texture:t,alphaCutoff:.5},null,1).createInstance().setPosition(0,0,-3.5));
        // Front first: holes must not write depth, or the red quad behind would vanish there.
        draw=()=>{ const s=Render3D.draw(front,camera,Render3D.CULL_NONE); Render3D.draw(back,camera,Render3D.CULL_NONE); return s; };
    } else if(stage===3) {
        const mesh=Model3D.load('models/embedded.glb');
        const a=keep(mesh.createInstance().setPosition(0,0,-3.2)); mesh.dispose();
        draw=()=>{ a.setRotationEuler(0,.4*Math.sin(frame/30),0); return Render3D.draw(a,camera,Render3D.CULL_NONE); };
    } else if(stage===4) {
        const scene=keep(new Scene3D.Scene()),root=scene.root;
        const top=new Scene3D.Node(cube).setScale(.4,.4,.4),bottom=new Scene3D.Node(cube).setScale(.4,.4,.4);
        root.add(top); root.add(bottom);
        const times=new Float32Array([0,1.5]),values=new Float32Array([-1.5,.7,-5, 1.5,.7,-5]);
        const linear=keep(new Animation3D.Clip([{path:'position',times,values}]));
        const cubic=keep(new Animation3D.Clip([{path:'position',interpolation:'cubic',times,
            values:new Float32Array([-1.5,-.7,-5, 1.5,-.7,-5]),inTangents:new Float32Array(6),outTangents:new Float32Array(6)}]));
        const p1=keep(new Animation3D.Player(linear,[top])),p2=keep(new Animation3D.Player(cubic,[bottom]));
        p1.loop=p2.loop=true; p1.play(); p2.play(); keep(top,bottom); root.dispose();
        draw=()=>{
            Animation3D.advance(1/60); scene.update();
            // At u = 0.25 of the 1.5 s segment: linear -0.75, cubic -1.5 + 3 * 0.15625 = -1.031.
            if(Math.abs(p1.time-.375)<.0084) console.log('3D features cubic: t='+p1.time.toFixed(3)+' linearX='+
                top.getLocalTransform().get(12).toFixed(3)+' cubicX='+bottom.getLocalTransform().get(12).toFixed(3));
            return scene.draw(camera,Render3D.CULL_BACK,lights);
        };
    } else if(stage===5) {
        const items=[];
        for(let i=0;i<64;i++) items.push(keep(cube.createInstance().setScale(.18,.18,.18)
            .setPosition(((i&7)-3.5)*.55,((i>>3)-3.5)*.45,-6)));
        draw=()=>{
            let total=null;
            Render3D.group(()=>{ for(const o of items) { o.setRotationEuler(.4,frame*.03,0);
                const s=Render3D.draw(o,camera,Render3D.CULL_BACK,lights);
                if(!total) total=s; else for(const k in s) total[k]+=s[k]; } });
            return total;
        };
    } else if(stage===6) {
        const t=keep(Model3D.Texture.load('models/checker.png'));
        const a=keep(quad({texture:t},null,.65).createInstance().setPosition(0,0,-4));
        draw=()=>{
            let total;
            Render3D.group(()=>{
                camera.setPosition(1,0,0).lookAt(1,0,-1);
                total=Render3D.draw(a,camera,Render3D.CULL_NONE);
                camera.setPosition(-1,0,0).lookAt(-1,0,-1);
                const second=Render3D.draw(a,camera,Render3D.CULL_NONE);
                for(const k in second) total[k]+=second[k];
            });
            camera.setPosition(0,0,0).lookAt(0,0,-1);
            if(total.pipelinePasses!==2) throw new Error('camera-group: expected 2 pipeline passes');
            return total;
        };
    } else if(stage>=7) {
        const a={instance:null,texture:null,mesh:null};
        const make=()=>{
            const old=a; const color=[0xff3030e0,0xff30e030,0xffe03030,0xff30e0e0][churn++&3];
            const pixels=new Uint32Array(128*128).fill(color);
            const texture=Model3D.Texture.fromPixels({width:128,height:128,pixels});
            const mesh=quad({texture},null,1); const instance=mesh.createInstance().setPosition(0,0,-4);
            if(old.instance) { old.instance.dispose(); old.mesh.dispose(); old.texture.dispose(); }
            a.instance=instance; a.mesh=mesh; a.texture=texture;
        };
        make(); res.push({dispose(){ a.instance.dispose(); a.mesh.dispose(); a.texture.dispose(); }});
        draw=()=>{
            if(frame%10===0) make();
            // Deferred releases keep at most a texture or two: memory must stay flat.
            if(frame%60===5) console.log('3D features churn: textures='+churn+' memory='+System.getMemoryStats().used);
            return Render3D.draw(a.instance,camera,Render3D.CULL_NONE);
        };
    }
}
Loop.run({draw() {
    const next=Math.floor(frame/180),local=frame%180;
    if(stage!==next) {
        if(samples.length) {
            const sorted=samples.slice().sort((x,y)=>x-y),mean=samples.reduce((x,y)=>x+y,0)/samples.length;
            console.log('3D_FEATURES '+JSON.stringify({stage:names[stage],samples:samples.length,drawTicksMean:+mean.toFixed(3),
                drawTicksP95:sorted[Math.floor(sorted.length*.95)],stats:last,memory:System.getMemoryStats().used}));
        }
        release(); stage=next; samples=[];
        if(stage<names.length) { create(); console.log('3D features stage '+stage+': '+names[stage]); }
    }
    if(stage>=names.length) {
        release(); cube.dispose(); lights.dispose(); camera.dispose();
        console.log('3D features complete'); Loop.stop(); return;
    }
    const start=Timer.getTime(timer); last=draw(); const ticks=Timer.getTime(timer)-start;
    if(local>=60) samples.push(ticks);
    for(let i=0;i<names.length;i++) Draw.rect(12+i*20,12,14,14,i===stage?orange:gray);
    Draw.rect(12,height-20,Math.max(1,Math.floor((mode.width-24)*(local+1)/180)),8,white);
    frame++;
}});
