import * as Model3D from "Model3D";
import * as Scene3D from "Scene3D";
import * as Camera3D from "Camera3D";
import * as GLTF3D from "GLTF3D";
import * as Animation3D from "Animation3D";
void Animation3D; // installs clip bindings used by GLTF3D.load
let checks=0;
function assert(ok,label) { checks++; if(!ok) throw new Error(label); }
function throws(fn,type,label) {
    let error; try { fn(); } catch(e) { error=e; }
    assert(error instanceof type,label);
}
const positions=new Float32Array([0,0,-3, 1,0,-3, 0,1,-3]);
const joints=new Uint16Array([0,1,0,0, 1,0,0,0, 0,1,0,0]);
const weights=new Float32Array([2,2,0,0, 1,0,0,0, 1,3,0,0]);
const deltas=new Float32Array([2,0,0, 2,0,0, 2,0,0, 0,4,0, 0,4,0, 0,4,0]);
const normals=new Float32Array([0,0,1, 0,0,1, 0,0,1]);
const geometry={positions,joints,weights,targetPositions:deltas};
assert(Model3D.MAX_JOINTS===256&&Model3D.MAX_TARGETS===8,"geometry limits exported");
const mesh=Model3D.Mesh.fromGeometry(geometry),node=new Scene3D.Node(mesh);
assert(mesh.vertexCount===3&&node.getWeights().join()==="0,0","two targets inferred");
const scene=new Scene3D.Scene(),root=scene.root;
root.add(node);root.dispose();
// The mesh owns copied streams even after the source changes or dies.
const bounds=mesh.getBounds(); deltas.fill(NaN);joints.fill(999);weights.fill(NaN);
mesh.dispose();std.gc();
node.setWeights([.5,.25]);scene.update();
const world=node.getWorldBounds();
assert(world.min[0]===1&&world.max[0]===2&&world.min[1]===1&&world.max[1]===2,"copied morph deltas affect bounds");
assert(bounds[0]===0&&bounds[3]===1,"base bounds exclude morph blend");
const camera=new Camera3D.Camera();
assert(scene.draw(camera,0).triangles===1,"procedural morph draws through scene");
camera.dispose();scene.dispose();node.dispose();
// A procedural mesh can replace the geometry of an existing glTF skin,
// retaining its joint palette. Extreme finite weights remain normalized.
{
    const loaded=GLTF3D.load("models/bend.glb");
    const body=loaded.nodes.find(n=>n.hasMesh);
    const m=Model3D.Mesh.fromGeometry({positions,normals,
        joints:new Uint16Array([0,1,0,0, 1,0,0,0, 0,1,0,0]),
        weights:new Float32Array([3e38,3e38,0,0, 1,0,0,0, 1,3,0,0])});
    body.setMesh(m);m.dispose();
    const s=new Scene3D.Scene(),r=s.root;r.add(loaded.root);r.dispose();s.update();
    const c=new Camera3D.Camera();
    assert(s.draw(c,0).triangles===1,"procedural skin uses retained glTF palette");
    c.dispose();s.dispose();loaded.root.dispose();
    for(const n of loaded.nodes) n.dispose();for(const name in loaded.clips) loaded.clips[name].dispose();
}
// Every stream honors its own subarray and source vertex count, before expansion.
function padded(Type,values) { const a=new Type(values.length+4);a.set(values,2);return a.subarray(2,-2); }
const validJoints=padded(Uint16Array,[0,1,0,0, 1,0,0,0, 0,1,0,0]);
const validWeights=padded(Float32Array,[3e38,3e38,0,0, 1,0,0,0, 1,3,0,0]);
const targetPositions=padded(Float32Array,[2,0,0,2,0,0,2,0,0]);
const targetNormals=padded(Float32Array,[0,.1,0,0,.1,0,0,.1,0]);
const indexed={positions,normals,joints:validJoints,weights:validWeights,targetPositions,targetNormals,
    indices:new Uint32Array([2,0,1,2,1,0])};
