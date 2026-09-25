/*
 * Video module smoke and contract tests.
 *
 * Run with bin/ as the working directory (default_script=tests/video_test.js
 * in athena.ini). Fixtures live in tests/video/ and are regenerated with
 * tests/video/make_fixtures.js: short.m2v is 60 frames of 640x360 @ 60 fps.
 */

const DIR = "tests/video/";
const SHORT = DIR + "short.m2v";
const SHORT_FRAMES = 60;

let passed = 0;
let failed = 0;

function pass(name) {
    console.log("[PASS] " + name);
    passed++;
}

function fail(name, error) {
    console.log("[FAIL] " + name + ": " + error);
    failed++;
}

function test(name, callback) {
    try {
        callback();
        pass(name);
    } catch (error) {
        fail(name, error);
    }
}

function captureError(callback) {
    try {
        callback();
    } catch (error) {
        return error;
    }
    throw new Error("expected an exception");
}

function expectThrow(name, callback, type, pattern) {
    test(name, function() {
        const error = captureError(callback);
        if (type && !(error instanceof type))
            throw new Error("expected " + type.name + ", got " + error);
        if (pattern && !pattern.test(String(error.message)))
            throw new Error("unexpected message: " + error.message);
    });
}

/* Like expectThrow for InternalError, and checks the stable error.code. */
function expectCode(name, callback, code) {
    test(name, function() {
        const error = captureError(callback);
        if (!(error instanceof InternalError))
            throw new Error("expected InternalError, got " + error);
        if (error.code !== code)
            throw new Error("expected code " + code + ", got " + error.code + " (" + error + ")");
    });
}

function assert(condition, message) {
    if (!condition) throw new Error(message);
}

function assertEqual(actual, expected, what) {
    if (actual !== expected)
        throw new Error(what + ": expected " + JSON.stringify(expected) + ", got " + JSON.stringify(actual));
}

/* Renders frames until `done()` or `limit` frames; returns frames rendered. */
function run(video, limit, done) {
    let frames = 0;
    while (frames < limit && !(done && done())) {
        Screen.clear(Color.new(0, 0, 0));
        video.update();
        video.draw(0, 0, 640, 448);
        Screen.flip();
        frames++;
    }
    return frames;
}

test("Video is exported as a constructor", function() {
    assertEqual(typeof Video, "function", "typeof Video");
    const names = ["play", "pause", "stop", "update", "draw", "free"];
    for (const name of names)
        assertEqual(typeof Video.prototype[name], "function", "Video.prototype." + name);
});

expectThrow("new Video() without a path", () => new Video(), TypeError);
expectThrow("new Video(1)", () => new Video(1), TypeError);
expectThrow("new Video('')", () => new Video(""), TypeError);
expectThrow("new Video(path, extra)", () => new Video(SHORT, 1), TypeError);
expectCode("new Video(missing file)", () => new Video(DIR + "missing.m2v"), "open_failed");
expectCode("new Video(empty file)", () => new Video(DIR + "empty.m2v"), "invalid_format");
expectCode("new Video(not MPEG)", () => new Video(DIR + "garbage.bin"), "invalid_format");
expectCode("new Video(1280 wide)", () => new Video(DIR + "wide.m2v"), "unsupported_format");
expectCode("new Video(4:2:2 chroma)", () => new Video(DIR + "chroma422.m2v"), "unsupported_format");
expectCode("new Video(forbidden frame rate)", () => new Video(DIR + "badrate.m2v"), "unsupported_format");

/* Plays `path` to the end; returns frames decoded. Fails instead of hanging. */
function playToEnd(path) {
    const clip = new Video(path);
    try {
        clip.play();
        run(clip, 60 * 5, () => clip.ended);
        assertEqual(clip.ended, true, "ended within 5 s");
        return clip.currentFrame;
    } finally {
        clip.free();
    }
}

test("a stream without a sequence end code ends", function() {
    const frames = playToEnd(DIR + "noend.m2v");
    assert(frames >= SHORT_FRAMES - 2, "only " + frames + " frames");
});

test("a stream cut in the middle of a frame ends", function() {
    const frames = playToEnd(DIR + "truncated.m2v");
    assert(frames > 10 && frames < SHORT_FRAMES, "frames " + frames);
});

