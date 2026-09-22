// Set this to the PNG path relative to the bin/ directory.
const PNG_PATH = "my_image.png";

const image = new Image(PNG_PATH);

if (!image.ready())
    throw new Error(`Failed to load PNG: ${PNG_PATH}`);

image.width = image.texWidth;
image.height = image.texHeight;
image.startx = 0;
image.starty = 0;
image.endx = image.texWidth;
image.endy = image.texHeight;

// Lock the texture in VRAM so it is never evicted by the texture manager.
// Without this, the async upload completes on frame 1 but the block weight
// becomes 0 after the first nextFrame() tick, making it a candidate for
// eviction on any subsequent alloc — causing the image to disappear.
image.lock();

while (true) {
    Screen.clear(Color.new(24, 24, 32, 128));
    image.draw(
        (Screen.getMode().width - image.width) / 2,
        (Screen.getMode().height - image.height) / 2
    );
    Screen.flip();
}

