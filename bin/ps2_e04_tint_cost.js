/* E04 submission cost: 300 DIFFUSE cubes in one Render3D.group, CPU ms per frame (Timer ticks) for
 * tint constant white, tint changing every draw (worst case), and tint changing every 10th draw. */
const mode=Screen.getMode();
const height=mode.height*(mode.interlace===Screen.INTERLACED&&mode.field===Screen.FRAME?2:1);
mode.height=height; mode.zbuffering=true; mode.psmz=Screen.Z16S; Screen.setMode(mode);
const camera=new Camera3D.Camera({aspect:mode.width/height,near:.5,far:100}).setPosition(0,0,30).lookAt(0,0,0);
const lights=new Lights.Set().setAmbient(.4,.4,.4).setDirectional(0,-.4,-.6,-1,1,1,1);
const cube=Model3D.load("models/textured_cube.glb",{shading:Model3D.DIFFUSE});
const N=300,inst=[];
for(let i=0;i<N;i++) inst.push(cube.createInstance().setPosition((i%20)-9.5,Math.floor(i/20)-7,0).setScale(.4,.4,.4));
const timer=Timer.new();
const tints=[[1,1,1],[.4,.6,1]];
/* Tint index per draw, precomputed: JS arithmetic on the EE is slow and must not be measured. */
const plan=f=>{ const a=new Uint8Array(N); for(let i=0;i<N;i++) a[i]=f(i); return a; };
const modes=[["white",plan(()=>0)],["every",plan(i=>i&1)],["every10",plan(i=>Math.floor(i/10)&1)]];
let frame=0,acc=0,count=0,m=0; const results=[]; const stats={};
Loop.run(()=>{
    Screen.clear(Color.new(40,40,60));
    const start=Timer.getTime(timer),pick=modes[m][1];
    Render3D.group(()=>{ for(let i=0;i<N;i++) { const t=tints[pick[i]]; Render3D.setTint(t[0],t[1],t[2]); Render3D.draw(inst[i],camera,Render3D.CULL_BACK,lights,null); } });
    Render3D.setTint(1,1,1);
    const dt=Timer.getTime(timer)-start;
    if(++frame>30) { acc+=dt; count++; }
    if(frame===30+120) {
        const s=Render3D.frameStats({},true);
        console.log("E04C "+modes[m][0]+" avgTicks="+(acc/count).toFixed(1)+" (Timer units) draws="+N);
        frame=0; acc=0; count=0; if(++m>=modes.length) { console.log("E04C_DONE"); Loop.stop(); }
    }
});