test("Video.probe(short.m2v)", function() {
    const info = Video.probe(SHORT);
    assertEqual(info.width, 640, "width");
    assertEqual(info.height, 360, "height");
    assertEqual(info.codedHeight, 368, "codedHeight");
    assertEqual(info.frames, SHORT_FRAMES, "frames");
    assert(info.fps > 59.9 && info.fps < 60.1, "fps " + info.fps);
    assert(Math.abs(info.duration - 1) < 0.01, "duration " + info.duration);
    assertEqual(info.mpeg2, true, "mpeg2");
    assertEqual(info.chroma, "4:2:0", "chroma");
    assertEqual(info.supported, true, "supported");
});
test("Video.probe() of unsupported streams", function() {
    const wide = Video.probe(DIR + "wide.m2v");
    assertEqual(wide.width, 1280, "wide width");
    assertEqual(wide.supported, false, "wide supported");
    assertEqual(Video.probe(DIR + "chroma422.m2v").chroma, "4:2:2", "chroma");
    assertEqual(Video.probe(DIR + "truncated.m2v").frames, 30, "truncated frames");
});
expectCode("Video.probe(not MPEG)", () => Video.probe(DIR + "garbage.bin"), "invalid_format");
expectCode("Video.probe(missing file)", () => Video.probe(DIR + "missing.m2v"), "open_failed");
expectThrow("Video.probe() without a path", () => Video.probe(), TypeError);

let video = null;
test("new Video(short.m2v) decodes the first frame", function() {
    video = new Video(SHORT);
    assert(video instanceof Video, "not a Video instance");
    assertEqual(video.ready, true, "ready");
    assertEqual(video.width, 640, "width");
    assertEqual(video.height, 360, "height (display, not the coded 368)");
    assert(video.fps > 59 && video.fps < 61, "fps " + video.fps);
    assertEqual(video.playing, false, "playing");
    assertEqual(video.ended, false, "ended");
    assertEqual(video.loop, false, "loop");
    assertEqual(video.currentFrame, 0, "currentFrame");
});

