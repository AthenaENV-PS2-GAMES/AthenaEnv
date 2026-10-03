/*
 * MemoryCard across IOP.reset(): files opened before the reset are refused
 * (their descriptor numbers may belong to someone else afterwards), close()
 * still releases them, and the next call starts mcserv again when the boot
 * sequence did not.
 *
 * Kept apart from memcard_test.js: IOP.reset() unloads every driver, file
 * I/O included, so this script reloads the boot device's driver itself,
 * like the boot sequence does (src/core/boot.c). Works from host: (PCSX2),
 * mass: (USB), mc: and cdfs:. Needs a formatted card in slot 1 with 64 KiB
 * free. Run with default_script=tests/memcard_reset_test.js.
 */

const ROOT = "mc0:/ATHTEST";

let passed = 0;
let failed = 0;
let skipped = 0;

function test(name, callback) {
    try {
        callback();
        console.log("[PASS] " + name);
        passed++;
    } catch (error) {
        console.log("[FAIL] " + name + ": " + error + (error && error.code ? " (" + error.code + ")" : ""));
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

function expectCode(callback, code) {
    try {
        callback();
    } catch (error) {
        assertEqual(error.code, code, "error.code");
        return;
    }
    throw new Error("expected " + code);
}

function waitUntil(condition, timeoutMs) {
    for (let elapsed = 0; elapsed < timeoutMs; elapsed += 20) {
        if (condition()) return true;
        System.sleep(20);
    }
    return condition();
}

function patternBuffer(size, seed) {
    const data = new Uint8Array(size);
    for (let i = 0; i < size; i++) data[i] = (i * 31 + seed) & 0xFF;
    return data.buffer;
}

function sameBytes(a, b) {
    const x = new Uint8Array(a), y = new Uint8Array(b);
    if (x.length !== y.length) return false;
    for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) return false;
    return true;
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
const probe = cwd + (cwd.endsWith("/") ? "" : "/") + "tests/memcard_reset_test.js";

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

console.log("=== MemoryCard IOP reset tests (" + cwd + ", driver " + driver + ") ===");

const card = MemoryCard.getInfo(0);
if (card.type !== "ps2" || !card.formatted || card.freeBytes < 64 * 1024) {
    console.log("[SKIP] all: need a formatted PS2 card with 64 KiB free in slot 1 (type " + card.type +
        ", formatted " + card.formatted + ", free " + card.freeBytes + ")");
    skipped++;
} else {
    const data = patternBuffer(20000, 8);
    let reader = null, writer = null;

    test("setup: files written and left open", function() {
        if (MemoryCard.exists(ROOT)) MemoryCard.remove(ROOT, { recursive: true });
        MemoryCard.writeFile(ROOT + "/reset.bin", data);
        reader = MemoryCard.open(ROOT + "/reset.bin", "r");
        assertEqual(reader.read(100).byteLength, 100, "read before the reset");
        writer = MemoryCard.open(ROOT + "/open.bin", "w");
        writer.write("before");
    });

    test("IOP.reset() with memory card files open", function() {
        assert(resetIop(), cwd + " did not come back after reloading " + driver);
        assertEqual(IOP.getModule("mcserv").started, driver === "mcserv", "mcserv.started");
    });

    test("files from before the reset are refused", function() {
        expectCode(function() { reader.read(100); }, "CLOSED");
        expectCode(function() { writer.write("after"); }, "CLOSED");
        expectCode(function() { reader.seek(0); }, "CLOSED");
    });

    test("close() releases them without touching the new descriptors", function() {
        reader.close();
        writer.close();
        assert(reader.closed && writer.closed, "closed");
    });

    test("the next call starts mcserv when the boot did not", function() {
        const info = MemoryCard.getInfo(0);
        assertEqual(IOP.getModule("mcserv").started, true, "mcserv.started");
        assertEqual(info.connected, true, "connected");
        assertEqual(info.formatted, true, "formatted");
    });

    test("files written before the reset read back", function() {
        assert(sameBytes(MemoryCard.readFile(ROOT + "/reset.bin"), data), "reset.bin content");
    });

    test("the three driver handles are free again", function() {
        const files = [0, 1, 2].map(n => MemoryCard.open(ROOT + "/h" + n, "w"));
        files.forEach(file => file.close());
    });

    test("System.getMCInfo after the reset", function() {
        const info = System.getMCInfo(0);
        assertEqual(info.type, 2, "type");
        assertEqual(info.format, 1, "format");
    });

    test("a job after the reset", function() {
        const job = MemoryCard.writeFileAsync(ROOT + "/job.bin", data);
        const status = MemoryCard.wait(job, 30000);
        assertEqual(status.state, "done", "state");
        assert(sameBytes(MemoryCard.readFile(ROOT + "/job.bin"), data), "content");
    });

    test("a second reset in a row", function() {
        assert(resetIop(), cwd + " did not come back after the second reset");
        assert(sameBytes(MemoryCard.readFile(ROOT + "/job.bin"), data), "content");
    });

    test("cleanup", function() {
        MemoryCard.remove(ROOT, { recursive: true });
        assert(!MemoryCard.exists(ROOT), "removed");
    });
}

console.log("Result: " + passed + " passed, " + failed + " failed, " + skipped + " skipped");
if (failed !== 0) throw new Error("MemoryCard IOP reset tests failed");
