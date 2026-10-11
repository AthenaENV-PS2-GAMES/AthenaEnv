/* PCSX2/hardware regression: real decoders, GS texture upload and long bitmap text.
 * Boot with --ignorecfg --script=tests/memory_safety_visual.js. */
const base = 'tests/memory_safety/';
const label = new Font({size:18});
const names = ['rgb.png','rgba_adam7.png','palette4.png',
               'palette8_adam7.png','padded.bmp','top_down.bmp'];
const images = names.map(name => {
    const image = new Image(base + name);
    if (!image.ready()) throw new Error('Image not ready: ' + name);
    image.width = 96; image.height = 64;
    return image;
});
for (const name of ['truncated.bmp','truncated.png']) {
    let rejected = false;
    try { new Image(base + name); } catch (e) { rejected = true; }
    if (!rejected) throw new Error('Truncated fixture accepted: ' + name);
}
const bitmap = new Font(base + 'bitmap_font.png');
bitmap.scale = 0.35;
bitmap.color = Color.new(245,245,245);
let text = '';
for (let line=0; line<16; line++) text += 'A'.repeat(75) + '\n';
text += 'A'.repeat(75);
console.log('MEMORY_SAFETY_VISUAL: loaded 6 images; truncated inputs rejected; 1275 bitmap glyphs');
let frames=0;
Loop.run(() => {
    label.print(20,12,'Memory safety: PNG / Adam7 / BMP / DMA');
    for(let i=0;i<images.length;i++) {
        const x=25+(i%3)*205, y=48+Math.floor(i/3)*94;
        Draw.rect(x,y,96,64,Color.new(65,65,65));
        images[i].draw(x,y);
        label.print(x,y+65,names[i]);
    }
    label.print(20,242,'1275 glyphs, 17 rows: continuous batches');
    bitmap.print(20,265,text);
    if(++frames===120) console.log('MEMORY_SAFETY_VISUAL: PASS after 120 rendered frames');
});
