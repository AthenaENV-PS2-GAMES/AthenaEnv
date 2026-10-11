// Synthetic CPU/job states validate peak/sum and complete-frame capture only.
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const source=(await readFile(new URL('../../bin/assets3d_bench.js',import.meta.url),'utf8'))
    .replace(/^import .*;$/gm,'');
for(const async of [false,true]) {
    let clock=0,group=null,polls=0,saved=null,running=true,lastFrame=0;
    const systems=[];
    const Loop={addSystem(s){systems.push(s);},removeSystem(s){systems.splice(systems.indexOf(s),1);},
        run(){},stop(){running=false;},getStats(){return{cpuMs:lastFrame,frameMs:17};}};
    const Assets={acquire(){polls=0;return group={done:false,failed:false,release(){}};},update(){}};
    const Assets3D={setBudget(){},update(){clock+=async?(polls++<2?100:7000):10000;
        if(!async||polls>2)group.done=true;},stats(){return{lastLoad:null};},
        pending(){return async&&!group.done?[{phase:'reading',bytesDone:polls*10,bytesTotal:100}]:[];}};
    const config={path:'out',warmup:1,samples:2,metadata:{}};
    const context=vm.createContext({__Loop:Loop,__Assets:Assets,__Assets3D:Assets3D,
        ticks:()=>++clock,ticksToMilliseconds:t=>t/1000,
        std:{loadFile:()=>JSON.stringify(config)},Screen:{getMode:()=>({})},
        System:{getMemoryStats:()=>({})},Bench:{save(r){saved=JSON.parse(JSON.stringify(r));}},console:{log(){}}});
    vm.runInContext('const Loop=__Loop, Assets=__Assets, Assets3D=__Assets3D;\n'+source,context);
    let count=0;
    while(running){assert(count++<100);const start=clock;
        for(const s of systems.slice())s.preUpdate();
        if(running)vm.runInContext('update()',context);
        lastFrame=(clock-start)/1000;
    }
    assert.equal(saved.status,'completed');assert.equal(saved.results.length,4);
    for(const result of saved.results){
        assert.equal(result.sampleMs.length,2);assert.equal(result.metrics.requests.length,3);
        assert.equal(result.metrics.requests[0].warmup,true);
        assert(result.batchCpu.averageMs>=result.averageMs);
    }
    const expected=saved.results.flatMap(r=>r.metrics.requests).reduce((n,q)=>n+q.frames+1,0);
    assert.equal(saved.frames.cpu.sampleMs.length,expected);
    assert.equal(saved.frames.wall.sampleMs.length,expected);
    assert.equal(systems.length,0);
}
console.log('Assets3D harness passed (synthetic jobs; no performance evidence)');
