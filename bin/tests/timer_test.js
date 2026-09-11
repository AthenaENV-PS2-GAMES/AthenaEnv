import * as Timer from "Timer";

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

function expectThrow(name, callback) {
    test(name, function() {
        let threw = false;
        try {
            callback();
        } catch (error) {
            threw = true;
        }
        if (!threw) throw new Error("expected an exception");
    });
}

test("Timer exports are available", function() {
    if (typeof Timer.new !== "function") throw new Error("new is missing");
    if (typeof Timer.getTime !== "function") throw new Error("getTime is missing");
    if (typeof Timer.destroy !== "function") throw new Error("destroy is missing");
});

test("new timer starts running", function() {
    const timer = Timer.new();
    if (typeof timer !== "number" || timer === 0) throw new Error("invalid timer handle");
    if (!Timer.isPlaying(timer)) throw new Error("timer is not running");
    Timer.destroy(timer);
});

test("timer can pause and resume", function() {
    const timer = Timer.new();
    Timer.pause(timer);
    if (Timer.isPlaying(timer)) throw new Error("timer is still running");
    const paused = Timer.getTime(timer);
    Timer.resume(timer);
    if (!Timer.isPlaying(timer)) throw new Error("timer did not resume");
    if (Timer.getTime(timer) < paused) throw new Error("time moved backwards");
    Timer.destroy(timer);
});

test("timer reset clears elapsed time", function() {
    const timer = Timer.new();
    Timer.setTime(timer, 100);
    Timer.pause(timer);
    Timer.reset(timer);
    if (Timer.getTime(timer) !== 0) throw new Error("timer was not reset");
    Timer.destroy(timer);
});

expectThrow("new rejects arguments", function() {
    Timer.new(1);
});

expectThrow("getTime rejects missing arguments", function() {
    Timer.getTime();
});

console.log("Result: " + passed + " passed, " + failed + " failed");
if (failed !== 0) throw new Error("Timer tests failed");
