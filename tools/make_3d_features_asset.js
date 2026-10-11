// Fixtures for bin/3d_features.js, using Node built-ins only:
//   models/palette8.png   64x64 8-bit paletted PNG (four colour bands at CLUT
//                         indices 3, 9, 18, 200; other entries magenta)
//   models/alpha_mask.png 64x64 RGBA PNG: checker with transparent round holes
//   models/embedded.glb   quad whose base colour image (alpha_mask.png) is
//                         embedded in the GLB binary chunk, sampled with the
//                         glTF default (no sampler: repeat), UVs tiled 0..2,
//                         alphaMode MASK with alphaCutoff 0.5.
const fs = require('fs'), path = require('path'), zlib = require('zlib');
function crc32(buf) { let c=0xffffffff; for(const b of buf) { c^=b; for(let i=0;i<8;i++) c=(c>>>1)^((c&1)?0xedb88320:0); } return (c^0xffffffff)>>>0; }
function chunk(type,bytes) { const t=Buffer.from(type), out=Buffer.alloc(bytes.length+12); out.writeUInt32BE(bytes.length,0);
    t.copy(out,4); bytes.copy(out,8); out.writeUInt32BE(crc32(Buffer.concat([t,bytes])),bytes.length+8); return out; }
function png(width,height,colorType,rows,extra=[]) {
    const ihdr=Buffer.alloc(13); ihdr.writeUInt32BE(width,0); ihdr.writeUInt32BE(height,4); ihdr[8]=8; ihdr[9]=colorType;
    return Buffer.concat([Buffer.from('89504e470d0a1a0a','hex'),chunk('IHDR',ihdr),...extra,
        chunk('IDAT',zlib.deflateSync(rows)),chunk('IEND',Buffer.alloc(0))]);
}
// Paletted: horizontal bands red, green, blue, yellow at indices 3, 9, 18 and
// 200; every other entry is magenta. The GS CLUT (CSM1) swaps entries 8..15
// and 16..23 of each 32: a wrong unswizzle shows magenta bands.
const bands=[[3,[230,40,40]],[9,[40,200,40]],[18,[40,60,230]],[200,[240,220,40]]];
const palette=Buffer.alloc(256*3);
for(let i=0;i<256;i++) palette.set([255,0,255],i*3);
for(const [index,rgb] of bands) palette.set(rgb,index*3);
const indexed=Buffer.alloc(64*65);
for(let y=0;y<64;y++) for(let x=0;x<64;x++) indexed[y*65+1+x]=bands[Math.min(3,y>>4)][0];
const palette8=png(64,64,3,indexed,[chunk('PLTE',palette)]);
// RGBA: white/blue checker, alpha 0 inside a circle of each 16x16 cell.
const rgba=Buffer.alloc(64*(1+64*4));
for(let y=0;y<64;y++) for(let x=0;x<64;x++) {
    const i=y*257+1+x*4, light=((x>>4)^(y>>4))&1, dx=(x&15)-7.5, dy=(y&15)-7.5;
    rgba[i]=light?250:40; rgba[i+1]=light?250:90; rgba[i+2]=light?250:220;
    rgba[i+3]=dx*dx+dy*dy<20?0:255;
}
const alphaMask=png(64,64,6,rgba);
// Quad in the XY plane, two triangles, UVs 0..2 (tiled twice).
const positions=[-1,-1,0, 1,-1,0, 1,1,0, -1,-1,0, 1,1,0, -1,1,0], uvs=[0,2, 2,2, 2,0, 0,2, 2,0, 0,0];
const geometry=Buffer.alloc((positions.length+uvs.length)*4);
[...positions,...uvs].forEach((v,i)=>geometry.writeFloatLE(v,i*4));
const imageOffset=geometry.length, binary=Buffer.alloc((geometry.length+alphaMask.length+3)&~3);
geometry.copy(binary,0); alphaMask.copy(binary,imageOffset);
const json={asset:{version:'2.0',generator:'AthenaEnv 3D features fixture'},buffers:[{byteLength:binary.length}],
    bufferViews:[{buffer:0,byteOffset:0,byteLength:72},{buffer:0,byteOffset:72,byteLength:48},
        {buffer:0,byteOffset:imageOffset,byteLength:alphaMask.length}],
    accessors:[{bufferView:0,componentType:5126,count:6,type:'VEC3',min:[-1,-1,0],max:[1,1,0]},
        {bufferView:1,componentType:5126,count:6,type:'VEC2'}],
    images:[{bufferView:2,mimeType:'image/png'}],textures:[{source:0}],
    materials:[{pbrMetallicRoughness:{baseColorTexture:{index:0}},alphaMode:'MASK',alphaCutoff:0.5,doubleSided:true}],
    meshes:[{primitives:[{attributes:{POSITION:0,TEXCOORD_0:1},material:0,mode:4}]}],
    nodes:[{mesh:0}],scenes:[{nodes:[0]}],scene:0};
const text=Buffer.from(JSON.stringify(json)), padded=Buffer.alloc((text.length+3)&~3,0x20); text.copy(padded);
const glb=Buffer.alloc(28+padded.length+binary.length);
glb.writeUInt32LE(0x46546c67,0); glb.writeUInt32LE(2,4); glb.writeUInt32LE(glb.length,8);
glb.writeUInt32LE(padded.length,12); glb.writeUInt32LE(0x4e4f534a,16); padded.copy(glb,20);
glb.writeUInt32LE(binary.length,20+padded.length); glb.writeUInt32LE(0x004e4942,24+padded.length); binary.copy(glb,28+padded.length);
const dest=path.join(__dirname,'../bin/models'); fs.mkdirSync(dest,{recursive:true});
fs.writeFileSync(path.join(dest,'palette8.png'),palette8);
fs.writeFileSync(path.join(dest,'alpha_mask.png'),alphaMask);
fs.writeFileSync(path.join(dest,'embedded.glb'),glb);
console.log(`3D feature fixtures: palette8.png ${palette8.length} B, alpha_mask.png ${alphaMask.length} B, embedded.glb ${glb.length} B`);