if (video) {
    expectCode("a second Video while one is open", () => new Video(SHORT), "busy");
    test("Video.probe() while a Video is open", function() {
        assertEqual(Video.probe(SHORT).frames, SHORT_FRAMES, "frames");
    });

    test("frame is a stable Image cropped to the picture", function() {
        const frame = video.frame;
        assert(frame instanceof Image, "frame is not an Image");
        assert(video.frame === frame, "frame identity changed");
        assertEqual(frame.width, 640, "frame.width");
        assertEqual(frame.height, 360, "frame.height");
        assertEqual(frame.endy, 360, "frame.endy");
        assertEqual(frame.texHeight, 368, "frame.texHeight (coded)");
    });

    /* The frame borrows the decoder's buffer; replacing it would free it. */
    expectThrow("frame.bpp = 16", () => { video.frame.bpp = 16; }, TypeError, /borrowed/);
    expectThrow("frame.texWidth = 512", () => { video.frame.texWidth = 512; }, TypeError, /borrowed/);
    expectThrow("frame.pixels = buffer", () => { video.frame.pixels = new ArrayBuffer(640 * 368 * 4); }, TypeError, /borrowed/);
    test("frame.optimize() is refused", function() {
        assertEqual(video.frame.optimize(), false, "optimize()");
    });

    test("update() does nothing while stopped", function() {
        assertEqual(video.update(), false, "update()");
        assertEqual(video.currentFrame, 0, "currentFrame");
    });

    expectThrow("update(1)", () => video.update(1), TypeError);
    expectThrow("draw() with 5 arguments", () => video.draw(0, 0, 0, 0, 0), TypeError);
    expectThrow("draw(NaN)", () => video.draw(NaN), RangeError);

    test("draw() accepts 0 to 4 arguments", function() {
        video.draw();
        video.draw(10);
        video.draw(10, 10);
        video.draw(10, 10, 320);
        video.draw(10, 10, 320, 184);
        Screen.flip();
    });

    test("draw(x, y, options)", function() {
        video.draw(0, 0, {});
        video.draw(0, 0, { width: 640, height: 448 });
        video.draw(0, 0, { startx: 160, starty: 90, endx: 480, endy: 270 });
        video.draw(320, 224, { width: 200, height: 112, angle: 0.3, color: Color.new(255, 255, 255, 64) });
        Screen.flip();
    });
    expectThrow("draw() source rectangle in the padding", () => video.draw(0, 0, { endy: 368 }), RangeError, /outside/);
    expectThrow("draw() empty source rectangle", () => video.draw(0, 0, { startx: 100, endx: 100 }), RangeError);
    expectThrow("draw() negative width", () => video.draw(0, 0, { width: -1 }), RangeError);
    expectThrow("draw() options array", () => video.draw(0, 0, [1, 2]), TypeError);

    test("play() advances frames and pause() holds them", function() {
        video.play();
        assertEqual(video.playing, true, "playing");
        run(video, 30);
        const frame = video.currentFrame;
        assert(frame > 0, "no frame decoded after 30 renders");
        video.pause();
        assertEqual(video.playing, false, "playing after pause()");
        run(video, 10);
        assertEqual(video.currentFrame, frame, "frames decoded while paused");
    });

    /* At vsync rate a 60 fps stream must not drop to 30 fps (P1). */
    test("playback keeps the stream's frame rate", function() {
        video.stop();
        video.play();
        const start = Date.now();
        run(video, 40);
        const elapsed = Date.now() - start;
        const expected = elapsed * video.fps / 1000;
        const frames = video.currentFrame;
        assert(frames >= expected * 0.85 && frames <= expected + 2,
            frames + " frames in " + elapsed + " ms, expected about " + Math.round(expected));
    });

    /*
     * Frames are decoded by a lower-priority thread while this one waits for
     * vsync. A loop that never waits must still advance (update() lends the
     * decoder up to one frame period), not freeze.
     */
    test("playback advances in a loop that never waits for vsync", function() {
        video.stop();
        video.play();
        for (let i = 0; i < 30; i++) {
            const start = Date.now();
            while (Date.now() - start < 17) { /* busy script */ }
            video.update();
        }
        assert(video.currentFrame >= 10, "only " + video.currentFrame + " frames in 30 busy updates");
    });

    test("playback ends after the last frame and calls onEnd once", function() {
        let ends = 0;
        video.onEnd = function() {
            assert(this === video, "onEnd this");
            ends++;
        };
        video.play();
        run(video, SHORT_FRAMES * 4, () => video.ended);
        assertEqual(video.ended, true, "ended");
        assertEqual(video.playing, false, "playing");
        assert(video.currentFrame >= SHORT_FRAMES - 2, "only " + video.currentFrame + " frames decoded");
        run(video, 5);
        assertEqual(ends, 1, "onEnd calls");
        video.onEnd = undefined;
    });

    test("an exception in onEnd is thrown by update()", function() {
        video.stop();
        video.onEnd = function() { throw new Error("boom"); };
        video.play();
        let caught = null;
        for (let i = 0; i < SHORT_FRAMES * 4 && !caught; i++) {
            try {
                video.update();
            } catch (error) {
                caught = error;
            }
            Screen.flip();
        }
        video.onEnd = undefined;
        assert(caught && caught.message === "boom", "caught " + caught);
        assertEqual(video.ended, true, "ended");
    });

    expectThrow("onEnd that is not a function", function() {
        video.stop();
        video.onEnd = 42;
        try {
            video.play();
            run(video, SHORT_FRAMES * 4, () => video.ended);
        } finally {
            video.onEnd = undefined;
        }
    }, TypeError, /onEnd/);

    test("play() after the end restarts", function() {
        video.play();
        assertEqual(video.ended, false, "ended");
        run(video, 20);
        assert(video.currentFrame > 0 && video.currentFrame < SHORT_FRAMES, "currentFrame " + video.currentFrame);
    });

    test("stop() rewinds", function() {
        video.stop();
        assertEqual(video.playing, false, "playing");
        assertEqual(video.currentFrame, 0, "currentFrame");
        video.play();
        run(video, 10);
        assert(video.currentFrame > 0, "no frame decoded after stop()/play()");
    });

    test("loop keeps playing past the end and calls onLoop", function() {
        const counts = [];
        video.stop();
        assertEqual(video.loopCount, 0, "loopCount after stop()");
        video.loop = true;
        assertEqual(video.loop, true, "loop");
        video.onLoop = count => counts.push(count);
        video.play();
        run(video, SHORT_FRAMES * 3);
        assertEqual(video.ended, false, "ended while looping");
        assertEqual(video.playing, true, "playing while looping");
        assert(video.loopCount >= 2, "loopCount " + video.loopCount);
        assertEqual(counts.join(","), Array.from({ length: video.loopCount }, (_, i) => i + 1).join(","),
            "onLoop counts");
        video.onLoop = undefined;
        video.loop = false;
    });

    let freedFrame = null;
    test("free() releases the video and can be repeated", function() {
        freedFrame = video.frame;
        video.free();
        video.free();
    });

    /* The frame Image no longer points at the video's surface. */
    expectThrow("frame.draw() after free()", () => freedFrame.draw(0, 0), InternalError, /not loaded/);
    test("frame.ready() after free()", function() {
        assertEqual(freedFrame.ready(), false, "ready()");
    });

    expectThrow("play() after free()", () => video.play(), TypeError, /freed/);
    expectThrow("ended after free()", () => video.ended, TypeError, /freed/);
    expectThrow("frame after free()", () => video.frame, TypeError, /freed/);

    test("a new Video can be opened after free()", function() {
        const again = new Video(SHORT);
        assertEqual(again.ready, true, "ready");
        again.free();
    });
}

