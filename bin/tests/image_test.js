function assertEqual(actual, expected, message) {
    if (actual !== expected)
        throw new Error(`${message}: expected ${expected}, got ${actual}`);
}

const image = new Image();
assertEqual(image.ready(), false, "empty image readiness");
assertEqual(image.renderable, false, "empty image renderability");
image.texWidth = 2;
image.texHeight = 2;
image.bpp = 32;
image.pixels = new ArrayBuffer(16);
assertEqual(image.size, 16, "image size");
assertEqual(image.bpp, 32, "image bpp");
assertEqual(image.ready(), true, "image readiness after pixel upload");
assertEqual(image.width, 0, "default draw width");
image.width = 2;
image.height = 2;
assertEqual(image.width, 2, "image width");
assertEqual(image.height, 2, "image height");
print("image_test passed");
