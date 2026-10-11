import * as Model3D from "Model3D";
import * as Render3D from "Render3D";
import * as Camera3D from "Camera3D";
let checks=0;
function assert(ok,message) { checks++; if(!ok) throw new Error(message); }
function throws(fn,message) { let caught=false; try { fn(); } catch(e) { caught=true; } assert(caught,message); }
const geometry={positions:new Float32Array([-.1,-.1,-3,.1,-.1,-3,0,.1,-3]),texcoords:new Float32Array([0,1,1,1,.5,0])};
const pixels=new Uint32Array([0,0xff,0xff00,0xff0000,0xffffff,0]);
const t=Model3D.Texture.fromPixels({width:2,height:2,pixels:pixels.subarray(1,5)});
assert(t.width===2&&t.height===2,'dimensions'); pixels.fill(0);
throws(()=>Model3D.Texture.fromPixels({width:3,height:2,pixels:new Uint32Array(6)}),'power of two');
throws(()=>Model3D.Texture.fromPixels({width:2,height:2,pixels:new Uint32Array(3)}),'pixel count');
throws(()=>Model3D.Texture.fromPixels({width:512,height:1024,pixels:new Uint32Array(4)}),'limit');
throws(()=>Model3D.Texture.fromPixels({width:1.5,height:2,pixels:new Uint32Array(4)}),'integral dimensions');
throws(()=>Model3D.Texture.fromPixels({width:NaN,height:2,pixels:new Uint32Array(4)}),'NaN');
throws(()=>Model3D.Texture.fromPixels({width:2,height:2,pixels:new Float32Array(4)}),'pixel type');
throws(()=>Model3D.Texture.fromPixels({width:2,height:2,pixels:new Uint32Array(4),filter:2}),'filter');
throws(()=>new Model3D.Texture(),'forbidden constructor');
throws(()=>Model3D.Texture.load('missing'), 'load error');
throws(()=>Model3D.Texture.load('bad\0path'),'NUL path');
throws(()=>Model3D.Mesh.fromGeometry({positions:geometry.positions,material:{texture:t}}),'required UVs');
throws(()=>Model3D.Mesh.fromGeometry({...geometry,texcoords:new Float32Array(4),material:{texture:t}}),'UV count');
for(const bad of [NaN,Infinity,-16.5,16.5]) {
    const uv=new Float32Array(geometry.texcoords); uv[0]=bad;
    throws(()=>Model3D.Mesh.fromGeometry({...geometry,texcoords:uv,material:{texture:t}}),'UV domain');
}
// Tiled UVs within UV_LIMIT are valid; the texture's wrap mode applies.
assert(Model3D.UV_LIMIT===16,'UV_LIMIT');
{
    const uv=new Float32Array(geometry.texcoords); uv[0]=-3.5; uv[3]=8;
    Model3D.Mesh.fromGeometry({...geometry,texcoords:uv,material:{texture:t}}).dispose();
}
assert(t.wrap===Model3D.Texture.CLAMP,'default wrap');
{
    const r=Model3D.Texture.fromPixels({width:1,height:1,pixels:new Uint32Array([255]),wrap:Model3D.Texture.REPEAT});
    assert(r.wrap===3&&Model3D.Texture.REPEAT_U===1&&Model3D.Texture.REPEAT_V===2,'repeat wrap'); r.dispose();
}
throws(()=>Model3D.Texture.fromPixels({width:1,height:1,pixels:new Uint32Array([255]),wrap:4}),'invalid wrap');
{
    let message='';
    try { Model3D.load('models/missing.glb'); } catch(e) { message=String(e.message); }
    assert(message.includes('models/missing.glb'),'load error names the file');
}
const mesh=Model3D.Mesh.fromGeometry({...geometry,material:{texture:t}}), instance=mesh.createInstance();
// Explicit upload, TRS getters and bounds.
assert(t.upload()===t&&t.upload()===t,'upload is idempotent and chains');
{
    const b=mesh.getBounds(); assert(b.length===6&&Math.abs(b[0]+.1)<1e-6&&Math.abs(b[4]-.1)<1e-6&&b[5]===-3,'bounds');
    const out=new Float32Array(6); assert(mesh.getBounds(out)===out&&out[2]===-3,'bounds out');
    instance.setPosition(1,2,3).setScale(2,2,2);
    const p=instance.getPosition(),sc=instance.getScale(),r=instance.getRotation();
    assert(p[0]===1&&p[2]===3&&sc[1]===2&&r.length===4&&r[3]===1,'TRS getters');
    const q=new Float32Array(4); assert(instance.getRotation(q)===q&&q[3]===1,'rotation out');
    throws(()=>instance.getPosition(new Float32Array(2)),'short out');
    instance.setPosition(0,0,0).setScale(1,1,1);
}
const imported=Model3D.load('models/textured_cube.glb',{texture:t}); assert(imported.vertexCount===36,'GLB UVs'); imported.dispose();
const automatic=Model3D.load('models/textured_cube.glb'); assert(automatic.vertexCount===36,'external texture relative path'); automatic.dispose();
const loaded=Model3D.Texture.load('models/checker.png',Model3D.Texture.LINEAR);
assert(loaded.width===64&&loaded.height===64,'Texture.load dimensions'); loaded.dispose();
mesh.dispose(); t.dispose(); t.dispose(); throws(()=>t.width,'disposed dimensions');
throws(()=>Model3D.Mesh.fromGeometry({...geometry,material:{texture:t}}),'disposed material texture');
const camera=new Camera3D.Camera(),batch=new Render3D.Batch(); batch.add(instance); instance.dispose();
let stats=batch.draw(camera); assert(stats.geometryBytes===96&&stats.triangles===1,'retained texture');
// Render3D.group shares one pass, returns fn's value and closes on throw.
assert(Render3D.group(()=>{ batch.draw(camera); return 7; })===7,'group result');
throws(()=>Render3D.group(()=>{ throw new Error('inside'); }),'group rethrows');
assert(Render3D.group(()=>1)===1,'group closed after throw');
throws(()=>Render3D.group(()=>Render3D.group(()=>0)),'nested group');
throws(()=>Render3D.group(3),'group needs a function');
batch.dispose(); camera.dispose();
// Every descriptor getter runs before obtaining a native texture handle.
const disposed=Model3D.Texture.fromPixels({width:1,height:1,pixels:new Uint32Array([255])});
throws(()=>Model3D.Mesh.fromGeometry({...geometry,material:{get texture(){return disposed;},get baseColor(){disposed.dispose();return undefined;}}}),'getter disposal');
const one=Model3D.Texture.fromPixels({width:1,height:1,pixels:new Uint32Array([255]),filter:Model3D.Texture.LINEAR});
const uv=new Float32Array(geometry.texcoords);
throws(()=>Model3D.Mesh.fromGeometry({...geometry,texcoords:uv,material:{texture:one,get shading(){uv[0]=NaN;return 0;}}}),'getter mutates UV');
one.dispose();
let getterRan=false;
throws(()=>Model3D.Texture.fromPixels({width:1,height:1,get filter(){getterRan=true;throw new Error('getter');},pixels:new Uint32Array([0])}),'throwing getter');
assert(getterRan,'getter ran');
for(let cycle=0;cycle<32;cycle++) {
    const texture=Model3D.Texture.fromPixels({width:1,height:1,pixels:new Uint32Array([255])});
    const m=Model3D.Mesh.fromGeometry({...geometry,material:{texture,shading:cycle&1}}),i=m.createInstance();
    texture.dispose(); m.dispose(); i.dispose();
}
console.log('3D texture tests passed ('+checks+' checks)');