const expanded=Model3D.Mesh.fromGeometry(indexed),expandedNode=new Scene3D.Node(expanded);
assert(expanded.vertexCount===6&&expandedNode.getWeights().length===1,"indexed skin and morph streams expand");
expanded.dispose();expandedNode.dispose();
const base={positions};
throws(()=>Model3D.Mesh.fromGeometry({...base,joints:validJoints}),RangeError,"missing weights");
throws(()=>Model3D.Mesh.fromGeometry({...base,weights:validWeights}),RangeError,"missing joints");
throws(()=>Model3D.Mesh.fromGeometry({...base,joints:new Uint8Array(12),weights:validWeights}),TypeError,"joints need Uint16Array");
throws(()=>Model3D.Mesh.fromGeometry({...base,joints:validJoints,weights:new Float64Array(12)}),TypeError,"weights need Float32Array");
for(const length of [0,4,11,13,16]) {
    throws(()=>Model3D.Mesh.fromGeometry({...base,joints:new Uint16Array(length),weights:validWeights}),RangeError,"joint count "+length);
    throws(()=>Model3D.Mesh.fromGeometry({...base,joints:validJoints,weights:new Float32Array(length)}),RangeError,"weight count "+length);
}
for(const bad of [-1,NaN,Infinity]) {
    const w=new Float32Array(12).fill(1);w[0]=bad;
    throws(()=>Model3D.Mesh.fromGeometry({...base,joints:validJoints,weights:w}),RangeError,"invalid weight "+bad);
}
throws(()=>Model3D.Mesh.fromGeometry({...base,joints:validJoints,weights:new Float32Array(12)}),RangeError,"all-zero weights");
const high=new Uint16Array(validJoints);high[0]=256;
throws(()=>Model3D.Mesh.fromGeometry({...base,joints:high,weights:validWeights}),RangeError,"joint limit");
for(const length of [0,8,10,81])
    throws(()=>Model3D.Mesh.fromGeometry({...base,targetPositions:new Float32Array(length)}),RangeError,"target length "+length);
throws(()=>Model3D.Mesh.fromGeometry({...base,targetNormals}),RangeError,"normal targets need positions");
throws(()=>Model3D.Mesh.fromGeometry({...base,targetPositions,targetNormals}),RangeError,"normal targets need base normals");
throws(()=>Model3D.Mesh.fromGeometry({...base,normals,targetPositions,targetNormals:new Float32Array(18)}),RangeError,"matching target counts");
throws(()=>Model3D.Mesh.fromGeometry({...base,targetPositions:new Float64Array(9)}),TypeError,"target float type");
for(const name of ["targetPositions","targetNormals"]) {
    const bad=new Float32Array(name==="targetPositions"?targetPositions:targetNormals);bad[3]=NaN;
    throws(()=>Model3D.Mesh.fromGeometry({...indexed,[name]:bad}),RangeError,"nonfinite "+name);
}
// Late getters mutate earlier buffers before validation, never after pinning.
const early=new Float32Array(targetPositions);
throws(()=>Model3D.Mesh.fromGeometry({...base,targetPositions:early,get targetNormals(){early[0]=NaN;return undefined;}}),RangeError,"late getter changes morph buffer");
const marker=new Error("getter failed");let caught;
try { Model3D.Mesh.fromGeometry({...base,get weights(){throw marker;}}); } catch(e) { caught=e; }
assert(caught===marker,"getter exception preserved");
// Max target count, with skin and normal deltas, survives repeated GC/finalization.
for(let i=0;i<32;i++) {
    const m=Model3D.Mesh.fromGeometry({...indexed,targetPositions:new Float32Array(72),targetNormals:new Float32Array(72)});
    const n=new Scene3D.Node(m);assert(n.getWeights().length===8,"maximum targets");m.dispose();n.dispose();
    if(i%4===0) std.gc();
}
console.log("3D procedural geometry tests passed ("+checks+")");
