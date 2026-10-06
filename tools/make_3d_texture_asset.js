// Deterministic GLB + external RGBA PNG, using Node built-ins only.
const fs = require('fs'), path = require('path'), zlib = require('zlib');
const points = [[-1,-1,-1],[1,-1,-1],[1,1,-1],[-1,1,-1],[-1,-1,1],[1,-1,1],[1,1,1],[-1,1,1]];
const faces = [[[0,3,2,1],[0,0,-1]],[[4,5,6,7],[0,0,1]],[[0,1,5,4],[0,-1,0]],
    [[3,7,6,2],[0,1,0]],[[0,4,7,3],[-1,0,0]],[[1,2,6,5],[1,0,0]]];
const positions=[], normals=[], uvs=[], corners=[[0,1],[0,0],[1,0],[1,1]];
for (const [quad,normal] of faces) for (const i of [0,1,2,0,2,3]) {
    positions.push(...points[quad[i]]); normals.push(...normal); uvs.push(...corners[i]);
}
const data=Buffer.alloc((positions.length+normals.length+uvs.length)*4);
[...positions,...normals,...uvs].forEach((v,i)=>data.writeFloatLE(v,i*4));
const json={asset:{version:'2.0',generator:'AthenaEnv texture fixture'},buffers:[{byteLength:data.length}],
    bufferViews:[{buffer:0,byteOffset:0,byteLength:432,target:34962},{buffer:0,byteOffset:432,byteLength:432,target:34962},
        {buffer:0,byteOffset:864,byteLength:288,target:34962}],
    accessors:[{bufferView:0,componentType:5126,count:36,type:'VEC3',min:[-1,-1,-1],max:[1,1,1]},
        {bufferView:1,componentType:5126,count:36,type:'VEC3'},{bufferView:2,componentType:5126,count:36,type:'VEC2'}],
    images:[{uri:'checker.png'}],samplers:[{magFilter:9728,minFilter:9728,wrapS:33071,wrapT:33071}],
    textures:[{source:0,sampler:0}],materials:[{pbrMetallicRoughness:{baseColorTexture:{index:0},metallicFactor:0,roughnessFactor:1}}],
    meshes:[{primitives:[{attributes:{POSITION:0,NORMAL:1,TEXCOORD_0:2},material:0,mode:4}]}],
    nodes:[{mesh:0}],scenes:[{nodes:[0]}],scene:0};
const text=Buffer.from(JSON.stringify(json)), padded=Buffer.alloc((text.length+3)&~3,0x20); text.copy(padded);
const glb=Buffer.alloc(28+padded.length+data.length);
glb.writeUInt32LE(0x46546c67,0); glb.writeUInt32LE(2,4); glb.writeUInt32LE(glb.length,8);
glb.writeUInt32LE(padded.length,12); glb.writeUInt32LE(0x4e4f534a,16); padded.copy(glb,20);
glb.writeUInt32LE(data.length,20+padded.length); glb.writeUInt32LE(0x004e4942,24+padded.length); data.copy(glb,28+padded.length);
function crc32(buf) { let c=0xffffffff; for(const b of buf) { c^=b; for(let i=0;i<8;i++) c=(c>>>1)^((c&1)?0xedb88320:0); } return (c^0xffffffff)>>>0; }
function chunk(type,bytes) { const t=Buffer.from(type), out=Buffer.alloc(bytes.length+12); out.writeUInt32BE(bytes.length,0);
    t.copy(out,4); bytes.copy(out,8); out.writeUInt32BE(crc32(Buffer.concat([t,bytes])),bytes.length+8); return out; }
const ihdr=Buffer.alloc(13); ihdr.writeUInt32BE(64,0); ihdr.writeUInt32BE(64,4); ihdr[8]=8; ihdr[9]=6;
const scan=Buffer.alloc(64*(1+64*4)), palette=[[255,64,64],[64,255,64],[64,64,255],[255,255,64]];
for(let y=0;y<64;y++) for(let x=0;x<64;x++) {
    const base=palette[(y>=32?2:0)+(x>=32?1:0)], factor=((x>>3)^(y>>3))&1?1:.35;
    const i=y*257+1+x*4; for(let c=0;c<3;c++) scan[i+c]=Math.round(base[c]*factor); scan[i+3]=255;
}
const png=Buffer.concat([Buffer.from('89504e470d0a1a0a','hex'),chunk('IHDR',ihdr),chunk('IDAT',zlib.deflateSync(scan)),chunk('IEND',Buffer.alloc(0))]);
const dest=path.join(__dirname,'../bin/models'); fs.mkdirSync(dest,{recursive:true});
fs.writeFileSync(path.join(dest,'textured_cube.glb'),glb); fs.writeFileSync(path.join(dest,'checker.png'),png);
console.log(`Texture fixtures: GLB ${glb.length} bytes, PNG ${png.length} bytes (64x64 RGBA)`);
