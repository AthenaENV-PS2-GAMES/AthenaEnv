/* E03 closing increment: peak blocking span per Assets3D.update() call, atomic
 * versus resumable loaders in ONE build. config.mode: "atomic" shadows Model3D
 * and GLTF3D without beginMemory (Assets3D then uses loadMemory / the path
 * loaders, with their synchronous I/O); "stepped" uses Model3D.Loader and
 * GLTF3D.Loader, including the explicit resolver for external files.
 * config.budgetMs (default 0: one unit per call) is Assets3D's soft budget. config.mode is
 * "atomic" (GLTF3D shadowed without beginMemory, so Assets3D falls back to
 * GLTF3D.loadMemory) or "stepped" (GLTF3D.Loader units). Each task also
 * records Assets3D.stats().phases, reset at its start. */
import * as Loop from "Loop";
import * as Assets3D from "Assets3D";
import { Assets } from "Scene";
import { ticks, ticksToMilliseconds } from "ProfilerNative";
const config=JSON.parse(std.loadFile("world_systems_bench_config.json"));
const warmup=config.warmup??30, samples=config.samples??120, mode=config.mode;
if(mode!=="atomic"&&mode!=="stepped") throw new Error("config.mode must be atomic or stepped");
if(mode==="atomic") {
    globalThis.GLTF3D={load:GLTF3D.load,loadMemory:GLTF3D.loadMemory};
    globalThis.Model3D={load:Model3D.load,loadMemory:Model3D.loadMemory,Texture:Model3D.Texture,Mesh:Model3D.Mesh};
} else if(typeof GLTF3D.beginMemory!=="function"||typeof Model3D.beginMemory!=="function") throw new Error("stepped mode needs beginMemory");
const tasks=[
    {name:"assets3d.mesh",kind:"meshes",path:"models/lit_cube.glb"},
    {name:"assets3d.mesh-large",kind:"meshes",path:"models/grid_mesh.glb",warmup:2,samples:6} /* ~1300 frames per stepped request at budget 0: fewer requests keep the report within the JS heap */,
    {name:"assets3d.gltf-external",kind:"gltf",path:"models/ext_bend.gltf"},
    {name:"assets3d.scene-read",kind:"gltf",path:"models/arm_read.glb"},
    {name:"assets3d.scene-construct",kind:"gltf",path:"models/bend_hi.glb"},
    {name:"assets3d.texture-upload",kind:"textures3d",path:"models/checker.png",upload:true},
];
const globalLoop=globalThis.Loop;
globalThis.Loop=undefined; // exercise the documented manual pump, timed explicitly
Assets3D.setBudget(config.budgetMs??0);
const cpu=[],wall=[],results=[];
let first=true, index=0, round=0, group=null, request=null, finishing=false;
let work=[],batch=[],metrics=[];
function stats(values) {
    const s=values.slice().sort((a,b)=>a-b);
    return {averageMs:s.reduce((a,b)=>a+b,0)/s.length,p95Ms:s[Math.ceil(s.length*.95)-1],
        minMs:s[0],maxMs:s[s.length-1]};
}
function frames(values) {return {...stats(values),sampleMs:values,overBudget:values.filter(v=>v>1000/60).length};}
const observer={priority:-1000000,realTime:true,preUpdate() {
    if (!first) {const s=Loop.getStats();cpu.push(s.cpuMs);wall.push(s.frameMs);}
    first=false;
    if(finishing) {
        const report={version:2,label:"assets3d-e03-final",status:"completed",frameBudgetMs:1000/60,
            metadata:{...config.metadata,videoMode:Screen.getMode(),warmup,samples,ticksPerMs:1/ticksToMilliseconds(1),
                workScope:"maximum Assets3D.update CPU span per request; includes read submit/poll/construction; texture-upload includes GS wait (see task gsWait)",
                batchScope:"sum of Assets3D.update spans per request across all active frames; acquire/release/Scene.Assets.update outside spans",
                frameScope:"all load frames including warmup, acquire, release, stats conversion and checkpoints; final JSON save excluded",
                manualPump:true,mode,budgetMs:config.budgetMs??0,memoryAfter:System.getMemoryStats()},results,
            frames:{cpu:frames(cpu),wall:frames(wall)}};
        Bench.save(report,config.path);globalThis.Loop=globalLoop;Loop.removeSystem(observer);Loop.stop();
        console.log("[Assets3DBench] completed");
    }
}};
function update() {
    if (finishing) return;
    const task=tasks[index];
    if(!group) {
        group=Assets.acquire({[task.kind]:{asset:task.upload?{path:task.path,upload:true}:task.path}});
        if(round===0) Assets3D.resetStats();
        request={frames:0,peakTicks:0,totalTicks:0,readingFrames:0,progress:[]};
    }
    if(group.failed) throw group.error;
    if(group.done) {
        const load=Assets3D.stats().lastLoad??null;
        const taskWarmup=task.warmup??warmup,taskSamples=task.samples??samples;
        metrics.push({...request,load,warmup:round<taskWarmup});
        if(round>=taskWarmup) {work.push(request.peakTicks);batch.push(ticksToMilliseconds(request.totalTicks));}
        group.release();group=null;Assets.update(0);
        if(++round===taskWarmup+taskSamples) {
            const sampleMs=work.map(t=>ticksToMilliseconds(t));
            results.push({name:task.name,status:"completed",warmup:taskWarmup,samples:taskSamples,iterations:1,
                ...stats(sampleMs),sampleTicks:work,sampleMs,batchCpu:{...stats(batch),sampleMs:batch},
                metadata:{kind:task.kind,path:task.path,workScope:"peak blocking update per request",gsWait:!!task.upload,phases:Assets3D.stats().phases},metrics:{requests:metrics}});
            console.log(`[Assets3DBench] ${task.name}: ${results[results.length-1].averageMs.toFixed(3)} ms peak/update`);
            work=[];batch=[];metrics=[];round=0;
            if(++index===tasks.length) finishing=true;
        }
        return;
    }
    const start=ticks();Assets3D.update();const span=(ticks()-start)>>>0;
    request.peakTicks=Math.max(request.peakTicks,span);request.totalTicks+=span;request.frames++;
    const p=Assets3D.pending()[0];
    if(p?.phase==="reading")request.readingFrames++;
    request.progress.push(span); // integer ticks per update: a stepped load spans hundreds of frames, keep the report small
    Assets.update(0);
}
Loop.addSystem(observer);
Loop.run(update,{maxDelta:0});
