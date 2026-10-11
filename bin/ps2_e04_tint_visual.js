/* E04 visual check: Render3D tint on UNLIT, DIFFUSE and indexed vertex-color draws.
 * Row 1: tint white/red/half/blue-green on UNLIT quads (same group, tint changes between draws).
 * Row 2: the same on DIFFUSE quads. Row 3: indexed cube, white then tinted. */
import * as Model3D from "Model3D";
import * as Render3D from "Render3D";
import * as Lights from "Lights";
import {Camera} from "Camera3D";
const mode=Screen.getMode(); mode.zbuffering=true; mode.psmz=Screen.Z16S; Screen.setMode(mode);
const quad = new Float32Array([-.4,-.4,0, .4,-.4,0, .4,.4,0, -.4,-.4,0, .4,.4,0, -.4,.4,0]);
const col = new Float32Array(6*4); for(let i=0;i<col.length;i++) col[i]=1; /* Float32Array.fill() writes zeros on the EE runtime */
const unlit = Model3D.Mesh.fromGeometry({positions:quad,colors:col,material:{baseColor:new Float32Array([.9,.8,.6,1])}});
const diffuse = Model3D.Mesh.fromGeometry({positions:quad,material:{shading:Model3D.DIFFUSE,baseColor:new Float32Array([.9,.8,.6,1])}});
const cube = Model3D.Mesh.fromGeometry({
    positions:new Float32Array([-1,-1,-1,1,-1,-1,1,1,-1,-1,1,-1,-1,-1,1,1,-1,1,1,1,1,-1,1,1]),
    colors:new Float32Array([1,0,0,1,0,1,0,1,0,0,1,1,1,1,0,1,1,0,1,1,0,1,1,1,1,1,1,1,.4,.6,1,1]),
    indices:new Uint32Array([0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5])});
const tints = [[1,1,1],[1,0,0],[.5,.5,.5],[0,.7,1]];
const rows = [[unlit,1.2],[diffuse,0],[cube,-1.5]];
const lights = new Lights.Set().setAmbient(.4,.4,.4).setDirectional(0,0,0,-1,1,1,1);
const camera = new Camera({fov:50,near:1,far:50,aspect:640/448}); camera.setPosition(0,0,6).lookAt(0,0,0);
let frames = 0;
Loop.run(()=>{
    Screen.clear(Color.new(40,40,60));
    Render3D.group(()=>{
        rows.forEach(([mesh,y],r)=>tints.forEach((t,i)=>{
            const inst = mesh.createInstance().setPosition(-3+i*2,y,0);
            if(mesh===cube) inst.setScale(.35,.35,.35).setRotationQuaternion(.2,.3,0,.93);
            Render3D.setTint(t[0],t[1],t[2]);
            Render3D.draw(inst,camera,Render3D.CULL_NONE,mesh===diffuse?lights:undefined);
            inst.dispose();
        }));
        Render3D.setTint(1,1,1);
    });
    ++frames;
    if(frames===60) console.log("E04_TINT_SNAP frames=60");
    if(frames===200) console.log("E04_TINT_VISUAL: frames=200 tint="+Render3D.getTint().join(","));
});
