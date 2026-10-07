// Deterministic, dependency-free animated GLB fixture for gltf3d: a node
// hierarchy (base -> arm), a node given by a matrix (lamp), a mesh with two
// primitives, and two animations: "wave" (LINEAR rotation of the arm, STEP
// translation of the base) and "bob" (CUBICSPLINE scale of the base).
// Run `node tools/make_3d_animated_asset.js` from any working directory.
const fs = require('fs');
const path = require('path');
const p = [[-1,-1,-1],[1,-1,-1],[1,1,-1],[-1,1,-1],[-1,-1,1],[1,-1,1],[1,1,1],[-1,1,1]];
const faces = [
    [[0,2,1,0,3,2],[0,0,-1]], [[4,5,6,4,6,7],[0,0,1]],
    [[0,1,5,0,5,4],[0,-1,0]], [[3,7,6,3,6,2],[0,1,0]],
    [[0,4,7,0,7,3],[-1,0,0]], [[1,2,6,1,6,5],[1,0,0]]
];
const floats = [], views = [], accessors = [];
function add(values, type, count, extra = {}) {
    const offset = floats.length * 4;
    floats.push(...values);
    views.push({buffer: 0, byteOffset: offset, byteLength: values.length * 4});
    accessors.push({bufferView: views.length - 1, componentType: 5126, count, type, ...extra});
    return accessors.length - 1;
}
// Cube: positions and face normals, 36 vertices.
const positions = [], normals = [];
for (const [indices, normal] of faces) for (const i of indices) { positions.push(...p[i]); normals.push(...normal); }
const cubePos = add(positions, 'VEC3', 36, {min: [-1,-1,-1], max: [1,1,1]});
const cubeNormal = add(normals, 'VEC3', 36);
// A second primitive: one triangle above the cube.
const triPos = add([-1,1.5,0, 1,1.5,0, 0,2.5,0], 'VEC3', 3, {min: [-1,1.5,0], max: [1,2.5,0]});
const s = Math.SQRT1_2;
const waveTimes = add([0, 1, 2], 'SCALAR', 3, {min: [0], max: [2]});
const waveRot = add([0,0,0,1, 0,0,s,s, 0,0,0,1], 'VEC4', 3);
const stepTimes = add([0, 1], 'SCALAR', 2, {min: [0], max: [1]});
const stepPos = add([0,0,0, 1,0,0], 'VEC3', 2);
const bobTimes = add([0, 1], 'SCALAR', 2, {min: [0], max: [1]});
// CUBICSPLINE: in-tangent, value, out-tangent per key.
const bobScale = add([0,0,0, 1,1,1, 0,0,0,  0,0,0, 2,2,2, 0,0,0], 'VEC3', 6);
const buffer = Buffer.alloc(floats.length * 4);
floats.forEach((v, i) => buffer.writeFloatLE(v, i * 4));
const json = {
    asset: {version: '2.0', generator: 'AthenaEnv animated fixture'},
    buffers: [{byteLength: buffer.length}],
    bufferViews: views, accessors,
    meshes: [
        {name: 'cube', primitives: [{attributes: {POSITION: cubePos, NORMAL: cubeNormal}, mode: 4}]},
        {name: 'cube+fin', primitives: [{attributes: {POSITION: cubePos, NORMAL: cubeNormal}, mode: 4},
            {attributes: {POSITION: triPos}, mode: 4}]}
    ],
    nodes: [
        {name: 'base', mesh: 0, children: [1]},
        {name: 'arm', mesh: 1, translation: [0, 1.5, 0], scale: [.5, .5, .5]},
        // 90 degrees about Y, then moved to (3, 0, 0).
        {name: 'lamp', matrix: [0,0,-1,0, 0,1,0,0, 1,0,0,0, 3,0,0,1]}
    ],
    scenes: [{nodes: [0, 2]}], scene: 0,
    animations: [
        {name: 'wave', samplers: [
            {input: waveTimes, output: waveRot, interpolation: 'LINEAR'},
            {input: stepTimes, output: stepPos, interpolation: 'STEP'}],
         channels: [{sampler: 0, target: {node: 1, path: 'rotation'}},
            {sampler: 1, target: {node: 0, path: 'translation'}}]},
        {name: 'bob', samplers: [{input: bobTimes, output: bobScale, interpolation: 'CUBICSPLINE'}],
         channels: [{sampler: 0, target: {node: 0, path: 'scale'}}]}
    ]
};
const text = Buffer.from(JSON.stringify(json));
const jsonChunk = Buffer.concat([text, Buffer.alloc((4 - text.length % 4) % 4, 0x20)]);
const binChunk = Buffer.concat([buffer, Buffer.alloc((4 - buffer.length % 4) % 4)]);
const header = Buffer.alloc(12), jh = Buffer.alloc(8), bh = Buffer.alloc(8);
header.writeUInt32LE(0x46546c67, 0); header.writeUInt32LE(2, 4);
header.writeUInt32LE(12 + 8 + jsonChunk.length + 8 + binChunk.length, 8);
jh.writeUInt32LE(jsonChunk.length, 0); jh.writeUInt32LE(0x4e4f534a, 4);
bh.writeUInt32LE(binChunk.length, 0); bh.writeUInt32LE(0x004e4942, 4);
const out = path.join(__dirname, '..', 'bin', 'models', 'arm.glb');
fs.writeFileSync(out, Buffer.concat([header, jh, jsonChunk, bh, binChunk]));
console.log('wrote ' + out);
