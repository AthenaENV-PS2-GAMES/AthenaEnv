/* Integration check for the production list and atlas-font paths on EE/GS.
 * Host regressions inspect every serialized vertex at the ring boundaries. */
Camera2D.setCurrent(null);
const camera=new Camera2D.Camera({x:320,y:224,rotation:0.10,pixelSnap:false});
const image=new Image('tests/memory_safety/rgb.png');
const sprites=TileMap.SpriteBuffer.fromObjects(Array.from({length:1920},(_,i)=>({
    x:20+(i%60)*10,y:50+Math.floor(i/60)*6,w:9,h:5,
    u1:0,v1:0,u2:64,v2:64,r:128,g:128,b:128,a:128
})));
const label=new Font({size:18});
const outlined=new Font({size:16});
outlined.scale=0.4;outlined.outline=0.5;outlined.outlineColor=Color.new(0,0,0);
const text=Array.from({length:12},()=> 'A'.repeat(112)).join('\n');
let frames=0;
Loop.run(()=>{
    Screen.clear(Color.new(25,30,45));
    camera.draw(()=>{
        image.drawList(sprites);
        outlined.print(20,266,text);
    });
    label.print(20,12,'Regression: 1920 image sprites / 1344 glyphs');
    label.print(20,408,'Rotated camera / atlas outline / ring 2048');
    if(++frames===120) console.log('DRAW_LISTS_REGRESSION_VISUAL: PASS after 120 frames; 1920 sprites; 1344 glyphs; rotated outline');
});
