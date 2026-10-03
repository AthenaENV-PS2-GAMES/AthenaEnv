// Soak workload (tests/soak_test.js): images. An ImageList worker decoding,
// requests queued and textures in VRAM when the VM is torn down.
const FRAMES = 90;
const FILES = ["tests/my_image.png", "tests/texture.png"];

const font = new Font();
const list = new ImageList({ workers: 1 });
const shown = [];

let frames = 0;
Loop.run(() => {
    // Requests for a path already pending are merged, so at most one per file is in flight.
    if (list.pending() === 0) {
        for (const file of FILES)
            list.load(file, {
                upload: frames % 3 ? "draw" : "bind",
                onLoad: image => {
                    shown.push(image);
                    if (shown.length > 6) shown.shift().free();
                },
            });
    }
    list.process(1);
    shown.forEach((image, i) => image.draw(20 + 100 * (i % 6), 120));
    font.print(40, 40, "Soak: ImageList, frame " + frames);
    if (++frames === FRAMES)
        std.reload("tests/soak_test.js", { returnTo: "tests/index.js" });
});
