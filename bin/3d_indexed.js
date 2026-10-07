// Indexed VU1 regression: each stage compares identical indexed (right)
// and explicit triangle-list (left) meshes. Inputs are mutated after copying.
const mode=Screen.getMode();mode.height*=mode.interlace===Screen.INTERLACED&&mode.field===Screen.FRAME?2:1;
mode.zbuffering=true;mode.psmz=Screen.Z16S;Screen.setMode(mode);
const camera=new Camera3D.Camera({aspect:mode.width/mode.height,near:.5,far:50}).lookAt(0,0,-1).setPosition(0,0,0);
const lights=new Lights.Set().setAmbient(.3,.3,.3).setDirectional(0,-.4,.6,1,.7,.7,.7);
const texture=Model3D.Texture.load('models/alpha_mask.png');texture.upload();
const names=['static','diffuse','texture-mask','morph-4','skin','skin-texture','skin-morph','near','guard','cull','point-fog'];
const side=9,count=side*side,corners=(side-1)*(side-1)*6;
let frame=0,stage=-1,resources=[],draw=null;
function keep(x){resources.push(x);return x;}
function release(){for(const x of resources)x.dispose();resources=[];}
function geometry(indexed,z,skin,morph){
 const p=new Float32Array(count*3),n=new Float32Array(count*3),c=new Float32Array(count*4),uv=new Float32Array(count*2),j=new Uint16Array(count*4),w=new Float32Array(count*4);
 const targets=new Float32Array(count*3*4),indices=new Uint32Array(corners);let cursor=0;
 for(let y=0;y<side;y++)for(let x=0;x<side;x++){
  const i=y*side+x;p[i*3]=(x/(side-1)-.5)*1.6;p[i*3+1]=(y/(side-1)-.5)*1.6;p[i*3+2]=z;if(stage===7){p[i*3]*=.08;p[i*3+1]*=.08;}n[i*3+2]=1;
  c[i*4]=.3+.7*x/(side-1);c[i*4+1]=.3+.7*y/(side-1);c[i*4+2]=1;c[i*4+3]=1;
  uv[i*2]=x/(side-1);uv[i*2+1]=1-y/(side-1);j[i*4]=y>(side-1)/2?1:0;w[i*4]=1;
  for(let t=0;t<4;t++)targets[(t*count+i)*3+2]=.1*Math.sin(x*.6+t)*Math.cos(y*.5+t);
 }
 for(let y=0;y<side-1;y++)for(let x=0;x<side-1;x++){const a=y*side+x,b=a+1,d=a+side+1,e=a+side;indices.set([a,b,d,a,d,e],cursor);cursor+=6;}
 const material={shading:stage===0?Model3D.UNLIT:Model3D.DIFFUSE,
  texture:stage===2||stage===5?texture:undefined,alphaCutoff:stage===2||stage===5?.5:undefined};
 function expand(a,stride,blocks=1){const out=new a.constructor(corners*stride*blocks);for(let t=0;t<blocks;t++)for(let i=0;i<corners;i++)for(let k=0;k<stride;k++)out[(t*corners+i)*stride+k]=a[(t*count+indices[i])*stride+k];return out;}
 const g={positions:indexed?p:expand(p,3),normals:indexed?n:expand(n,3),colors:indexed?c:expand(c,4),texcoords:indexed?uv:expand(uv,2),
  indices:indexed?indices:undefined,joints:skin?(indexed?j:expand(j,4)):undefined,weights:skin?(indexed?w:expand(w,4)):undefined,
  targetPositions:morph?(indexed?targets:expand(targets,3,4)):undefined,material};
 const mesh=keep(Model3D.Mesh.fromGeometry(g));p.fill(NaN);indices.fill(65535);return mesh;
}
function create(){
 if(stage===10)lights.setAmbient(.1,.1,.1).setPoint(0,-1,0,-3,.3,.3,.4,5).setPoint(1,1,0,-3,.3,.3,.4,5).setFog(3,9,.2,.1,.1);
 const scene=keep(new Scene3D.Scene()),root=scene.root;let animated=[];
 const skin=stage>=4&&stage<=6,morph=stage===3||stage===6||stage===7;
 for(let i=0;i<2;i++){
  const mesh=geometry(!!i,skin||stage===9?0:-6,skin,morph);let node;
  if(skin){
   const loaded=GLTF3D.load('models/bend.glb');keep(loaded.root);for(const n of loaded.nodes)keep(n);for(const k in loaded.clips)keep(loaded.clips[k]);
   node=loaded.nodes.find(n=>n.hasMesh);node.setMesh(mesh);root.add(loaded.root);loaded.root.setPosition(i?1:-1,0,-6);
   animated.push(keep(new Animation3D.Player(loaded.clips.bend,loaded.nodes)));
  }else{node=keep(new Scene3D.Node(mesh));root.add(node);node.setPosition((i?1:-1)*(stage===7?.14:1),0,stage===7?5.5:stage===9?-6:0);}
  if(morph)node.setWeights(new Float32Array([.2,.4,-.3,.1]));
  if(stage===8)node.setPosition(i?4.8:-4.8,0,0);
  if(stage===9)node.setRotationEuler(0,i?0:Math.PI,0);
 }
 root.dispose();
 draw=()=>{for(const p of animated)p.time=.4+.2*Math.sin(frame*.03);scene.update();return scene.draw(camera,stage===9?Render3D.CULL_BACK:Render3D.CULL_NONE,lights);};
}
Loop.run({draw(){
 const next=Math.floor(frame/180),local=frame%180;
 if(next!==stage){release();stage=next;if(stage===names.length){texture.dispose();lights.dispose();camera.dispose();console.log('3D indexed complete');Loop.stop();return;}create();console.log('3D indexed stage '+stage+': '+names[stage]);}
 const stats=draw();if(local===90)console.log('3D_INDEXED '+JSON.stringify({stage:names[stage],stats,memory:System.getMemoryStats().used}));
 for(let i=0;i<names.length;i++)Draw.rect(12+i*20,12,14,14,Color.new(i===stage?255:64,128,0));frame++;
}});
