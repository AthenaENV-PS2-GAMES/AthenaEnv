// Deterministic, dependency-free GLB fixture: face normals, twelve triangles.
// Run `node tools/make_3d_lighting_asset.js` from any working directory.
const fs = require('fs');
const path = require('path');
const p = [
    [-1,-1,-1],[1,-1,-1],[1,1,-1],[-1,1,-1],
    [-1,-1,1],[1,-1,1],[1,1,1],[-1,1,1]
];
const faces = [
    [[0,2,1,0,3,2],[0,0,-1]], [[4,5,6,4,6,7],[0,0,1]],
    [[0,1,5,0,5,4],[0,-1,0]], [[3,7,6,3,6,2],[0,1,0]],
    [[0,4,7,0,7,3],[-1,0,0]], [[1,2,6,1,6,5],[1,0,0]]
];
const positions = [], normals = [];
for (const [indices, normal] of faces) for (const i of indices) {
    positions.push(...p[i]); normals.push(...normal);
}
const buffer = Buffer.alloc((positions.length + normals.length) * 4);
[...positions,...normals].forEach((v,i) => buffer.writeFloatLE(v,i*4));
const json = {
    asset: {version:'2.0',generator:'AthenaEnv lighting fixture'},
    buffers: [{byteLength:buffer.length}],
    bufferViews: [{buffer:0,byteOffset:0,byteLength:432,target:34962},
        {buffer:0,byteOffset:432,byteLength:432,target:34962}],
    accessors: [{bufferView:0,componentType:5126,count:36,type:'VEC3',min:[-1,-1,-1],max:[1,1,1]},
        {bufferView:1,componentType:5126,count:36,type:'VEC3'}],
    meshes: [{primitives:[{attributes:{POSITION:0,NORMAL:1},mode:4}]}],
    nodes:[{mesh:0}],scenes:[{nodes:[0]}],scene:0
};
const text = Buffer.from(JSON.stringify(json));
const padded = Buffer.alloc((text.length + 3) & ~3,0x20); text.copy(padded);
const glb = Buffer.alloc(12+8+padded.length+8+buffer.length);
glb.writeUInt32LE(0x46546c67,0); glb.writeUInt32LE(2,4); glb.writeUInt32LE(glb.length,8);
glb.writeUInt32LE(padded.length,12); glb.writeUInt32LE(0x4e4f534a,16); padded.copy(glb,20);
const offset = 20+padded.length;
glb.writeUInt32LE(buffer.length,offset); glb.writeUInt32LE(0x004e4942,offset+4); buffer.copy(glb,offset+8);
const destination = path.join(__dirname,'../bin/models/lit_cube.glb');
fs.mkdirSync(path.dirname(destination),{recursive:true}); fs.writeFileSync(destination,glb);
console.log(`Lighting GLB: ${glb.length} bytes, 36 vertices`);
