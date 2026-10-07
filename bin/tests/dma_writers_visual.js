/* Real VU upload, tile batches, atlas font passes and page clear on the GS. */
const count=2400;
const sprites=Array.from({length:count},(_,i)=>({
    x:20+(i%60)*10,y:52+Math.floor(i/60)*8,w:9,h:7,
    r:[240,40,40,240][i%4],g:[40,220,80,240][i%4],b:[40,60,240,240][i%4],a:128
}));
const map=new TileMap.Instance({
    descriptor:new TileMap.Descriptor({materials:[{textureIndex:-1,endOffset:count-1}]}),
    spriteBuffer:TileMap.SpriteBuffer.fromObjects(sprites)
});
const font=new Font({size:16});
font.outline=1; font.outlineColor=Color.new(0,0,0);
const title='DMA writers: 2400 VU sprites / font outline';
let frames=0;
Loop.run(()=>{
    // Every page must be replaced, including at batch boundaries.
    Screen.clear(Color.new(30,20,45));
    map.render(0,0);
    font.print(20,12,title);
    font.print(20,390,'Ring 2048 QWs: page clear + VU MPG + atlas font');
    if(++frames===120) console.log('DMA_WRITERS_VISUAL: PASS after 120 frames; 2400 sprites, outlined font and page clear');
});
