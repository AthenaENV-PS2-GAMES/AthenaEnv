/*
 * Sound across IOP.reset(): samples reload from their files on the next
 * play(), streams reopen their file and resume, settings survive.
 *
 * Kept apart from sound_test.js: IOP.reset() unloads every driver, file
 * I/O included, so this script reloads the boot device's driver itself,
 * like the boot sequence does (src/core/boot.c). Works from host: (PCSX2),
 * mass: (USB), mc: and cdfs:. Run with default_script=tests/sound_reset_test.js.
 */

const DIR = "tests/sound/";

let passed = 0;
let failed = 0;

function test(name, callback) {
    try {
        callback();
        console.log("[PASS] " + name);
        passed++;
    } catch (error) {
        console.log("[FAIL] " + name + ": " + error);
        failed++;
    }
}

function assert(condition, message) {
    if (!condition) throw new Error(message);
}

function assertEqual(actual, expected, what) {
    if (actual !== expected)
        throw new Error(what + ": expected " + JSON.stringify(expected) + ", got " + JSON.stringify(actual));
}

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

/* The driver boot.c loads for the device the script runs from. */
function bootDriver(path) {
    if (path.startsWith("mass")) return "usbmass_bd";
    if (path.startsWith("mc")) return "mcserv";
    if (path.startsWith("cdfs") || path.startsWith("cdrom")) return "cdfs";
    return "fileXio";
}

const [cwd] = os.getcwd();
const driver = bootDriver(cwd);
/* Absolute, so it does not depend on the directory the tests change. */
const probe = cwd + (cwd.endsWith("/") ? "" : "/") + DIR + "pop.adp";

/* The boot device answers again. (os.stat() is not implemented in this port.) */
function deviceReady() {
    const file = std.open(probe, "rb");
    if (!file) return false;
    file.close();
    return true;
}

/* IOP.reset(), then what the boot sequence does to get files back. */
function resetIop() {
    IOP.reset();
    IOP.loadModule("fileXio");
    if (driver !== "fileXio")
        IOP.loadModule(driver);
    return waitUntil(deviceReady, 5000);
}

console.log("=== Sound IOP reset tests (" + cwd + ", driver " + driver + ") ===");

let pop = null;
let loop = null;
let wav = null;
let wavBefore = 0;

test("setup: samples loaded, stream playing", function() {
    pop = new Sound.Sfx(DIR + "pop.adp");
    loop = new Sound.Sfx(DIR + "loop.adp");
    wav = new Sound.Stream(DIR + "bg.wav");
    Sound.setVolume(80);
    Sound.setSfxVolume(90);
    assertEqual(Sound.getMemoryStats().samples, 2, "samples");
    wav.position = 5000;
    wav.play();
    wait(600);
    assert(wav.playing(), "stream not playing");
    assert(pop.play() >= 0, "pop did not play");
});

test("IOP.reset() while a stream plays", function() {
    assert(resetIop(), cwd + " did not come back after reloading " + driver);
    assertEqual(IOP.getModule("audsrv").started, false, "audsrv.started");
    assertEqual(wav.playing(), false, "stream playing after the reset");
    wavBefore = wav.position;
    assert(wavBefore > 5300 && wavBefore < 5800, "position kept at the reset: " + wavBefore);
    const stats = Sound.getMemoryStats();
    assertEqual(stats.samples, 0, "samples after the reset");
    assertEqual(stats.used, 0, "SPU2 memory used after the reset");
});

test("a sample dropped by the reset is not playing, and stays unloaded", function() {
    assertEqual(pop.playing(0), false, "pop.playing(0)");
    assertEqual(IOP.getModule("audsrv").started, false, "playing() loaded audsrv");
});

test("reloads find the files after the directory changed", function() {
    // Relative paths would now resolve to tests/tests/sound/...
    assertEqual(os.chdir(cwd + (cwd.endsWith("/") ? "" : "/") + "tests"), 0, "chdir");
});

test("play() reloads the sample from its file", function() {
    const channel = pop.play();
    assert(channel >= 0, "channel " + channel);
    assertEqual(IOP.getModule("audsrv").started, true, "audsrv.started");
    assertEqual(Sound.getMemoryStats().samples, 1, "only the played sample was reloaded");
    assertEqual(pop.playing(channel), true, "pop.playing");
});

test("settings survive the reset", function() {
    assertEqual(Sound.getVolume(), 80, "stream volume");
    assertEqual(Sound.getSfxVolume(), 90, "sfx volume");
    assertEqual(pop.volume, 100, "pop.volume");
});

test("the other sample reloads on its own play()", function() {
    assertEqual(loop.play(23), 23, "channel");
    assertEqual(Sound.getMemoryStats().samples, 2, "samples");
    wait(300);
    assertEqual(loop.playing(23), true, "loop playing past its first pass");
    loop.stop(23);
});

test("the stream reopens its file and resumes", function() {
    wav.play();
    wait(700);
    assertEqual(wav.playing(), true, "playing");
    const position = wav.position;
    assert(position > wavBefore + 300 && position < wavBefore + 900,
        "resumed from " + wavBefore + ": " + position);
    wav.pause();
});

test("a new stream and a seek work after the reset", function() {
    // Relative to the directory changed above.
    const short = new Sound.Stream("sound/short.wav");
    assertEqual(short.rate, 22050, "rate");
    short.free();
    wav.position = 20000;
    wav.play();
    wait(400);
    assert(wav.position > 20100 && wav.position < 20500, "position " + wav.position);
    wav.pause();
});

test("loadSfxAsync after the reset", function() {
    const status = Sound.wait(Sound.loadSfxAsync("sound/pop.adp"), 3000);
    assertEqual(status.state, "done", "state " + (status.error || ""));
    assert(status.result.play() >= 0, "plays");
    status.result.free();
});

test("a second reset", function() {
    assertEqual(os.chdir(cwd), 0, "chdir back");
    assert(resetIop(), cwd + " did not come back");
    assert(pop.play() >= 0, "pop after the second reset");
    wav.play();
    wait(500);
    assertEqual(wav.playing(), true, "stream after the second reset");
    wav.stop();
});

test("freeing samples that were never reloaded", function() {
    assert(resetIop(), cwd + " did not come back");
    // Neither was played since: nothing of theirs is on the IOP to free.
    pop.free();
    loop.free();
    wav.free();
    assertEqual(Sound.getMemoryStats().samples, 0, "samples");
});

console.log("Result: " + passed + " passed, " + failed + " failed");
if (failed !== 0) throw new Error("Sound reset tests failed");
