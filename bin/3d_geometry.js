// D6 visual regression: copied/indexed procedural morph and skin streams.
// Each stage lasts 180 frames and logs 3D_GEOMETRY {json} after warmup.
// Morph: left VU1 (position deltas), right CPU (also normal deltas), same shape.
// Skin: left untextured VU1, right textured VU1, sharing geometry and motion;
// joints/palette come from bend.glb; the body's mesh is replaced from JS.
const mode=Screen.getMode();
const height=mode.height*(mode.interlace===Screen.INTERLACED&&mode.field===Screen.FRAME?2:1);
mode.height=height;mode.zbuffering=true;mode.psmz=Screen.Z16S;Screen.setMode(mode);
const camera=new Camera3D.Camera({aspect:mode.width/height,near:.5,far:50}).lookAt(0,0,-1).setPosition(0,0,0);
const lights=new Lights.Set().setAmbient(1,1,1);
const positions=new Float32Array([-.5,0,0,.5,0,0,.5,2,0,-.5,2,0]);
const normals=new Float32Array([0,0,1,0,0,1,0,0,1,0,0,1]);
const indices=new Uint32Array([0,1,2,0,2,3]);
const orange=Color.new(255,128,0),white=Color.new(255,255,255),gray=Color.new(64,64,64);
let scene=null,draw=null,resources=[],frame=0,stage=-1;
const stages=['morph','skin','skin-unlit-mask','skin-perspective','skin-near'];
function keep(r) {resources.push(r);return r;}
function release() {for(const r of resources) r.dispose();resources=[];scene=null;}
function create() {
    scene=keep(new Scene3D.Scene());const root=scene.root;
    if(stage===0) {
        const deltas=new Float32Array([0,0,0,0,0,0,.8,.5,0,.8,.5,0]);
        const nodes=[];
        for(let i=0;i<2;i++) {
            const mesh=Model3D.Mesh.fromGeometry({positions,normals,indices,targetPositions:deltas,
                targetNormals:i?new Float32Array(12):undefined,
                material:{shading:Model3D.DIFFUSE,baseColor:new Float32Array([.2,.8,1,1])}});
            const node=keep(new Scene3D.Node(mesh).setPosition(i?1.2:-1.8,-1,-5));
            mesh.dispose();root.add(node);nodes.push(node);
        }
        // Mutation after creation must not affect either copy.
        deltas.fill(NaN);
        draw=()=>{
            const weight=.5+.4*Math.sin(frame*Math.PI/90);
            for(const n of nodes) n.setWeights([weight]);scene.update();
            const stats=scene.draw(camera,Render3D.CULL_NONE,lights);
            if(stats.triangles!==4||stats.vuMorphObjects!==1) throw new Error('procedural morph paths');
            return stats;
        };
    } else {
        const clips=[];
        const joints=new Uint16Array([0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0]);
        const weights=new Float32Array([1,0,0,0,1,0,0,0,3e38,3e38,0,0,3e38,3e38,0,0]);
        const texture=keep(Model3D.Texture.load(stage===2?'models/alpha_mask.png':'models/checker.png'));texture.upload();
        for(let i=0;i<2;i++) {
            const loaded=GLTF3D.load('models/bend.glb');
            keep(loaded.root);for(const n of loaded.nodes) keep(n);for(const name in loaded.clips) keep(loaded.clips[name]);
            const mesh=Model3D.Mesh.fromGeometry({positions,normals,indices,joints,weights,
                texcoords:i?new Float32Array([0,1,1,1,1,0,0,0]):undefined,
                material:{shading:stage===2?Model3D.UNLIT:Model3D.DIFFUSE,texture:i?texture:undefined,
                    alphaCutoff:stage===2&&i?.5:undefined,
                    baseColor:new Float32Array([1,1,1,1])}});
            loaded.nodes.find(n=>n.hasMesh).setMesh(mesh);mesh.dispose();
            loaded.root.setPosition(i?1.5:-1.5,-1,-5);root.add(loaded.root);
            if(stage===3) loaded.root.setRotationEuler(0,.8,0);
            if(stage===4) loaded.root.setPosition(i?.4:-.4,-.5,-.55).setRotationEuler(0,.8,0);
            clips.push(keep(new Animation3D.Player(loaded.clips.bend,loaded.nodes)));
        }
        joints.fill(999);weights.fill(NaN);
        draw=()=>{
            for(const p of clips) p.time=.5+.5*Math.sin(frame*Math.PI/90);
            scene.update();const stats=scene.draw(camera,Render3D.CULL_NONE,lights);
            if(stats.queuedObjects!==2) throw new Error('procedural skin paths');
            if(stage!==4&&(stats.triangles!==4||stats.geometryBytes!==416)) throw new Error('textured skin must use VU1 streams');
            if(stage===4&&!stats.nearClipObjects) throw new Error('skin near-plane fallback');
            return stats;
        };
    }
    root.dispose();
}
Loop.run({draw(){
    const next=Math.floor(frame/180),local=frame%180;
    if(next!==stage) {
        release();stage=next;
        if(stage>=stages.length) {camera.dispose();lights.dispose();console.log('3D geometry complete');Loop.stop();return;}
        create();console.log('3D geometry stage '+stage+': '+stages[stage]);
    }
    const stats=draw();
    if(local===90) console.log('3D_GEOMETRY '+JSON.stringify({stage:stages[stage],stats,memory:System.getMemoryStats().used}));
    for(let i=0;i<stages.length;i++) Draw.rect(12+i*20,12,14,14,i===stage?orange:gray);
    Draw.rect(12,height-20,Math.max(1,Math.floor((mode.width-24)*(local+1)/180)),8,white);frame++;
}});
