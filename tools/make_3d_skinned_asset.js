// Deterministic, dependency-free skinned GLB fixture: a square column from
// y = 0 to 2 made of three rings, skinned to two joints ("root" at the origin
// and "tip" at y = 1). The bottom ring follows root, the middle ring both
// halves, the top ring tip; "bend" turns tip 90 degrees about Z in 1 s.
// Run `node tools/make_3d_skinned_asset.js` from any working directory.
// `--rings=N --sides=M --out=name.glb` makes a finer column for profiling
// (weights then blend linearly between y = 0.5 and 1.5).
const fs = require('fs');
const path = require('path');
const args = Object.fromEntries(process.argv.slice(2).map(a => a.replace(/^--/, '').split('=')));
const ringCount = +(args.rings || 3), sides = +(args.sides || 4), fine = ringCount !== 3 || sides !== 4;
const h = .25, rings = Array.from({length: ringCount}, (_, r) => 2 * r / (ringCount - 1));
// x, z around the column, in the same turning direction as the 4-sided
// default (the square of the tests), so every side keeps the same winding.
const corners = sides === 4 ? [[-h,-h],[h,-h],[h,h],[-h,h]] :
    Array.from({length: sides}, (_, c) => [Math.cos(Math.PI * 2 * c / sides) * h * Math.SQRT2,
        Math.sin(Math.PI * 2 * c / sides) * h * Math.SQRT2]);
const ringPos = [], ringWeight = [];
rings.forEach((y, r) => corners.forEach(([x, z]) => {
    ringPos.push([x, y, z]);
    const t = fine ? Math.min(1, Math.max(0, y - .5)) : r === 0 ? 0 : r === 1 ? .5 : 1;
    ringWeight.push([1 - t, t]);
}));
const positions = [], normals = [], joints = [], weights = [];
function vertex(i, n) {
    positions.push(...ringPos[i]); normals.push(...n);
    joints.push(0, 1, 0, 0); weights.push(ringWeight[i][0], ringWeight[i][1], 0, 0);
}
// Sides between consecutive rings, counter-clockwise seen from outside.
const sideNormals = sides === 4 ? [[0,0,-1],[1,0,0],[0,0,1],[-1,0,0]] : corners.map((_, c) => {
    const a = Math.PI * 2 * (c + .5) / sides; return [Math.cos(a), 0, Math.sin(a)];
});
for (let r = 0; r < ringCount - 1; r++) for (let c = 0; c < sides; c++) {
    const a = r * sides + c, b = r * sides + (c + 1) % sides, d = a + sides, e = b + sides, n = sideNormals[c];
    vertex(a, n); vertex(d, n); vertex(b, n);
    vertex(b, n); vertex(d, n); vertex(e, n);
}
// Top cap.
const top = (ringCount - 1) * sides;
for (let c = 1; c < sides - 1; c++) { vertex(top, [0,1,0]); vertex(top + c + 1, [0,1,0]); vertex(top + c, [0,1,0]); }
const count = positions.length / 3;
const parts = [], views = [], accessors = [];
let offset = 0;
function add(buffer, type, n, componentType, extra = {}) {
    parts.push(buffer); views.push({buffer: 0, byteOffset: offset, byteLength: buffer.length});
    accessors.push({bufferView: views.length - 1, componentType, count: n, type, ...extra});
    offset += buffer.length; return accessors.length - 1;
}
const f32 = values => { const b = Buffer.alloc(values.length * 4); values.forEach((v, i) => b.writeFloatLE(v, i * 4)); return b; };
const u16 = values => { const b = Buffer.alloc(values.length * 2); values.forEach((v, i) => b.writeUInt16LE(v, i * 2)); return b; };
const pos = add(f32(positions), 'VEC3', count, 5126, {min: [-h, 0, -h], max: [h, 2, h]});
const nrm = add(f32(normals), 'VEC3', count, 5126);
const jnt = add(u16(joints), 'VEC4', count, 5123);
const wgt = add(f32(weights), 'VEC4', count, 5126);
// Inverse bind matrices: root at the origin, tip at y = 1.
const ibm = add(f32([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,  1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-1,0,1]), 'MAT4', 2, 5126);
const s = Math.SQRT1_2;
const times = add(f32([0, 1]), 'SCALAR', 2, 5126, {min: [0], max: [1]});
const turn = add(f32([0,0,0,1, 0,0,s,s]), 'VEC4', 2, 5126);
const buffer = Buffer.concat(parts);
const json = {
    asset: {version: '2.0', generator: 'AthenaEnv skinned fixture'},
    buffers: [{byteLength: buffer.length}], bufferViews: views, accessors,
    meshes: [{name: 'column', primitives: [{attributes: {POSITION: pos, NORMAL: nrm, JOINTS_0: jnt, WEIGHTS_0: wgt}, mode: 4}]}],
    skins: [{joints: [1, 2], inverseBindMatrices: ibm}],
    nodes: [
        {name: 'body', mesh: 0, skin: 0},
        {name: 'root', children: [2]},
        {name: 'tip', translation: [0, 1, 0]}
    ],
    scenes: [{nodes: [0, 1]}], scene: 0,
    animations: [{name: 'bend', samplers: [{input: times, output: turn, interpolation: 'LINEAR'}],
        channels: [{sampler: 0, target: {node: 2, path: 'rotation'}}]}]
};
const text = Buffer.from(JSON.stringify(json));
const jsonChunk = Buffer.concat([text, Buffer.alloc((4 - text.length % 4) % 4, 0x20)]);
const binChunk = Buffer.concat([buffer, Buffer.alloc((4 - buffer.length % 4) % 4)]);
const header = Buffer.alloc(12), jh = Buffer.alloc(8), bh = Buffer.alloc(8);
header.writeUInt32LE(0x46546c67, 0); header.writeUInt32LE(2, 4);
header.writeUInt32LE(12 + 8 + jsonChunk.length + 8 + binChunk.length, 8);
jh.writeUInt32LE(jsonChunk.length, 0); jh.writeUInt32LE(0x4e4f534a, 4);
bh.writeUInt32LE(binChunk.length, 0); bh.writeUInt32LE(0x004e4942, 4);
const out = path.join(__dirname, '..', 'bin', 'models', args.out || 'bend.glb');
fs.writeFileSync(out, Buffer.concat([header, jh, jsonChunk, bh, binChunk]));
console.log('wrote ' + out + ' (' + count + ' vertices)');
