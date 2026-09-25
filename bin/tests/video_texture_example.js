// Video as a texture: the playing video drawn rotated, cropped, tinted and
// through its `frame` Image. TRIANGLE quits.

const video = new Video("tests/video/short.m2v");
video.loop = true;
video.play();

const pad = Gamepad.player(0);
const BACKGROUND = Color.new(20, 20, 40);
const HALF_ALPHA = Color.new(255, 255, 255, 64);
const RED_TINT = Color.new(255, 96, 96, 128);

// One Image that always shows the current frame; set up once.
const frame = video.frame;
frame.width = 128;
frame.height = 72;

let angle = 0;
while (true) {
    Gamepad.update();
    if (pad.justPressed(Gamepad.TRIANGLE)) break;

    Screen.clear(BACKGROUND);
    video.update();

    // Rotating, half-transparent copy in the middle.
    video.draw(160, 134, { width: 320, height: 180, angle, color: HALF_ALPHA });
    // The picture's center, cropped and zoomed, tinted red.
    video.draw(420, 20, {
        startx: video.width / 4, starty: video.height / 4,
        endx: video.width * 3 / 4, endy: video.height * 3 / 4,
        width: 200, height: 112, color: RED_TINT,
    });
    // Thumbnails through the frame Image.
    frame.draw(10, 10);
    frame.draw(10, 366);
    frame.draw(502, 366);

    angle += 0.01;
    Screen.flip();
}

video.free();
