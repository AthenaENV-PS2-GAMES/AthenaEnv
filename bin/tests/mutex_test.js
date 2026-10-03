import * as Mutex from "Mutex";

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

test("Mutex exports are available", function() {
    if (typeof Mutex.new !== "function") throw new Error("new is missing");
    if (typeof Mutex.lock !== "function") throw new Error("lock is missing");
    if (typeof Mutex.unlock !== "function") throw new Error("unlock is missing");
});

test("mutex can be locked and unlocked", function() {
    const mutex = Mutex.new();
    if (Mutex.lock(mutex) < 0) throw new Error("lock failed");
    if (Mutex.unlock(mutex) < 0) throw new Error("unlock failed");
    Mutex.destroy(mutex);
});

expectThrow("lock rejects values from another type", function() {
    Mutex.lock({});
});

expectThrow("lock rejects missing arguments", function() {
    Mutex.lock();
});

expectThrow("destroy rejects an already destroyed mutex", function() {
    const mutex = Mutex.new();
    Mutex.destroy(mutex);
    Mutex.destroy(mutex);
});

console.log("Result: " + passed + " passed, " + failed + " failed");
if (failed !== 0) throw new Error("Mutex tests failed");
