// Projector geometry tests with a synthetic renderer. GS packets are tested
// separately by render3d_packet_test.c; this test makes no rasterization claim.
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
let builds=0, draws=0, mode=-1, grouped=false, capturing=false;
class Texture {
    static fromPixels(g) { const t=new Texture(); t.pixels=g.pixels; return t; }
    static renderTarget(width,height,filter) {
        assert([64,128,256,512].includes(width)&&[64,128,256,512].includes(height));
        assert([0,1].includes(filter));const t=new Texture();t.target=true;t.valid=false;return t;
    }
    upload(){assert(!this.dead&&(!this.target||this.valid&&!capturing));return this;}
    dispose() { this.dead=true; }
}
class Mesh {
    static fromGeometry(g) { const m=new Mesh(); m.geometry=g; builds++; return m; }
    createInstance() { const mesh=this; return {mesh,dispose(){this.dead=true;}}; }
    getBounds() {
        const p=this.geometry.positions, lo=[Infinity,Infinity,Infinity], hi=[-Infinity,-Infinity,-Infinity];
        for(let i=0;i<p.length;i++) { lo[i%3]=Math.min(lo[i%3],p[i]);hi[i%3]=Math.max(hi[i%3],p[i]); }
        return [...lo,...hi];
    }
    dispose() { this.dead=true; }
}
const Model3D={Texture,Mesh,UV_LIMIT:16,UNLIT:0};
const Render3D={CULL_NONE:0,draw(i){assert(!i.dead&&!i.mesh.dead);assert(mode>=0||capturing);draws++;},
    captureSilhouette(t,fn){assert(t.target&&!t.dead&&!grouped&&!capturing);capturing=true;t.valid=false;
        try{const result=fn();t.valid=true;return result;}finally{capturing=false;}},
    group(fn){grouped=true;try{return fn();}finally{grouped=false;}},
    withBlend(m,fn){assert([-1,0,1,2].includes(m));assert(!grouped&&!capturing);const old=mode;mode=m;try{return fn();}finally{mode=old;}}};
class Camera { setPosition(){return this;} lookAt(){return this;} dispose(){} }
class World {
    addBox(){this.box=true;}
    raycast(o,d,length,options,out){
        const distance=-o[1]/d[1];
        if(!this.box||distance<0||distance>length) return null;
        return Object.assign(out,{x:o[0]+d[0]*distance,y:0,z:o[2]+d[2]*distance,nx:0,ny:1,nz:0});
    }
    dispose(){}
}
const context=vm.createContext({Model3D,Render3D,Camera3D:{Camera},Collision3D:{World},console,Float32Array,Uint32Array});
const source=readFileSync(new URL('../../src/modules/shadows/js/shadows.js',import.meta.url),'utf8')
    .replace(/^import .*;$/gm,'').replace(/^export /gm,'');
vm.runInContext(source+'\nglobalThis.Shadows={Projector,ShadowMap};',context);
const test=readFileSync(new URL('../../bin/shadows_test.js',import.meta.url),'utf8').replace(/^import .*;$/gm,'');
vm.runInContext(test,context);
assert(builds>0&&draws>0);assert.equal(mode,-1);
// Quaternion orientation, slope filtering, mirrored UVs, externally-owned texture.
vm.runInContext(`
const tex=new Model3D.Texture(); const q=new Shadows.Projector(tex);
q.setSize(2,4); q.rotation=[0,Math.SQRT1_2,0,Math.SQRT1_2];
q.setUVRect(1,0,0,1); q.render(new Camera3D.Camera());
const b=q._mesh.getBounds(); assert(Math.abs(b[0]+2)<1e-5 && Math.abs(b[5]-1)<1e-5,'quaternion footprint');
assert(q._mesh.geometry.texcoords[0]===1,'mirrored UV');
q.enableRaycast({raycast(o,d,l,opt,out){return Object.assign(out,{x:o[0],y:0,z:o[2],ny:.2});}},4).setSlopeLimit(.8);
q.render(new Camera3D.Camera()); assert(q._mesh===null,'steep terrain rejected');
q.dispose(); assert(!tex.dead,'borrowed texture remains live');
`,context);
console.log('Shadows geometry harness passed (synthetic renderer)');
