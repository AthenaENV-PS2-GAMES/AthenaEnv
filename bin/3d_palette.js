// C3: exact T4/T8 palettes, CLUT switches, video reset and VRAM pressure.
// The measured deltas include the CLUT. Run with 3d_palette.ini.
const mode=Screen.getMode();
mode.height*=mode.interlace===Screen.INTERLACED&&mode.field===Screen.FRAME?2:1;
mode.zbuffering=true;mode.psmz=Screen.Z16S;Screen.setMode(mode);
const camera=new Camera3D.Camera({aspect:mode.width/mode.height,near:.5,far:50})
    .lookAt(0,0,-1).setPosition(0,0,0);
const stages=['palettes','alpha','reset','pressure'];
let frame=0,stage=-1,resources=[],items=[],allocations=[];
function keep(x){resources.push(x);return x;}
function release(){for(const x of resources)x.dispose();resources=[];items=[];}
function add(size,colors,x,y,scale,mask){
    const pixels=new Uint32Array(size*size);
    if(size===512){
        for(let row=0;row<size;row++){
            const index=(row>>5)&15;
            pixels.fill(0xff000000|(index*17)|((15-index)*17<<16),row*size,(row+1)*size);
        }
    }else for(let row=0;row<size;row++)for(let col=0;col<size;col++){
        const index=mask?((col>>3)+(row>>3))&1:((row*16/size|0)*16+(col*16/size|0))%colors;
        pixels[row*size+col]=mask?(index?0xffe0c030:0x00e0c030):
            (0xff000000|((index&15)*17)|((index>>4)*17<<8)|((15-(index&15))*17<<16));
    }
    const before=Screen.getMemoryStats();
    const texture=keep(Model3D.Texture.fromPixels({width:size,height:size,pixels}));texture.upload();
    const allocated=Screen.getMemoryStats()-before;
    const expected=(colors<=16?size*size/2+256:size*size+1024);
    if(allocated!==expected)throw new Error('VRAM palette allocation '+allocated+' != '+expected);
    allocations.push({size,colors,allocated,ct32:size*size*4});
    const mesh=keep(Model3D.Mesh.fromGeometry({
        positions:new Float32Array([-1,-1,0,1,-1,0,1,1,0,-1,-1,0,1,1,0,-1,1,0]),
        texcoords:new Float32Array([0,1,1,1,1,0,0,1,1,0,0,0]),
        material:{texture,alphaCutoff:mask?.5:undefined}}));
    items.push(keep(mesh.createInstance().setScale(scale,scale,scale).setPosition(x,y,-5)));
}
function create(){
    // Reset must invalidate residency while retained indexed CPU buffers remain valid.
    if(stage===2){Screen.setMode(mode);for(const x of resources)if(x.upload)x.upload();return;}
    release();Screen.setMode(mode);allocations=[];
    if(stage===0){add(64,16,-1.1,0,.95,false);add(64,256,1.1,0,.95,false);}
    else if(stage===1)add(64,2,0,0,1.5,true);
    else for(let i=0;i<8;i++)add(512,16,((i&3)-1.5)*1.1,i<4?.6:-.6,.48,false);
}
Loop.run({draw(){
    const next=Math.floor(frame/180),local=frame%180;
    if(next!==stage){stage=next;if(stage===stages.length){release();camera.dispose();
        console.log('3D palette complete');Loop.stop();return;}create();}
    let triangles=0;Render3D.group(()=>{for(const x of items)triangles+=Render3D.draw(x,camera,Render3D.CULL_NONE).triangles;});
    if(local===90)console.log('3D_PALETTE '+JSON.stringify({stage:stages[stage],allocations,triangles,
        vram:Screen.getMemoryStats(),memory:System.getMemoryStats().used}));
    for(let i=0;i<stages.length;i++)Draw.rect(12+i*20,12,14,14,Color.new(i===stage?255:64,128,0));
    frame++;
}});