/*
 * Every Video starts a decoder thread and allocates ~4 MB. The EE kernel has
 * 256 thread slots and IDs grow as threads come and go, so leaked threads
 * would make the constructor fail with thread_failed well before 300 cycles.
 */
test("300 open/free cycles leak no thread or memory", function() {
    const before = System.getUsedMemory();
    for (let i = 0; i < 300; i++) {
        const cycle = new Video(SHORT);
        cycle.free();
    }
    const grown = System.getUsedMemory() - before;
    console.log("  memory after 300 cycles: " + (grown >= 0 ? "+" : "") + grown + " bytes");
    assert(grown < 256 * 1024, "used memory grew by " + grown + " bytes");
});

/* --- Audio sync: video.audio = Sound.Stream ------------------------------- */

const SHORT_WAV = DIR + "short.wav";   // 1 s, the length of short.m2v
const FRAME_MS = 1000 / 60;

if (typeof Sound === "undefined") {
    console.log("[SKIP] audio sync: the Sound module is not in this build");
} else {
    const av = new Video(SHORT);
    const audio = Sound.Stream(SHORT_WAV);

    expectThrow("video.audio = 42", () => { av.audio = 42; }, TypeError, /Sound\.Stream/);
    expectThrow("video.audio = {}", () => { av.audio = {}; }, TypeError, /Sound\.Stream/);

    test("video.audio = stream", function() {
        assertEqual(av.audio, null, "audio before");
        av.loop = false;
        audio.loop = true;
        av.audio = audio;
        assert(av.audio === audio, "audio identity");
        assertEqual(audio.loop, false, "video.loop copied to the stream");
    });

    test("play() starts the audio and the video follows it", function() {
        av.play();
        assertEqual(audio.playing(), true, "audio playing");
        let worst = 0;
        let frames = 0;
        while (!av.ended && frames < 60 * 5) {
            Screen.clear(Color.new(0, 0, 0));
            av.update();
            av.draw(0, 0, 640, 360);
            Screen.flip();
            frames++;
            /* Once running: the picture shown vs the audio heard. */
            if (frames > 15 && !audio.ended && av.currentFrame < SHORT_FRAMES - 1)
                worst = Math.max(worst, Math.abs(av.currentFrame * FRAME_MS - audio.position));
        }
        console.log("  worst A/V offset: " + worst.toFixed(1) + " ms");
        assertEqual(av.ended, true, "ended");
        assert(av.currentFrame >= SHORT_FRAMES - 2, "only " + av.currentFrame + " pictures");
        assert(worst <= 4 * FRAME_MS, "A/V offset " + worst.toFixed(1) + " ms");
    });

    expectThrow("video.audio cannot change while playing", function() {
        av.play();
        try {
            av.audio = null;
        } finally {
            av.stop();
        }
    }, TypeError, /not playing/);

    test("pause() and play() pause and resume both", function() {
        av.play();
        run(av, 20);
        av.pause();
        assertEqual(audio.playing(), false, "audio playing after pause()");
        const frame = av.currentFrame;
        const heard = audio.position;
        run(av, 10);
        assertEqual(av.currentFrame, frame, "video moved while paused");
        assertEqual(audio.position, heard, "audio moved while paused");
        av.play();
        assertEqual(audio.playing(), true, "audio after resume");
        run(av, 10);
        assert(av.currentFrame > frame, "video did not resume");
    });

    test("stop() rewinds both", function() {
        av.stop();
        assertEqual(av.currentFrame, 0, "video frame");
        assertEqual(audio.playing(), false, "audio playing");
        assertEqual(audio.position, 0, "audio position");
    });

    test("loop: the video restarts with the audio", function() {
        const loops = [];
        av.loop = true;
        assertEqual(audio.loop, true, "video.loop copied to the stream");
        av.onLoop = count => loops.push(count);
        av.play();
        run(av, 60 * 3);
        assertEqual(av.playing, true, "playing");
        assertEqual(av.ended, false, "ended while looping");
        assert(av.loopCount >= 2, "loopCount " + av.loopCount);
        assertEqual(loops.join(","), Array.from({ length: av.loopCount }, (_, i) => i + 1).join(","),
            "onLoop counts");
        av.onLoop = undefined;
        av.stop();
        av.loop = false;
    });

    test("video.audio = null goes back to the EE clock", function() {
        av.audio = null;
        assertEqual(av.audio, null, "audio");
        av.play();
        assertEqual(audio.playing(), false, "detached audio started");
        run(av, 20);
        assert(av.currentFrame > 10, "currentFrame " + av.currentFrame);
        av.stop();
    });

    av.free();
    audio.free();
}

console.log("Result: " + passed + " passed, " + failed + " failed");
if (failed !== 0) throw new Error("Video tests failed");
