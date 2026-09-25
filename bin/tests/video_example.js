// Video playback example: plays an MPEG-2 stream with its soundtrack and
// on-screen status.
//
//   CROSS     play / pause
//   CIRCLE    stop (rewind)
//   SQUARE    toggle loop
//   TRIANGLE  quit
//
// Point PATH/AUDIO at your own files (see docs/VIDEO.md for the ffmpeg
// commands); set AUDIO to null for a silent video.

const PATH = "tests/video/short.m2v";
const AUDIO = "tests/video/short.wav";

const info = Video.probe(PATH);
console.log("Video: " + info.width + "x" + info.height + " @ " + info.fps.toFixed(2) +
    " fps, " + info.frames + " frames (" + info.duration.toFixed(1) + " s)");
if (!info.supported)
    throw new Error(PATH + " cannot be played (" + info.chroma + ", " + info.codedWidth + "x" + info.codedHeight + ")");

const font = new Font();
const pad = Gamepad.player(0);
const BLACK = Color.new(0, 0, 0);
const video = new Video(PATH);
const audio = AUDIO ? Sound.Stream(AUDIO) : null;

// The picture follows the audio heard; play/pause/stop/loop drive both.
video.audio = audio;

let status = "";
video.onEnd = () => { status = "ended: CROSS plays again"; };
video.onLoop = count => { status = "looped " + count + "x"; };
video.play();

// Scale to the screen width, keeping the aspect ratio, centered vertically.
const width = 640;
const height = Math.round(video.height * width / video.width);
const top = Math.max(0, (448 - height) >> 1);

let quit = false;
while (!quit) {
    Gamepad.update();
    if (pad.justPressed(Gamepad.CROSS)) {
        if (video.playing) video.pause();
        else video.play();
    }
    if (pad.justPressed(Gamepad.CIRCLE)) video.stop();
    if (pad.justPressed(Gamepad.SQUARE)) video.loop = !video.loop;
    if (pad.justPressed(Gamepad.TRIANGLE)) quit = true;

    Screen.clear(BLACK);
    video.update();
    video.draw(0, top, width, height);
    font.print(10, 10, "frame " + video.currentFrame + "/" + info.frames +
        (audio ? "  audio " + Math.round(audio.position) + " ms" : "") +
        (video.loop ? "  loop" : "") + (video.playing ? "" : "  paused"));
    if (status) font.print(10, 30, status);
    Screen.flip();
}

video.free();
if (audio) audio.free();
console.log("Video example finished");
