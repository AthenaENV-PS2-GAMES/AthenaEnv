// In-memory Image example: creates a 64x64 checkerboard and draws it.

const width = 64;
const height = 64;
const pixels = new ArrayBuffer(width * height * 4);
const data = new Uint32Array(pixels);

for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
        const even = ((x >> 3) + (y >> 3)) % 2 === 0;
        data[y * width + x] = even
            ? Color.new(40, 160, 240, 128)
            : Color.new(240, 80, 80, 128);
    }
}

const image = new Image();
image.texWidth = width;
image.texHeight = height;
image.bpp = 32;
image.pixels = pixels;
image.width = 256;
image.height = 256;
image.color = Color.new(255, 255, 255, 128);

if (!image.ready())
    throw new Error("Image was not ready after assigning pixels");

while (true) {
    Screen.clear(Color.new(24, 24, 32, 128));
    image.draw(192, 112);
    Screen.flip();
}
