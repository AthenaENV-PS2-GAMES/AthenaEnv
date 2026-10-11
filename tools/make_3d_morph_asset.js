// Deterministic, dependency-free morph target GLB fixture: a unit cube (36
// vertices, no normals: diffuse normals are generated per face) with two
// targets. "stretch" raises the top face by 1; "pinch" pulls the top face to
// the Y axis and is stored as a sparse accessor (only the top vertices),
// the usual encoding of exported targets. Default weights [0, 0]; the "morph"
// animation goes [0,0] -> [1,0] -> [0,1] -> [0,0] over 3 s (LINEAR).
// Run `node tools/make_3d_morph_asset.js` from any working directory.
const fs = require('fs');
const path = require('path');
// `--grid=N --out=name.glb` splits every face into N x N quads for profiling
// (N = 8: 2304 vertices); the targets then scale with the height, so the
// whole box bends instead of only its top face.
const args = Object.fromEntries(process.argv.slice(2).map(a => a.replace(/^--/, '').split('=')));
const grid = +(args.grid || 1);
const p = [[-1,-1,-1],[1,-1,-1],[1,1,-1],[-1,1,-1],[-1,-1,1],[1,-1,1],[1,1,1],[-1,1,1]];
const faces = [[0,2,1,0,3,2],[4,5,6,4,6,7],[0,1,5,0,5,4],[3,7,6,3,6,2],[0,4,7,0,7,3],[1,2,6,1,6,5]];
const positions = [];
const lerp = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
if (grid === 1) for (const face of faces) for (const i of face) positions.push(...p[i]);
else for (const face of faces) {
    // The quad in winding order: both triangles start at the same corner and
    // share the diagonal, listed second or third depending on the face.
    const [a, x, y, , z, w] = face;
    const quad = (y === z ? [a, x, y, w] : [a, z, x, y]).map(i => p[i]);
    const at = (u, v) => lerp(lerp(quad[0], quad[1], u), lerp(quad[3], quad[2], u), v);
    for (let i = 0; i < grid; i++) for (let j = 0; j < grid; j++) {
        const u0 = i / grid, u1 = (i + 1) / grid, v0 = j / grid, v1 = (j + 1) / grid;
        positions.push(...at(u0, v0), ...at(u1, v0), ...at(u1, v1), ...at(u0, v0), ...at(u1, v1), ...at(u0, v1));
    }
}
const count = positions.length / 3;
const stretch = [], top = [], pinch = [];
for (let v = 0; v < count; v++) {
    const [x, y, z] = positions.slice(v * 3, v * 3 + 3);
    const t = grid === 1 ? (y > 0 ? 1 : 0) : (y + 1) / 2;
    stretch.push(0, t, 0);
    if (t > 0) { top.push(v); pinch.push(-x * t, 0, -z * t); }
}
const parts = [], views = [], accessors = [];
let offset = 0;
function view(buffer) {
    parts.push(buffer); views.push({buffer: 0, byteOffset: offset, byteLength: buffer.length});
    offset += buffer.length; return views.length - 1;
}
function add(buffer, type, n, componentType, extra = {}) {
    accessors.push({bufferView: view(buffer), componentType, count: n, type, ...extra});
    return accessors.length - 1;
}
const f32 = values => { const b = Buffer.alloc(values.length * 4); values.forEach((v, i) => b.writeFloatLE(v, i * 4)); return b; };
const u16 = values => { const b = Buffer.alloc(values.length * 2); values.forEach((v, i) => b.writeUInt16LE(v, i * 2)); return b; };
const pad4 = b => Buffer.concat([b, Buffer.alloc((4 - b.length % 4) % 4)]);
const pos = add(f32(positions), 'VEC3', count, 5126, {min: [-1,-1,-1], max: [1,1,1]});
const stretchAcc = add(f32(stretch), 'VEC3', count, 5126, {min: [0,0,0], max: [0,1,0]});
// Sparse: no base bufferView (all zeros), only the top vertices stored.
const sparseIndices = view(pad4(u16(top))), sparseValues = view(f32(pinch));
accessors.push({componentType: 5126, count, type: 'VEC3', min: [-1,0,-1], max: [1,0,1],
    sparse: {count: top.length, indices: {bufferView: sparseIndices, componentType: 5123},
        values: {bufferView: sparseValues}}});
const pinchAcc = accessors.length - 1;
const times = add(f32([0, 1, 2, 3]), 'SCALAR', 4, 5126, {min: [0], max: [3]});
const weights = add(f32([0,0, 1,0, 0,1, 0,0]), 'SCALAR', 8, 5126);
const buffer = Buffer.concat(parts);
const json = {
    asset: {version: '2.0', generator: 'AthenaEnv morph fixture'},
    buffers: [{byteLength: buffer.length}], bufferViews: views, accessors,
    meshes: [{name: 'box', weights: [0, 0], primitives: [{attributes: {POSITION: pos}, mode: 4,
        targets: [{POSITION: stretchAcc}, {POSITION: pinchAcc}]}]}],
    nodes: [{name: 'box', mesh: 0}],
    scenes: [{nodes: [0]}], scene: 0,
    animations: [{name: 'morph', samplers: [{input: times, output: weights, interpolation: 'LINEAR'}],
        channels: [{sampler: 0, target: {node: 0, path: 'weights'}}]}]
};
const text = Buffer.from(JSON.stringify(json));
const jsonChunk = Buffer.concat([text, Buffer.alloc((4 - text.length % 4) % 4, 0x20)]);
const binChunk = pad4(buffer);
const header = Buffer.alloc(12), jh = Buffer.alloc(8), bh = Buffer.alloc(8);
header.writeUInt32LE(0x46546c67, 0); header.writeUInt32LE(2, 4);
header.writeUInt32LE(12 + 8 + jsonChunk.length + 8 + binChunk.length, 8);
jh.writeUInt32LE(jsonChunk.length, 0); jh.writeUInt32LE(0x4e4f534a, 4);
bh.writeUInt32LE(binChunk.length, 0); bh.writeUInt32LE(0x004e4942, 4);
const out = path.join(__dirname, '..', 'bin', 'models', args.out || 'morph.glb');
fs.writeFileSync(out, Buffer.concat([header, jh, jsonChunk, bh, binChunk]));
console.log('wrote ' + out + ' (' + count + ' vertices)');
