// Soak workload (tests/soak_test.js): video. A video and its soundtrack
// playing when the VM is torn down.
const FRAMES = 90;

const font = new Font();
const video = new Video("tests/video/short.m2v");
video.audio = Sound.Stream("tests/video/short.wav");
video.loop = true;
video.play();

let frames = 0;
Loop.run(() => {
    video.update();
    video.draw(0, 0, 640, 448);
    font.print(40, 40, "Soak: video, frame " + frames);
    if (++frames === FRAMES)
        std.reload("tests/soak_test.js", { returnTo: "tests/index.js" });
});
