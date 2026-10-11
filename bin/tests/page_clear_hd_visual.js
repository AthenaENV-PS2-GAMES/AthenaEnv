/* 1020 pages need two clear batches in a 2048-QW ring (1012 + 8).
 * One CT16 buffer, no Z buffer: fits in 4 MiB VRAM without font textures. */
const mode=Screen.getMode();
Screen.setMode({...mode, mode:Screen.DTV_1080i,width:1920,height:1080,
    psm:Screen.CT16,interlace:Screen.INTERLACED,field:Screen.FIELD,
    zbuffering:false,double_buffering:false});
const actual=Screen.getMode();
if(actual.width!==1920 || actual.height!==1080) throw new Error("Unexpected clear surface dimensions");
let frames=0;
while(true) {
    Screen.clear(Color.new(32,48,80));
    Draw.rect(0,0,96,96,Color.new(240,40,40));
    Draw.rect(1824,0,96,96,Color.new(40,220,60));
    Draw.rect(0,984,96,96,Color.new(40,80,240));
    Draw.rect(1824,984,96,96,Color.new(240,240,240));
    Screen.flip();
    if(++frames===120) console.log('PAGE_CLEAR_HD_VISUAL: PASS after 120 frames; 1920x1080 CT16; 1020 pages in ring 2048');
}
