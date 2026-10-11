// D5: stable wrappers, explicit disposal, cyclic GC and glTF identity on EE.
// Four stages of 180 frames; green markers mean assertions passed this frame.
import * as std from 'std';
const mode=Screen.getMode();
const height=mode.height*(mode.interlace===Screen.INTERLACED&&mode.field===Screen.FRAME?2:1);
mode.height=height;mode.zbuffering=true;mode.psmz=Screen.Z16S;Screen.setMode(mode);
const camera=new Camera3D.Camera({aspect:mode.width/height,near:.5,far:50}).lookAt(0,0,-1).setPosition(0,0,0);
const lights=new Lights.Set().setAmbient(1,1,1);
const names=['constructor','dispose','cyclic-gc','gltf'];
const green=Color.new(64,255,96),gray=Color.new(64,64,64),white=Color.new(255,255,255);
let resources=[],scene=null,root=null,check=null,stage=-1,frame=0,checks=0;
function assert(ok,label) {checks++;if(!ok) throw new Error(label);}
function keep(r) {resources.push(r);return r;}
function release() {for(const r of resources) r.dispose();resources=[];}
function create() {
    scene=keep(new Scene3D.Scene());root=keep(scene.root);
    if(stage<3) {
        const mesh=Model3D.load('models/textured_cube.glb',{shading:Model3D.DIFFUSE});
        let node=new Scene3D.Node(mesh).setPosition(0,0,-5);mesh.dispose();root.add(node);
        node.marker='cube';
        if(stage===0) {
            keep(node);const metadata=new Map([[node,'cube']]);
            check=()=>{
                assert(scene.root===root&&node.getParent()===root,'root/parent identity');
                assert(root.getChild(0)===node&&metadata.get(root.getChild(0))==='cube','child/Map identity');
            };
        } else if(stage===1) {
            const old=node,alias=root.getChild(0);alias.dispose();alias.dispose();
            let threw=false;try {old.hasMesh;} catch(e) {threw=e instanceof TypeError;}
            assert(threw,'disposed aliases');node=keep(root.getChild(0));
            assert(node!==old&&node.marker===undefined&&node.hasMesh,'replacement wrapper');
            const oldRoot=root;root.dispose();root=keep(scene.root);oldRoot.dispose();
            check=()=>assert(scene.root===root&&root.getChild(0)===node&&node.getParent()===root,'replacement identity');
        } else {
            node.self=node;node=null;std.gc();
            const fresh=keep(root.getChild(0));
            assert(fresh.self===undefined&&fresh.marker===undefined&&fresh.hasMesh,'weak cache cyclic GC');
            check=()=>assert(root.getChild(0)===fresh&&fresh.getParent()===root,'GC replacement identity');
        }
    } else {
        const loaded=GLTF3D.load('models/arm.glb');keep(loaded.root);
        for(let i=0;i<loaded.nodes.length;i++) {keep(loaded.nodes[i]);loaded.nodes[i].index=i;}
        for(const name in loaded.clips) keep(loaded.clips[name]);
        loaded.root.setPosition(0,0,-7);root.add(loaded.root);
        check=()=>{
            assert(root.getChild(0)===loaded.root,'glTF root identity');
            for(const node of loaded.nodes) {
                const parent=node.getParent();
                let found=false;
                for(let i=0;i<parent.childCount;i++) if(parent.getChild(i)===node) found=true;
                assert(found&&loaded.nodes[node.index]===node,'glTF nodes identity');
            }
        };
    }
    scene.update();
}
Loop.run({draw(){
    const next=Math.floor(frame/180),local=frame%180;
    if(next!==stage) {
        release();stage=next;checks=0;
        if(stage>=names.length) {camera.dispose();lights.dispose();console.log('3D node identity complete');Loop.stop();return;}
        create();console.log('3D node identity stage '+stage+': '+names[stage]);
    }
    check();const stats=scene.draw(camera,Render3D.CULL_NONE,lights);
    if(local===90) console.log('3D_NODE_IDENTITY '+JSON.stringify({stage:names[stage],checks,stats,memory:System.getMemoryStats().used}));
    for(let i=0;i<names.length;i++) Draw.rect(12+i*20,12,14,14,i===stage?green:gray);
    Draw.rect(12,height-20,Math.max(1,Math.floor((mode.width-24)*(local+1)/180)),8,white);frame++;
}});
