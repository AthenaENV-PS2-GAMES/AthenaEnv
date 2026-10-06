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
for(const bad of [NaN,Infinity,-.1,1.1]) {
    const uv=new Float32Array(geometry.texcoords); uv[0]=bad;
    throws(()=>Model3D.Mesh.fromGeometry({...geometry,texcoords:uv,material:{texture:t}}),'UV domain');
}
const mesh=Model3D.Mesh.fromGeometry({...geometry,material:{texture:t}}), instance=mesh.createInstance();
const imported=Model3D.load('models/textured_cube.glb',{texture:t}); assert(imported.vertexCount===36,'GLB UVs'); imported.dispose();
const automatic=Model3D.load('models/textured_cube.glb'); assert(automatic.vertexCount===36,'external texture relative path'); automatic.dispose();
const loaded=Model3D.Texture.load('models/checker.png',Model3D.Texture.LINEAR);
assert(loaded.width===64&&loaded.height===64,'Texture.load dimensions'); loaded.dispose();
mesh.dispose(); t.dispose(); t.dispose(); throws(()=>t.width,'disposed dimensions');
throws(()=>Model3D.Mesh.fromGeometry({...geometry,material:{texture:t}}),'disposed material texture');
const camera=new Camera3D.Camera(),batch=new Render3D.Batch(); batch.add(instance); instance.dispose();
let stats=batch.draw(camera); assert(stats.geometryBytes===96&&stats.triangles===1,'retained texture');
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
