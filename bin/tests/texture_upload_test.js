/*
 * Texture upload ordering under VRAM pressure (docs/TEXTURE_UPLOAD.md).
 *
 * Six 512x512 CT32 atlases (1 MB each) cannot all stay in VRAM, so every
 * frame rebinds and re-uploads most of them. Each atlas has one colour and
 * is drawn twice per frame, in two orders: a tile showing another tile's
 * colour, a stripe of it, or a missing tile is an ordering bug. Next to them
 * are a paletted (T8) texture, whose CLUT and pixels upload together, and a
 * 1024x256 CT32 texture (1 MB), which takes three IMAGE chunks.
 *
 * The top line shows the bytes uploaded to VRAM during the last frame.
 */
const font = new Font();

const COLORS = [
    [255, 60, 60], [60, 220, 60], [70, 110, 255],
    [255, 220, 40], [230, 70, 230], [40, 220, 230],
];

// CT32 texels are 0xAABBGGRR, with 0x80 as opaque alpha.
const texel = ([r, g, b]) => (0x80 << 24 | b << 16 | g << 8 | r) >>> 0;

function solid(width, height, rgb) {
    const image = new Image();
    image.texWidth = width;
    image.texHeight = height;
    image.bpp = 32;
    const pixels = new ArrayBuffer(width * height * 4);
    new Uint32Array(pixels).fill(texel(rgb));
    image.pixels = pixels;
    return image;
}

function indexed(width, height) {
    const image = new Image();
    image.texWidth = width;
    image.texHeight = height;
    image.bpp = 8;
    const palette = new ArrayBuffer(256 * 4);
    const entries = new Uint32Array(palette);
    entries.fill(texel([0, 0, 0]));
    entries[1] = texel([255, 255, 255]);
    entries[2] = texel([255, 140, 0]);
    image.palette = palette;
    // Horizontal bands of index 1 and 2, 32 lines each.
    const pixels = new Uint8Array(width * height);
    for (let y = 0; y < height; y++)
        pixels.fill((y >> 5) & 1 ? 2 : 1, y * width, (y + 1) * width);
    image.pixels = pixels.buffer;
    return image;
}

const atlases = COLORS.map(rgb => solid(512, 512, rgb));
const paletted = indexed(256, 256);
const large = solid(1024, 256, [255, 255, 255]);

const TILE = 64;
const GAP = 8;
const LEFT = 24;
const ROW_A = 60;
const ROW_B = ROW_A + TILE + GAP;
const ROW_C = ROW_B + TILE + 2 * GAP;

function tile(image, x, y, w = TILE, h = TILE) {
    // The whole texture: an Image made in code keeps its first source rect.
    image.startx = 0;
    image.starty = 0;
    image.endx = image.texWidth;
    image.endy = image.texHeight;
    image.width = w;
    image.height = h;
    image.draw(x, y);
}

let frames = 0;

Loop.run(() => {
    frames++;

    // Row A in order, row B in reverse: each bind evicts a block that a
    // tile drawn earlier this frame still samples.
    atlases.forEach((image, i) => tile(image, LEFT + i * (TILE + GAP), ROW_A));
    atlases.slice().reverse().forEach((image, i) =>
        tile(image, LEFT + (atlases.length - 1 - i) * (TILE + GAP), ROW_B));

    // The other textures, interleaved with atlases so they are evicted too.
    tile(paletted, LEFT, ROW_C, 128, 128);
    tile(atlases[frames % atlases.length], LEFT + 136, ROW_C, 32, 32);
    tile(large, LEFT + 176, ROW_C, 256, 128);
    tile(atlases[(frames + 3) % atlases.length], LEFT + 440, ROW_C, 32, 32);

    font.print(LEFT, 16, `uploaded ${(Screen.getMemoryStats(Screen.VRAM_UPLOADED) / 1024).toFixed(0)} KB/frame` +
        `   fps ${Loop.getStats().fps.toFixed(1)}`);
    font.print(LEFT, ROW_C + 140, "Rows 1-2 match, no stripes.");
    font.print(LEFT, ROW_C + 164, "Bands white/orange, wide white bar.");
}, { clearColor: Color.new(20, 20, 30) });
