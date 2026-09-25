/*
 * Sound module smoke and contract tests.
 *
 * Run with bin/ as the working directory (default_script=tests/sound_test.js
 * in athena.ini, without `audsrv = true` so the lazy IOP load is checked).
 * Fixtures live in tests/sound/ and are regenerated with
 * tests/sound/make_fixtures.js (bg.wav, pop.adp, over.adp and music.ogg are
 * not generated).
 *
 * Timing checks use wide margins: under PCSX2 the audio clock follows the
 * emulated SPU2, which may run slightly off real time.
 */

const DIR = "tests/sound/";
const WAV = DIR + "bg.wav";
const ADP = DIR + "pop.adp";

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

function skip(name, reason) {
    console.log("[SKIP] " + name + ": " + reason);
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

/* Like expectThrow, and checks the stable error.code. */
function expectCode(name, callback, type, code) {
    test(name, function() {
        const error = captureError(callback);
        if (type && !(error instanceof type))
            throw new Error("expected " + type.name + ", got " + error);
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

function assertNear(actual, expected, tolerance, what) {
    if (Math.abs(actual - expected) > tolerance)
        throw new Error(what + ": expected " + expected + " +/- " + tolerance + ", got " + actual);
}

/* Blocks the script thread, like a frame loop waiting for vsync. */
function wait(ms) {
    System.sleep(ms);
}

function waitUntil(condition, timeoutMs) {
    for (let elapsed = 0; elapsed < timeoutMs; elapsed += 20) {
        if (condition()) return true;
        wait(20);
    }
    return condition();
}

console.log("=== Sound tests ===");

/* ------------------------------------------------------------------ */
/* Module shape                                                         */
/* ------------------------------------------------------------------ */

test("Sound is global", function() {
    assertEqual(typeof Sound, "object", "typeof Sound");
    assertEqual(typeof Sound.Stream, "function", "typeof Sound.Stream");
    assertEqual(typeof Sound.Sfx, "function", "typeof Sound.Sfx");
    assertEqual(typeof Sound.setVolume, "function", "typeof Sound.setVolume");
    assertEqual(typeof Sound.getVolume, "function", "typeof Sound.getVolume");
    assertEqual(typeof Sound.findChannel, "function", "typeof Sound.findChannel");
    assertEqual(Sound.CHANNELS, 24, "Sound.CHANNELS");
});

test("import from 'Sound' matches the global", function() {
    // The bootstrap sets globalThis.Sound to the module namespace.
    assert(Object.keys(Sound).indexOf("Stream") >= 0, "Stream export");
    assert(Object.keys(Sound).indexOf("Sfx") >= 0, "Sfx export");
});

/* ------------------------------------------------------------------ */
/* Lazy IOP loading                                                     */
/* ------------------------------------------------------------------ */

const audsrvBefore = IOP.getModule("audsrv");
test("audsrv and libsd are registered", function() {
    assert(audsrvBefore, "audsrv not registered");
    assert(IOP.getModule("libsd"), "libsd not registered");
});

if (audsrvBefore && audsrvBefore.started) {
    skip("audsrv is not loaded before first use", "athena.ini has audsrv = true");
} else {
    test("audsrv is not loaded before first use", function() {
        assertEqual(audsrvBefore.started, false, "audsrv.started");
        // Volume changes are stored without touching the IOP.
        Sound.setVolume(100);
        assertEqual(IOP.getModule("audsrv").started, false, "audsrv.started after setVolume");
    });
}

test("first use loads audsrv", function() {
    const channel = Sound.findChannel();
    assert(channel >= 0 && channel < 24, "findChannel returned " + channel);
    assertEqual(IOP.getModule("audsrv").started, true, "audsrv.started");
    assertEqual(IOP.getModule("libsd").started, true, "libsd.started");
});

/* ------------------------------------------------------------------ */
/* Module functions                                                     */
/* ------------------------------------------------------------------ */

test("setVolume/getVolume", function() {
    Sound.setVolume(40);
    assertEqual(Sound.getVolume(), 40, "volume");
    Sound.setVolume(0);
    assertEqual(Sound.getVolume(), 0, "volume");
    Sound.setVolume(100);
    assertEqual(Sound.getVolume(), 100, "volume");
});
expectThrow("setVolume() without arguments", () => Sound.setVolume(), TypeError);
expectThrow("setVolume with extra arguments", () => Sound.setVolume(1, 2), TypeError);
expectThrow("setVolume('50')", () => Sound.setVolume("50"), TypeError);
expectThrow("setVolume(101)", () => Sound.setVolume(101), RangeError);
expectThrow("setVolume(-1)", () => Sound.setVolume(-1), RangeError);
expectThrow("setVolume(50.5)", () => Sound.setVolume(50.5), RangeError);
expectThrow("setVolume(NaN)", () => Sound.setVolume(NaN), RangeError);
expectThrow("findChannel with arguments", () => Sound.findChannel(1), TypeError);
test("setVolume keeps the last valid value", function() {
    assertEqual(Sound.getVolume(), 100, "volume");
});

/* ------------------------------------------------------------------ */
/* Sfx                                                                  */
/* ------------------------------------------------------------------ */

let pop = null;
test("new Sound.Sfx(adp)", function() {
    pop = new Sound.Sfx(ADP);
    assert(pop instanceof Sound.Sfx, "instanceof");
    assert(pop.rate > 0, "rate " + pop.rate);
    assert(pop.length > 50 && pop.length < 1000, "length " + pop.length);
    assertEqual(pop.loop, false, "loop");
    assertEqual(pop.pitch, 0, "pitch");
    assertEqual(pop.volume, 100, "volume");
    assertEqual(pop.pan, 0, "pan");
});

test("Sound.Sfx(adp) without new", function() {
    const sfx = Sound.Sfx(ADP);
    assert(sfx instanceof Sound.Sfx, "instanceof");
    sfx.free();
});

test("length is not truncated to whole seconds", function() {
    // 0.1 s <= length < 1 s: the old API returned 0 here.
    assert(pop.length % 1000 !== 0, "length " + pop.length);
});

expectThrow("Sfx() without path", () => new Sound.Sfx(), TypeError);
expectThrow("Sfx(123)", () => new Sound.Sfx(123), TypeError);
expectThrow("Sfx(path, extra)", () => new Sound.Sfx(ADP, 1), TypeError);
expectThrow("Sfx(missing file)", () => new Sound.Sfx(DIR + "missing.adp"), InternalError, /cannot open file/);
expectThrow("Sfx(bad magic)", () => new Sound.Sfx(DIR + "bad.adp"), InternalError, /unsupported audio format/);
expectThrow("Sfx(text file)", () => new Sound.Sfx(DIR + "garbage.bin"), InternalError, /unsupported audio format/);
expectThrow("Sfx(wav file)", () => new Sound.Sfx(DIR + "short.wav"), InternalError, /unsupported audio format/);

/* Stable error codes. */
expectCode("code NOT_FOUND", () => new Sound.Sfx(DIR + "missing.adp"), InternalError, "NOT_FOUND");
expectCode("code BAD_FORMAT", () => new Sound.Sfx(DIR + "bad.adp"), InternalError, "BAD_FORMAT");
expectCode("code INVALID_ARGUMENT (type)", () => new Sound.Sfx(123), TypeError, "INVALID_ARGUMENT");
expectCode("code INVALID_ARGUMENT (range)", () => Sound.setVolume(101), RangeError, "INVALID_ARGUMENT");
expectCode("code UNSUPPORTED", () => { pop.pitch = 1; }, TypeError, "UNSUPPORTED");

/* ADPCM data is checked before it is uploaded. */
expectCode("Sfx(truncated) is refused", () => new Sound.Sfx(DIR + "truncated.adp"), InternalError, "CORRUPT");
expectCode("Sfx(no end flag) is refused", () => new Sound.Sfx(DIR + "noend.adp"), InternalError, "CORRUPT");
expectThrow("corrupt message explains why", () => new Sound.Sfx(DIR + "noend.adp"), InternalError, /end flag/);

test("Sfx(loop.adp) reads the loop flag", function() {
    const sfx = new Sound.Sfx(DIR + "loop.adp");
    assertEqual(sfx.loop, true, "loop");
    assertNear(sfx.length, 200, 5, "length");
    sfx.free();
});

/* SPU2 memory accounting. */
test("getMemoryStats follows loads and frees", function() {
    const before = Sound.getMemoryStats();
    assertEqual(before.total, 2 * 1024 * 1024 - 0x5010, "total");
    assertEqual(before.used + before.free, before.total, "used + free");
    const a = new Sound.Sfx(DIR + "over.adp");
    const b = new Sound.Sfx(ADP);
    const loaded = Sound.getMemoryStats();
    assertEqual(loaded.samples, before.samples + 2, "samples");
    assertEqual(loaded.used, before.used + 23744 + 3408, "used");
    // Freed before b: its memory stays reserved until b goes too.
    a.free();
    const hole = Sound.getMemoryStats();
    assertEqual(hole.used, loaded.used, "used with a hole");
    assertEqual(hole.wasted, before.wasted + 23744, "wasted");
    b.free();
    const after = Sound.getMemoryStats();
    assertEqual(after.used, before.used, "used after both");
    assertEqual(after.wasted, before.wasted, "wasted after both");
});

test("SPU2 memory exhaustion is reported before the upload", function() {
    const loaded = [];
    let error = null;
    try {
        for (let i = 0; i < 100 && !error; i++) {
            try {
                loaded.push(new Sound.Sfx(DIR + "over.adp"));
            } catch (e) {
                error = e;
            }
        }
        assert(error, "100 x 23 KB fitted in SPU2 memory");
        assertEqual(error.code, "SPU_MEMORY", "code");
        assert(/free/.test(error.message), "message: " + error.message);
        assert(Sound.getMemoryStats().free < 23744, "free " + Sound.getMemoryStats().free);
    } finally {
        loaded.forEach((sfx) => sfx.free());
    }
    // Everything fits again once freed.
    new Sound.Sfx(DIR + "over.adp").free();
});

/* Master volume for every sound effect. */
test("setSfxVolume/getSfxVolume", function() {
    assertEqual(Sound.getSfxVolume(), 100, "default");
    Sound.setSfxVolume(25);
    assertEqual(Sound.getSfxVolume(), 25, "volume");
    // The per-sample volume is not changed by the master volume.
    assertEqual(pop.volume, 100, "pop.volume");
    Sound.setSfxVolume(100);
});
expectCode("setSfxVolume(101)", () => Sound.setSfxVolume(101), RangeError, "INVALID_ARGUMENT");
expectThrow("setSfxVolume('1')", () => Sound.setSfxVolume("1"), TypeError);

test("Sfx volume and pan", function() {
    pop.volume = 30;
    pop.pan = -100;
    assertEqual(pop.volume, 30, "volume");
    assertEqual(pop.pan, -100, "pan");
    pop.pan = 100;
    assertEqual(pop.pan, 100, "pan");
    pop.volume = 100;
    pop.pan = 0;
});
expectThrow("Sfx.volume = 101", () => { pop.volume = 101; }, RangeError);
expectThrow("Sfx.volume = -1", () => { pop.volume = -1; }, RangeError);
expectThrow("Sfx.volume = 'x'", () => { pop.volume = "x"; }, TypeError);
expectThrow("Sfx.pan = 101", () => { pop.pan = 101; }, RangeError);
expectThrow("Sfx.pan = -101", () => { pop.pan = -101; }, RangeError);
expectThrow("Sfx.loop is read-only", () => { pop.loop = true; }, TypeError, /read-only/);
expectThrow("Sfx.pitch is not supported", () => { pop.pitch = 10; }, TypeError, /not supported/);

test("Sfx.play() on a free channel", function() {
    const channel = pop.play();
    assert(channel >= 0 && channel < 24, "channel " + channel);
    assertEqual(pop.playing(channel), true, "playing right after play");
    assert(waitUntil(() => !pop.playing(channel), pop.length + 500), "still playing after its length");
});

test("Sfx.play(channel) uses that channel", function() {
    assertEqual(pop.play(7), 7, "channel");
    assertEqual(pop.playing(7), true, "playing(7)");
    // Busy channel: reported, not silently accepted.
    assertEqual(pop.play(7), -1, "busy channel");
    assert(waitUntil(() => !pop.playing(7), pop.length + 500), "channel 7 never ended");
    assertEqual(pop.play(undefined) >= 0, true, "play(undefined) picks a channel");
    waitUntil(() => Sound.findChannel() === 0, pop.length + 500);
});

expectThrow("Sfx.play(24)", () => pop.play(24), RangeError);
expectThrow("Sfx.play(-1)", () => pop.play(-1), RangeError);
expectThrow("Sfx.play(1.5)", () => pop.play(1.5), RangeError);
expectThrow("Sfx.play('1')", () => pop.play("1"), TypeError);
expectThrow("Sfx.play(1, 2)", () => pop.play(1, 2), TypeError);
expectThrow("Sfx.playing()", () => pop.playing(), TypeError);
expectThrow("Sfx.playing(24)", () => pop.playing(24), RangeError);
expectThrow("Sfx method on another object", () => Sound.Sfx.prototype.play.call({}), TypeError);

test("all 24 channels busy returns -1", function() {
    const channels = [];
    let refused = 0;
    for (let i = 0; i < 26; i++) {
        const channel = pop.play();
        if (channel < 0) refused++;
        else channels.push(channel);
    }
    const unique = channels.filter((c, i) => channels.indexOf(c) === i);
    assertEqual(unique.length, channels.length, "channels reused while busy");
    assert(channels.length <= 24, "more than 24 channels: " + channels.length);
    assert(refused >= 2, "refused " + refused);
    assertEqual(Sound.findChannel(), -1, "findChannel while all busy");
    assert(waitUntil(() => Sound.findChannel() >= 0, pop.length + 1000), "no channel freed");
});

test("Sfx.free() releases SPU2 memory", function() {
    // ~2.3 MiB in total: more than SPU2 RAM, so it only fits if free() works.
    for (let i = 0; i < 100; i++) {
        const sfx = new Sound.Sfx(DIR + "over.adp");
        sfx.free();
    }
});

test("Sfx garbage collection releases SPU2 memory", function() {
    for (let i = 0; i < 100; i++) {
        let sfx = new Sound.Sfx(DIR + "over.adp");
        sfx = null;
        std.gc();
    }
});

test("Sfx use after free throws", function() {
    const sfx = new Sound.Sfx(ADP);
    sfx.free();
    const error = captureError(() => sfx.play());
    assert(error instanceof TypeError, "play: " + error);
    assert(captureError(() => sfx.free()) instanceof TypeError, "double free");
    assert(captureError(() => sfx.volume) instanceof TypeError, "volume getter");
});

/* ------------------------------------------------------------------ */
/* Stream: WAV                                                          */
/* ------------------------------------------------------------------ */

let wav = null;
test("Sound.Stream(wav)", function() {
    wav = Sound.Stream(WAV);
    assert(wav instanceof Sound.Stream, "instanceof");
    assertEqual(wav.format, "wav", "format");
    assertEqual(wav.rate, 44100, "rate");
    assertEqual(wav.channels, 2, "channels");
    // 6792126 bytes - 44 header bytes at 176400 bytes/s.
    assertNear(wav.length, 38503, 5, "length");
    assertEqual(wav.position, 0, "position");
    assertEqual(wav.playing(), false, "playing before play()");
    assertEqual(wav.loop, false, "loop");
});

test("new Sound.Stream(wav with LIST chunk)", function() {
    const short = new Sound.Stream(DIR + "short.wav");
    assertEqual(short.rate, 22050, "rate");
    assertEqual(short.channels, 1, "channels");
    assertNear(short.length, 500, 1, "length");
    short.free();
});

expectThrow("Stream() without path", () => Sound.Stream(), TypeError);
expectThrow("Stream({})", () => Sound.Stream({}), TypeError);
expectThrow("Stream(missing)", () => Sound.Stream(DIR + "missing.ogg"), InternalError, /cannot open file/);
expectCode("Stream(mu-law wav)", () => Sound.Stream(DIR + "mulaw.wav"), InternalError, "BAD_FORMAT");
expectThrow("Stream(6 channels)", () => Sound.Stream(DIR + "surround.wav"), InternalError, /only mono and stereo/);
expectCode("Stream(missing) code", () => Sound.Stream(DIR + "missing.ogg"), InternalError, "NOT_FOUND");

/* Formats audsrv cannot play directly are converted on the EE. */
test("Stream(16 kHz wav) is converted", function() {
    const s = new Sound.Stream(DIR + "rate16k.wav");
    assertEqual(s.rate, 16000, "rate");
    assertEqual(s.converted, true, "converted");
    assertNear(s.length, 2000, 1, "length");
    s.free();
});
test("Stream(float wav) is converted", function() {
    const s = new Sound.Stream(DIR + "float.wav");
    assertEqual(s.rate, 44100, "rate");
    assertEqual(s.channels, 2, "channels");
    assertEqual(s.converted, true, "converted");
    assertNear(s.length, 1000, 1, "length");
    s.free();
});
test("Stream(8-bit stereo 22050 Hz) is converted", function() {
    const s = new Sound.Stream(DIR + "stereo8.wav");
    assertEqual(s.converted, true, "converted");
    s.free();
});
test("Stream(22050 Hz mono 16-bit) is not converted", function() {
    const s = new Sound.Stream(DIR + "short.wav");
    assertEqual(s.converted, false, "converted");
    s.free();
});
expectThrow("Stream(wav without data)", () => Sound.Stream(DIR + "nodata.wav"), InternalError, /unsupported audio format/);
expectThrow("Stream(text file)", () => Sound.Stream(DIR + "garbage.bin"), InternalError, /unsupported audio format/);
expectThrow("Stream(adp)", () => Sound.Stream(ADP), InternalError, /unsupported audio format/);

test("Stream plays and the position advances", function() {
    wav.play();
    assertEqual(wav.playing(), true, "playing");
    wait(1000);
    const position = wav.position;
    assertNear(position, 1000, 400, "position after 1 s");
});

test("Stream.pause keeps the heard position", function() {
    wav.pause();
    assertEqual(wav.playing(), false, "playing after pause");
    const paused = wav.position;
    wait(500);
    assertEqual(wav.position, paused, "position moved while paused");
    wav.play();
    assertEqual(wav.playing(), true, "playing after resume");
    wait(600);
    assert(wav.position >= paused, "went back: " + paused + " -> " + wav.position);
    assertNear(wav.position, paused + 500, 400, "position after resume");
});

test("Stream.position seeks", function() {
    wav.position = 20000;
    assertNear(wav.position, 20000, 50, "position right after seek");
    assertEqual(wav.playing(), true, "still playing after seek");
    wait(700);
    assertNear(wav.position, 20600, 450, "position after seek");
    // Relative seeks past the start clamp, as in the old sound.js sample.
    wav.position = wav.position - 60000;
    assertNear(wav.position, 0, 150, "clamped at 0");
});
expectThrow("Stream.position = 'x'", () => { wav.position = "x"; }, TypeError);
expectThrow("Stream.position = NaN", () => { wav.position = NaN; }, RangeError);

test("Stream ends at its length", function() {
    wav.position = wav.length - 300;
    assert(waitUntil(() => !wav.playing(), 2000), "never ended");
    assertEqual(wav.position, 0, "position after the end");
    // Playing again starts over.
    wav.play();
    wait(300);
    assert(wav.position > 0 && wav.position < 1000, "restart position " + wav.position);
});

test("Stream.rewind and stop", function() {
    wait(500);
    wav.rewind();
    assertEqual(wav.playing(), true, "rewind keeps playing");
    assertNear(wav.position, 0, 150, "position after rewind");
    wait(400);
    wav.stop();
    assertEqual(wav.playing(), false, "playing after stop");
    assertEqual(wav.position, 0, "position after stop");
});

test("Stream.loop", function() {
    const short = new Sound.Stream(DIR + "short.wav");
    short.loop = true;
    assertEqual(short.loop, true, "loop");
    short.play();
    wait(1300);
    assertEqual(short.playing(), true, "stopped while looping");
    assert(short.position < short.length, "position " + short.position);
    short.loop = false;
    assert(waitUntil(() => !short.playing(), 1500), "never ended after loop = false");
    short.free();
});

/* ------------------------------------------------------------------ */
/* Stream: converted formats                                            */
/* ------------------------------------------------------------------ */

test("converted streams play and advance", function() {
    ["rate16k.wav", "stereo8.wav", "float.wav"].forEach(function(name) {
        const s = new Sound.Stream(DIR + name);
        s.play();
        wait(500);
        assertEqual(s.playing(), true, name + " playing");
        assertNear(s.position, 500, 300, name + " position");
        s.free();
    });
});

test("converted stream seeks, pauses and ends", function() {
    const s = new Sound.Stream(DIR + "rate16k.wav");
    s.play();
    wait(200);
    s.position = 1000;
    assertNear(s.position, 1000, 60, "right after seek");
    wait(300);
    assertNear(s.position, 1300, 300, "300 ms after seek");
    s.pause();
    const paused = s.position;
    wait(200);
    assertEqual(s.position, paused, "moved while paused");
    s.play();
    assert(waitUntil(() => !s.playing(), 2000), "never ended");
    assertEqual(s.ended, true, "ended");
    assertEqual(s.position, 0, "position after the end");
    s.free();
});

/* ------------------------------------------------------------------ */
/* Stream: events                                                       */
/* ------------------------------------------------------------------ */

test("ended and onEnd through Sound.process()", function() {
    const s = new Sound.Stream(DIR + "short.wav");
    let calls = 0;
    let self = null;
    s.onEnd = function() { calls++; self = this; };
    assertEqual(s.ended, false, "ended before play");
    s.play();
    Sound.process();
    assertEqual(calls, 0, "onEnd before the end");
    assert(waitUntil(() => s.ended, 1500), "never ended");
    assert(Sound.process() >= 1, "process() ran no callback");
    assertEqual(calls, 1, "onEnd calls");
    assert(self === s, "this is not the stream");
    Sound.process();
    assertEqual(calls, 1, "onEnd ran twice");
    s.play();
    assertEqual(s.ended, false, "ended after play()");
    s.free();
});

test("onLoop fires while looping, onEnd does not", function() {
    const s = new Sound.Stream(DIR + "short.wav");
    let loops = 0;
    let ends = 0;
    s.loop = true;
    s.onLoop = () => loops++;
    s.onEnd = () => ends++;
    s.play();
    wait(1300);
    Sound.process();
    assert(loops >= 1, "loops " + loops);
    assertEqual(ends, 0, "ends while looping");
    assertEqual(s.ended, false, "ended while looping");
    s.free();
});

expectCode("onEnd must be a function", () => { wav.onEnd = 1; }, TypeError, "INVALID_ARGUMENT");
test("onEnd = null clears it", function() {
    wav.onEnd = () => {};
    wav.onEnd = null;
    assertEqual(wav.onEnd, null, "onEnd");
});

test("a callback exception propagates from Sound.process()", function() {
    const s = new Sound.Stream(DIR + "short.wav");
    s.onEnd = () => { throw new Error("boom"); };
    s.position = s.length - 100;
    s.play();
    assert(waitUntil(() => s.ended, 1500), "never ended");
    const error = captureError(() => Sound.process());
    assertEqual(error.message, "boom", "message");
    s.free();
});

test("streams whose callback captures them are collected", function() {
    // Each stream holds an open file: leaked cycles would run out of
    // descriptors long before 64 and the constructor would throw.
    for (let i = 0; i < 64; i++) {
        (function() {
            const s = new Sound.Stream(DIR + "short.wav");
            s.onEnd = function() { s.play(); };
        })();
        std.gc();
    }
    assertEqual(Sound.process(), 0, "callbacks of collected streams");
});

/* ------------------------------------------------------------------ */
/* Stream: fades                                                        */
/* ------------------------------------------------------------------ */

test("play({fade}) fades in", function() {
    wav.position = 5000;
    wav.play({ fade: 400 });
    assertEqual(wav.playing(), true, "playing");
    wait(600);
    assertNear(wav.position, 5600, 400, "position");
});

test("pause({fade}) plays until the fade ends", function() {
    wav.pause({ fade: 400 });
    assertEqual(wav.playing(), true, "playing during the fade");
    assert(waitUntil(() => !wav.playing(), 1500), "never paused");
    const paused = wav.position;
    wait(300);
    assertEqual(wav.position, paused, "moved after the fade-out");
    assert(paused > 5600, "position " + paused);
});

test("play() during a fade-out cancels it", function() {
    wav.play();
    wav.pause({ fade: 600 });
    wait(100);
    wav.play({ fade: 100 });
    wait(900);
    assertEqual(wav.playing(), true, "paused anyway");
});

test("stop({fade}) rewinds after the fade", function() {
    wav.stop({ fade: 300 });
    assertEqual(wav.playing(), true, "playing during the fade");
    assert(waitUntil(() => !wav.playing(), 1500), "never stopped");
    assertEqual(wav.position, 0, "position");
});

expectCode("play({fade: -1})", () => wav.play({ fade: -1 }), RangeError, "INVALID_ARGUMENT");
expectCode("play({fade: 'x'})", () => wav.play({ fade: "x" }), TypeError, "INVALID_ARGUMENT");
expectThrow("pause('x')", () => wav.pause("x"), TypeError);

test("setVolume while a stream plays", function() {
    wav.play();
    Sound.setVolume(20);
    wait(200);
    Sound.setVolume(100);
    wait(200);
    assertEqual(wav.playing(), true, "playing");
    wav.pause();
});

/* ------------------------------------------------------------------ */
/* Stream: OGG and switching                                            */
/* ------------------------------------------------------------------ */

let ogg = null;
test("Sound.Stream(ogg)", function() {
    ogg = new Sound.Stream(DIR + "music.ogg");
    assertEqual(ogg.format, "ogg", "format");
    assertEqual(ogg.rate, 24000, "rate");
    assertEqual(ogg.channels, 2, "channels");
    assert(ogg.length > 60000 && ogg.length < 200000, "length " + ogg.length);
});

test("OGG plays, seeks and pauses", function() {
    ogg.play();
    wait(800);
    assertNear(ogg.position, 800, 400, "position");
    ogg.position = 30000;
    wait(500);
    assertNear(ogg.position, 30400, 450, "position after seek");
    ogg.pause();
    const paused = ogg.position;
    wait(300);
    assertEqual(ogg.position, paused, "moved while paused");
});

test("playing another stream replaces the current one", function() {
    wav.play();
    ogg.play();
    assertEqual(wav.playing(), false, "wav still playing");
    assertEqual(ogg.playing(), true, "ogg not playing");
    const wavPosition = wav.position;
    wait(400);
    assertEqual(wav.position, wavPosition, "replaced stream kept moving");
});

test("sound effects play over a stream", function() {
    assertEqual(ogg.playing(), true, "ogg playing");
    const channel = pop.play();
    assert(channel >= 0, "channel " + channel);
    wait(300);
    assertNear(ogg.position, 30400 + 400, 900, "stream stalled by sfx");
});

test("free() of the playing stream stops it", function() {
    ogg.free();
    assert(captureError(() => ogg.play()) instanceof TypeError, "use after free");
    assert(captureError(() => ogg.position) instanceof TypeError, "getter after free");
    assert(captureError(() => ogg.free()) instanceof TypeError, "double free");
    wav.play();
    wait(300);
    assertEqual(wav.playing(), true, "next stream plays");
    wav.pause();
});

test("streams collected while playing", function() {
    for (let i = 0; i < 5; i++) {
        let stream = new Sound.Stream(DIR + "short.wav");
        stream.play();
        wait(50);
        stream = null;
        std.gc();
    }
    wav.play();
    wait(200);
    assertEqual(wav.playing(), true, "wav after collected streams");
    wav.stop();
});

expectThrow("Stream method on another object", () => Sound.Stream.prototype.play.call({}), TypeError);
expectThrow("Stream.play(1)", () => wav.play(1), TypeError);

/* Loops: the SPU2 flags them as ended after the first pass (200 ms here). */
test("a looping sample plays until stopped", function() {
    const sfx = new Sound.Sfx(DIR + "loop.adp");
    assertEqual(sfx.play(23), 23, "channel");
    wait(500);
    assertEqual(sfx.playing(23), true, "playing after two passes");
    assertEqual(pop.play(23), -1, "its channel is busy");
    sfx.stop(23);
    assertEqual(sfx.playing(23), false, "playing after stop()");
    // The muted loop gives way to the next sample played on its channel.
    assertEqual(pop.play(23), 23, "channel reused after stop()");
    assertEqual(pop.playing(23), true, "pop on 23");
    sfx.free();
});

test("free() of a looping sample releases its channel", function() {
    const sfx = new Sound.Sfx(DIR + "loop.adp");
    const channel = sfx.play();
    assert(channel >= 0, "channel " + channel);
    wait(500);
    sfx.free();
    assertEqual(pop.play(channel), channel, "channel reused after free()");
});

test("Sfx.stop() of a one-shot sample", function() {
    const channel = pop.play();
    assert(channel >= 0, "channel " + channel);
    pop.stop();
    assertEqual(pop.playing(channel), false, "playing after stop()");
});
expectThrow("Sfx.stop(24)", () => pop.stop(24), RangeError);
expectThrow("Sfx.stop('1')", () => pop.stop("1"), TypeError);

if (wav) wav.free();
if (pop) pop.free();

console.log("Result: " + passed + " passed, " + failed + " failed");
if (failed !== 0) throw new Error("Sound tests failed");
