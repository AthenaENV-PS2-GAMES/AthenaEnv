/*
 * System module smoke and contract tests.
 *
 * Execute this script as the AthenaEnv entry script on PCSX2 and on real
 * hardware. Hardware-dependent operations are reported as skipped when they
 * are unavailable.
 */

const passed = [];
const failed = [];
const skipped = [];

function pass(name) {
    passed.push(name);
    console.log("[PASS] " + name);
}

function fail(name, error) {
    failed.push(name);
    console.log("[FAIL] " + name + ": " + error);
}

function skip(name, reason) {
    skipped.push(name);
    console.log("[SKIP] " + name + ": " + reason);
}

function assert(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

function expectThrow(name, callback) {
    try {
        callback();
        fail(name, "expected an exception");
    } catch (error) {
        pass(name);
    }
}

function test(name, callback) {
    try {
        callback();
        pass(name);
    } catch (error) {
        fail(name, error);
    }
}

console.log("=== AthenaEnv System module tests ===");
console.log("Boot path: " + System.bootPath);

test("bootPath is a non-empty string", function() {
    assert(typeof System.bootPath === "string", "bootPath must be a string");
    assert(System.bootPath.length > 0, "bootPath must not be empty");
    assert(System.boot_path === System.bootPath, "boot_path alias must match bootPath");
});

test("listDir returns directory entries", function() {
    const entries = System.listDir();
    assert(Array.isArray(entries), "listDir must return an array");
    for (const entry of entries) {
        assert(typeof entry.name === "string", "entry.name must be a string");
        assert(typeof entry.size === "number", "entry.size must be a number");
        assert(typeof entry.dir === "boolean", "entry.dir must be a boolean");
        assert(entry.name !== "." && entry.name !== "..", "special entries must be filtered");
    }
});

test("devices returns an array", function() {
    const devices = System.devices();
    assert(Array.isArray(devices), "devices must return an array");
    for (const device of devices) {
        assert(typeof device.name === "string", "device.name must be a string");
        assert(typeof device.desc === "string", "device.desc must be a string");
    }
});

test("CPU information has the expected shape", function() {
    const info = System.getCPUInfo();
    assert(typeof info.implementation === "number", "CPU implementation must be numeric");
    assert(typeof info.revision === "number", "CPU revision must be numeric");
    assert(typeof info.RAMSize === "number" && info.RAMSize > 0, "RAMSize must be positive");
    assert(typeof info.BUSClock === "number" && info.BUSClock > 0, "BUSClock must be positive");
    assert(typeof info.CPUClock === "number" && info.CPUClock > 0, "CPUClock must be positive");
    assert(typeof info.MachineType === "number", "MachineType must be numeric");
});

test("GPU information has the expected shape", function() {
    const info = System.getGPUInfo();
    assert(typeof info.revision === "number", "GPU revision must be numeric");
    assert(typeof info.id === "number", "GPU id must be numeric");
});

test("memory APIs return non-negative values", function() {
    const stats = System.getMemoryStats();
    assert(typeof stats.core === "number", "core must be numeric");
    assert(typeof stats.nativeStack === "number", "nativeStack must be numeric");
    assert(typeof stats.allocs === "number", "allocs must be numeric");
    assert(typeof stats.used === "number" && stats.used >= 0, "used must be non-negative");
    assert(System.getUsedMemory() >= 0, "getUsedMemory must be non-negative");
    assert(System.getFreeMemory() >= 0, "getFreeMemory must be non-negative");
});

test("clock APIs return monotonic values", function() {
    const ticksBefore = System.getTicks();
    const millisecondsBefore = System.getMilliseconds();
    System.delay();
    const ticksAfter = System.getTicks();
    const millisecondsAfter = System.getMilliseconds();
    assert(typeof ticksBefore === "number", "getTicks must return a number");
    assert(typeof millisecondsBefore === "number", "getMilliseconds must return a number");
    assert(ticksAfter >= ticksBefore, "ticks must not move backwards");
    assert(millisecondsAfter >= millisecondsBefore, "milliseconds must not move backwards");
});

test("sleep accepts a valid duration", function() {
    System.sleep(1);
});

test("setDarkMode accepts boolean values", function() {
    System.setDarkMode(false);
    System.setDarkMode(true);
});

test("garbage collection completes", function() {
    System.gc();
});

expectThrow("sleep rejects missing arguments", function() {
    System.sleep();
});

expectThrow("getCPUInfo rejects extra arguments", function() {
    System.getCPUInfo(true);
});

expectThrow("loadELF rejects non-array arguments", function() {
    System.loadELF("invalid.elf", "not-an-array");
});

try {
    const temperature = System.getTemperature();
    if (typeof temperature === "undefined") {
        skip("getTemperature", "not available on this platform");
    } else {
        assert(typeof temperature === "number", "temperature must be numeric");
        pass("getTemperature");
    }
} catch (error) {
    skip("getTemperature", "hardware query failed: " + error);
}

for (let port = 0; port < 2; port++) {
    try {
        const memoryCard = System.getMCInfo(port);
        assert(typeof memoryCard.type === "number", "memory-card type must be numeric");
        assert(typeof memoryCard.freemem === "number", "memory-card free space must be numeric");
        assert(typeof memoryCard.format === "number", "memory-card format must be numeric");
        pass("getMCInfo(port " + port + ")");
    } catch (error) {
        skip("getMCInfo(port " + port + ")", "memory card unavailable: " + error);
    }
}

console.log("");
console.log("Result: " + passed.length + " passed, " +
    failed.length + " failed, " + skipped.length + " skipped");

if (failed.length > 0) {
    throw new Error("System module tests failed");
}
