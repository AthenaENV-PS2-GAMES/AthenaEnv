/*
 * MemoryCard module smoke and contract tests.
 *
 * Execute this script as the AthenaEnv entry script on PCSX2 and on real
 * hardware (default_script=tests/memcard_test.js). It needs a formatted card
 * in slot 1 (mc0:) with 64 KiB free, 320 KiB for the frame loop check;
 * without one, the card tests are reported as skipped. A card in slot 2 gets
 * a short round trip too. Everything is written below mcN:/ATHTEST, which is
 * removed at the end. format/unformat are only checked for argument errors:
 * they would erase the card.
 *
 * Also checked here: the same files through std.open (the iomanX mc device),
 * the three driver handles both paths share, and that a Loop keeps drawing
 * frames while a write job runs. Throughput is printed as [INFO].
 *
 * Card removal and swaps: memcard_interactive_test.js. IOP.reset():
 * memcard_reset_test.js.
 */

const ROOT = "mc0:/ATHTEST";
const passed = [];
const failed = [];
const skipped = [];
/* Returned by a test callback that reported itself as skipped. */
const SKIPPED = {};

function pass(name) {
    passed.push(name);
    console.log("[PASS] " + name);
}

function fail(name, error) {
    failed.push(name);
    console.log("[FAIL] " + name + ": " + error + (error && error.code ? " (" + error.code + ")" : ""));
}

function skip(name, reason) {
    skipped.push(name);
    console.log("[SKIP] " + name + ": " + reason);
    return SKIPPED;
}

function assert(condition, message) {
    if (!condition) throw new Error(message);
}

function test(name, callback) {
    try {
        if (callback() !== SKIPPED) pass(name);
    } catch (error) {
        fail(name, error);
    }
}

async function testAsync(name, callback) {
    try {
        if (await callback() !== SKIPPED) pass(name);
    } catch (error) {
        fail(name, error);
    }
}

/* Passes when callback throws `type` with `error.code === code` (code may be null). */
function expectCode(name, callback, code, type) {
    try {
        callback();
        fail(name, "expected an exception");
    } catch (error) {
        if (type && !(error instanceof type)) fail(name, "wrong error type: " + error);
        else if (code && error.code !== code) fail(name, "expected code " + code + ", got " + error.code + ": " + error);
        else pass(name);
    }
}

function bytes(buffer) {
    return new Uint8Array(buffer);
}

function sameBytes(a, b) {
    const x = bytes(a), y = bytes(b);
    if (x.length !== y.length) return false;
    for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) return false;
    return true;
}

function patternBuffer(size, seed) {
    const data = new Uint8Array(size);
    for (let i = 0; i < size; i++) data[i] = (i * 31 + seed) & 0xFF;
    return data.buffer;
}

console.log("=== AthenaEnv MemoryCard module tests ===");

/* --- Exports and arguments (no card needed) -------------------------------- */

test("module is imported and exposed globally", function() {
    assert(typeof MemoryCard === "object", "MemoryCard must be a global");
    const functions = ["getInfo", "format", "unformat", "stat", "exists", "list", "mkdir", "remove",
        "rename", "setInfo", "getFreeEntries", "readFile", "readText", "readJSON", "writeFile", "writeJSON", "open",
        "createIconSys", "readFileAsync", "readTextAsync", "readJSONAsync", "writeFileAsync", "writeJSONAsync", "removeAsync", "formatAsync",
        "poll", "wait", "cancel"];
    for (const name of functions)
        assert(typeof MemoryCard[name] === "function", name + " must be a function");
    assert(MemoryCard.ATTR_DIRECTORY === 0x20 && MemoryCard.ATTR_FILE === 0x10, "attribute constants");
    assert(MemoryCard.NAME_MAX === 31 && MemoryCard.MAX_OPEN_FILES === 3 && MemoryCard.CLUSTER_SIZE === 1024,
        "limits");
});

expectCode("paths must name a card", function() { MemoryCard.stat("mass:/x"); }, "INVALID_ARGUMENT", TypeError);
expectCode("paths must be strings", function() { MemoryCard.stat(42); }, "INVALID_ARGUMENT", TypeError);
expectCode("wildcards are rejected", function() { MemoryCard.stat("mc0:/A*"); }, "INVALID_ARGUMENT", TypeError);
expectCode("names longer than 31 bytes are rejected", function() {
    MemoryCard.stat("mc0:/" + "x".repeat(32));
}, "INVALID_ARGUMENT", TypeError);
expectCode("paths cannot leave the root", function() { MemoryCard.stat("mc0:/.."); }, "INVALID_ARGUMENT", TypeError);
expectCode("missing arguments", function() { MemoryCard.readFile(); }, "INVALID_ARGUMENT", TypeError);
expectCode("extra arguments", function() { MemoryCard.exists("mc0:/a", 1); }, "INVALID_ARGUMENT", TypeError);
expectCode("port out of range", function() { MemoryCard.getInfo(2); }, null, RangeError);
expectCode("format port type", function() { MemoryCard.format("0"); }, "INVALID_ARGUMENT", TypeError);
expectCode("format without a port", function() { MemoryCard.format(); }, "INVALID_ARGUMENT", TypeError);
expectCode("unknown open mode", function() { MemoryCard.open("mc0:/a", "rw"); }, "INVALID_ARGUMENT", TypeError);
expectCode("writeFile data type", function() { MemoryCard.writeFile("mc0:/a", 12); }, "INVALID_ARGUMENT", TypeError);
expectCode("writeFile options type", function() {
    MemoryCard.writeFile("mc0:/a", "x", { atomic: 1 });
}, "INVALID_ARGUMENT", TypeError);
expectCode("remove refuses the root", function() {
    MemoryCard.remove("mc0:/", { recursive: true });
}, "INVALID_ARGUMENT", TypeError);
expectCode("rename takes a name, not a path", function() {
    MemoryCard.rename("mc0:/a", "b/c");
}, "INVALID_ARGUMENT", TypeError);
expectCode("setInfo rejects other attribute bits", function() {
    MemoryCard.setInfo("mc0:/a", { attributes: MemoryCard.ATTR_DIRECTORY });
}, "INVALID_ARGUMENT", TypeError);
expectCode("poll rejects other values", function() { MemoryCard.poll({}); }, "INVALID_ARGUMENT", TypeError);

test("createIconSys builds a 964-byte icon.sys", function() {
    const data = bytes(MemoryCard.createIconSys({ title: "Athena\nTest", icon: "icon.ico" }));
    assert(data.length === 964, "size " + data.length);
    assert(String.fromCharCode(data[0], data[1], data[2], data[3]) === "PS2D", "magic");
    assert(data[6] === 12, "line break offset " + data[6]);
    assert(data[192] === 0x82 && data[193] === 0x60, "Shift-JIS 'A'");
    assert(data[260] === 0x69, "icon name");
});
expectCode("createIconSys needs a title", function() {
    MemoryCard.createIconSys({ icon: "icon.ico" });
}, "INVALID_ARGUMENT", TypeError);
expectCode("createIconSys rejects non-ASCII titles", function() {
    MemoryCard.createIconSys({ title: "café", icon: "icon.ico" });
}, "INVALID_ARGUMENT", TypeError);
expectCode("createIconSys checks colors", function() {
    MemoryCard.createIconSys({ title: "x", icon: "i", background: [[0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 300]] });
}, "INVALID_ARGUMENT", TypeError);

/* --- Card ------------------------------------------------------------------- */

let card = null;
try {
    card = MemoryCard.getInfo(0);
    test("getInfo shape", function() {
        assert(card.port === 0, "port");
        assert(["none", "ps1", "ps2", "pocketstation"].includes(card.type), "type " + card.type);
        assert(card.connected === (card.type !== "none"), "connected");
        assert(typeof card.formatted === "boolean" && typeof card.changed === "boolean", "flags");
        assert(card.freeBytes === card.freeClusters * 1024, "freeBytes");
    });
    test("second getInfo reports no change", function() {
        assert(MemoryCard.getInfo(0).changed === false, "changed");
    });
    test("empty or present slot 2 does not throw", function() {
        const other = MemoryCard.getInfo(1);
        assert(typeof other.connected === "boolean", "connected");
    });
    test("System.getMCInfo goes through the module", function() {
        if (typeof System === "undefined") return skip("System.getMCInfo", "System is not in this runtime");
        const info = System.getMCInfo(0);
        assert(info.type === (card.type === "ps2" ? 2 : info.type), "type " + info.type);
        assert(info.format === (card.formatted ? 1 : 0), "format " + info.format);
    });
} catch (error) {
    skip("getInfo", "memory card service unavailable: " + error + " (" + error.code + ")");
}

const cardReady = card && card.type === "ps2" && card.formatted && card.freeBytes >= 64 * 1024;
if (!cardReady) {
    skip("card operations", card ? ("need a formatted PS2 card with 64 KiB free in slot 1 (type " +
        card.type + ", formatted " + card.formatted + ", free " + card.freeBytes + ")") : "no card service");
}

function cardTests() {
    if (MemoryCard.exists(ROOT)) MemoryCard.remove(ROOT, { recursive: true });
    /* Left on the card by an earlier version of this test ("a" was not a mode yet). */
    if (MemoryCard.exists("mc0:/a")) {
        const leftover = MemoryCard.stat("mc0:/a");
        if (!leftover.directory && leftover.size === 0) MemoryCard.remove("mc0:/a");
    }

    test("mkdir creates nested directories", function() {
        assert(MemoryCard.mkdir(ROOT + "/A/B", { recursive: true }) === true, "created");
        assert(MemoryCard.mkdir(ROOT + "/A/B", { recursive: true }) === false, "already there");
        assert(MemoryCard.stat(ROOT + "/A").directory, "directory");
    });
    expectCode("mkdir of an existing directory", function() { MemoryCard.mkdir(ROOT); }, "EXISTS");
    expectCode("mkdir without the parent", function() { MemoryCard.mkdir(ROOT + "/X/Y"); }, "NOT_FOUND");

    const big = patternBuffer(40000, 5);
    test("writeFile / readFile round trip across blocks", function() {
        assert(MemoryCard.writeFile(ROOT + "/big.bin", big) === 40000, "bytes written");
        assert(sameBytes(MemoryCard.readFile(ROOT + "/big.bin"), big), "content");
    });
    test("writeFile of a typed array view", function() {
        const view = new Uint8Array(big, 100, 50);
        MemoryCard.writeFile(ROOT + "/view.bin", view);
        assert(sameBytes(MemoryCard.readFile(ROOT + "/view.bin"), view.slice().buffer), "view bytes");
    });
    test("text round trip (UTF-8) and createDirs", function() {
        const text = JSON.stringify({ level: 3, name: "Athéna" });
        MemoryCard.writeFile(ROOT + "/new/dir/save.json", text);
        assert(MemoryCard.readText(ROOT + "/new/dir/save.json") === text, "text");
    });
    test("atomic replace leaves no temporary file", function() {
        MemoryCard.writeFile(ROOT + "/save.json", "old", { atomic: true });
        MemoryCard.writeFile(ROOT + "/save.json", "new", { atomic: true });
        assert(MemoryCard.readText(ROOT + "/save.json") === "new", "content");
        assert(!MemoryCard.exists(ROOT + "/save.json~"), "temporary file removed");
    });
    expectCode("createDirs: false needs the parent", function() {
        MemoryCard.writeFile(ROOT + "/nope/file", "x", { createDirs: false });
    }, "NOT_FOUND");
    expectCode("readFile of a directory", function() { MemoryCard.readFile(ROOT); }, "IS_DIRECTORY");
    expectCode("readFile of a missing file", function() { MemoryCard.readFile(ROOT + "/none"); }, "NOT_FOUND");
    test("exists", function() {
        assert(MemoryCard.exists(ROOT + "/big.bin") && !MemoryCard.exists(ROOT + "/none"), "exists");
    });

    test("stat and list", function() {
        const entry = MemoryCard.stat(ROOT + "/big.bin");
        assert(entry.name === "big.bin" && entry.size === 40000 && !entry.directory, "entry");
        assert(entry.path === ROOT + "/big.bin", "path " + entry.path);
        assert(entry.modified > Date.UTC(2000, 0, 1) || entry.modified === 0, "modified " + entry.modified);
        const names = MemoryCard.list(ROOT).map(function(e) { return e.name; }).sort();
        assert(names.join() === "A,big.bin,new,save.json,view.bin", "names " + names.join());
        assert(MemoryCard.list("mc0:/").some(function(e) { return e.name === "ATHTEST"; }), "root listing");
    });
    expectCode("list of a file", function() { MemoryCard.list(ROOT + "/big.bin"); }, "NOT_DIRECTORY");
    test("getFreeEntries", function() {
        assert(MemoryCard.getFreeEntries(ROOT) >= 0, "count");
    });

    test("rename in place", function() {
        MemoryCard.rename(ROOT + "/view.bin", "renamed.bin");
        assert(MemoryCard.exists(ROOT + "/renamed.bin") && !MemoryCard.exists(ROOT + "/view.bin"), "renamed");
    });
    expectCode("rename onto an existing entry", function() {
        MemoryCard.rename(ROOT + "/renamed.bin", "big.bin");
    }, "EXISTS");

    test("setInfo changes attributes and dates", function() {
        const modified = Date.UTC(2030, 5, 7, 8, 9, 10);
        MemoryCard.setInfo(ROOT + "/renamed.bin", {
            attributes: MemoryCard.ATTR_READABLE | MemoryCard.ATTR_HIDDEN,
            modified: new Date(modified),
        });
        const entry = MemoryCard.stat(ROOT + "/renamed.bin");
        assert((entry.attributes & MemoryCard.ATTR_WRITABLE) === 0, "read-only");
        assert((entry.attributes & MemoryCard.ATTR_HIDDEN) !== 0, "hidden");
        assert(entry.attributes & MemoryCard.ATTR_FILE, "still a file");
        assert(entry.modified === modified, "modified " + new Date(entry.modified).toISOString());
    });
    expectCode("read-only files cannot be replaced", function() {
        MemoryCard.writeFile(ROOT + "/renamed.bin", "x");
    }, "ACCESS_DENIED");
    expectCode("read-only files cannot be opened for writing", function() {
        MemoryCard.open(ROOT + "/renamed.bin", "r+");
    }, "ACCESS_DENIED");
    test("attributes can be restored", function() {
        MemoryCard.setInfo(ROOT + "/renamed.bin", {
            attributes: MemoryCard.ATTR_READABLE | MemoryCard.ATTR_WRITABLE | MemoryCard.ATTR_EXECUTABLE,
        });
        MemoryCard.writeFile(ROOT + "/renamed.bin", "writable again");
        /* The driver deletes read-only entries too; restoring first is just tidy. */
        MemoryCard.remove(ROOT + "/renamed.bin");
    });

    test("open, write, seek, read, close", function() {
        const file = MemoryCard.open(ROOT + "/stream.bin", "w+");
        assert(file.write("hello world") === 11 && file.tell() === 11, "write");
        assert(file.seek(6) === 6, "seek");
        file.write(new Uint8Array([87, 79, 82, 76, 68]));
        file.flush();
        assert(file.seek(-5, "end") === 6, "seek from end");
        assert(String.fromCharCode.apply(null, bytes(file.read(64))) === "WORLD", "read");
        assert(file.read(8).byteLength === 0, "end of file");
        file.close();
        file.close();
        assert(file.closed, "closed");
    });
    test("file.size, read() of the rest, append", function() {
        const log = MemoryCard.open(ROOT + "/log.txt", "a");
        assert(log.size === 0, "new file size " + log.size);
        log.write("one;");
        log.close();
        const more = MemoryCard.open(ROOT + "/log.txt", "a+");
        assert(more.size === 4 && more.tell() === 4, "append starts at the end");
        more.write("two;");
        assert(more.size === 8, "size " + more.size);
        more.seek(0);
        assert(String.fromCharCode.apply(null, bytes(more.read())) === "one;two;", "read() reads the rest");
        assert(more.read().byteLength === 0, "nothing left");
        more.close();
    });
    test("readJSON / writeJSON", function() {
        const state = { level: 7, name: "Athéna", items: [1, 2, 3], nested: { ok: true } };
        MemoryCard.writeJSON(ROOT + "/state.json", state, { indent: 2, atomic: true });
        assert(MemoryCard.readText(ROOT + "/state.json").indexOf("\n  \"level\": 7") >= 0, "indented");
        assert(JSON.stringify(MemoryCard.readJSON(ROOT + "/state.json")) === JSON.stringify(state), "round trip");
    });
    test("readJSON of invalid JSON throws SyntaxError with the path", function() {
        MemoryCard.writeFile(ROOT + "/bad.json", "{ not json");
        try {
            MemoryCard.readJSON(ROOT + "/bad.json");
            throw new Error("parsed");
        } catch (error) {
            assert(error instanceof SyntaxError, "type " + error.name);
            assert(error.path === ROOT + "/bad.json", "path " + error.path);
        }
    });
    expectCode("writeJSON of an unserializable value", function() {
        MemoryCard.writeJSON(ROOT + "/x.json", undefined);
    }, "INVALID_ARGUMENT", TypeError);
    test("errors carry error.path", function() {
        try {
            MemoryCard.readFile(ROOT + "/nothing/here");
            throw new Error("read");
        } catch (error) {
            assert(error.code === "NOT_FOUND" && error.path === ROOT + "/nothing/here", "path " + error.path);
        }
    });

    expectCode("closed file", function() {
        const file = MemoryCard.open(ROOT + "/stream.bin");
        file.close();
        file.read(1);
    }, "CLOSED");
    expectCode("open of a directory", function() { MemoryCard.open(ROOT); }, "IS_DIRECTORY");
    expectCode("open of a missing file for reading", function() { MemoryCard.open(ROOT + "/none"); }, "NOT_FOUND");
    test("the driver's three handles", function() {
        const files = [];
        try {
            for (let i = 0; i < 3; i++) files.push(MemoryCard.open(ROOT + "/h" + i, "w"));
            try {
                MemoryCard.open(ROOT + "/h3", "w");
                throw new Error("a fourth handle opened");
            } catch (error) {
                if (error.code !== "TOO_MANY_OPEN") throw error;
            }
        } catch (error) {
            if (error.code === "TOO_MANY_OPEN" && files.length < 3)
                return skip("the driver's three handles", "another file is open on the card");
            throw error;
        } finally {
            for (const file of files) file.close();
        }
    });

    expectCode("remove of a non-empty directory", function() { MemoryCard.remove(ROOT + "/A"); }, "NOT_EMPTY");
    test("remove recursive", function() {
        MemoryCard.remove(ROOT + "/A", { recursive: true });
        assert(!MemoryCard.exists(ROOT + "/A"), "removed");
    });

    /* The same card through the iomanX mc device (fopen, what std.open uses). */
    test("files written by MemoryCard are read through std.open", function() {
        if (typeof std.open !== "function") return skip("files written by MemoryCard are read through std.open", "std.open is not in this runtime");
        MemoryCard.writeFile(ROOT + "/interop.txt", "from MemoryCard");
        const file = std.open(ROOT + "/interop.txt", "rb");
        assert(file, "std.open failed");
        const text = file.readAsString();
        file.close();
        assert(text === "from MemoryCard", "text " + JSON.stringify(text));
    });
    test("files written through std.open are read by MemoryCard", function() {
        if (typeof std.open !== "function") return skip("files written through std.open are read by MemoryCard", "std.open is not in this runtime");
        const file = std.open(ROOT + "/std.txt", "wb");
        assert(file, "std.open failed");
        file.puts("from std");
        file.close();
        assert(MemoryCard.readText(ROOT + "/std.txt") === "from std", "text");
        assert(MemoryCard.stat(ROOT + "/std.txt").size === 8, "size");
    });
    test("std.open and MemoryCard share the driver's three handles", function() {
        if (typeof std.open !== "function") return skip("std.open and MemoryCard share the driver's three handles", "std.open is not in this runtime");
        const outside = std.open(ROOT + "/std.txt", "rb");
        assert(outside, "std.open failed");
        const files = [];
        try {
            files.push(MemoryCard.open(ROOT + "/h0", "w"));
            files.push(MemoryCard.open(ROOT + "/h1", "w"));
            try {
                MemoryCard.open(ROOT + "/h2", "w");
                throw new Error("a third MemoryCard handle opened next to a std file");
            } catch (error) {
                if (error.code !== "TOO_MANY_OPEN") throw error;
            }
        } finally {
            for (const file of files) file.close();
            outside.close();
        }
        /* Every handle is back. */
        MemoryCard.open(ROOT + "/h2", "w").close();
    });

    test("throughput (128 KiB)", function() {
        const data = patternBuffer(128 * 1024, 2);
        let start = Date.now();
        MemoryCard.writeFile(ROOT + "/speed.bin", data);
        const writeMs = Math.max(1, Date.now() - start);
        start = Date.now();
        const back = MemoryCard.readFile(ROOT + "/speed.bin");
        const readMs = Math.max(1, Date.now() - start);
        assert(sameBytes(back, data), "content");
        console.log("[INFO] write " + writeMs + " ms (" + Math.round(128000 / writeMs) + " KiB/s), read " +
            readMs + " ms (" + Math.round(128000 / readMs) + " KiB/s)");
        MemoryCard.remove(ROOT + "/speed.bin");
    });
}

/* Slot 2 (mc1:): a round trip when a card is there, NO_CARD otherwise. */
function slot2Tests() {
    let info;
    try {
        info = MemoryCard.getInfo(1);
    } catch (error) {
        return skip("slot 2", "getInfo(1) failed: " + error);
    }
    if (!info.connected) {
        expectCode("slot 2 empty: operations fail with NO_CARD", function() {
            MemoryCard.list("mc1:/");
        }, "NO_CARD");
        return;
    }
    if (info.type !== "ps2" || !info.formatted || info.freeBytes < 16 * 1024)
        return skip("slot 2", "card in slot 2 is " + info.type + (info.formatted ? "" : ", unformatted") +
            ", " + info.freeBytes + " bytes free");
    test("slot 2 round trip", function() {
        const dir = "mc1:/ATHTEST";
        const data = patternBuffer(5000, 11);
        if (MemoryCard.exists(dir)) MemoryCard.remove(dir, { recursive: true });
        MemoryCard.writeFile(dir + "/slot2.bin", data);
        assert(sameBytes(MemoryCard.readFile(dir + "/slot2.bin"), data), "content");
        assert(!MemoryCard.exists("mc0:/ATHTEST/slot2.bin"), "written to the other card");
        MemoryCard.remove(dir, { recursive: true });
        assert(!MemoryCard.exists(dir), "removed");
    });
}

async function asyncTests() {
    await testAsync("writeFileAsync / readFileAsync with poll and wait", async function() {
        const data = patternBuffer(50000, 9);
        const job = MemoryCard.writeFileAsync(ROOT + "/async.bin", data);
        let status = MemoryCard.poll(job);
        assert(["running", "done"].includes(status.state), "state " + status.state);
        assert(status.bytesTotal === 50000, "bytesTotal " + status.bytesTotal);
        status = MemoryCard.wait(job, 30000);
        assert(status.state === "done" && status.result === 50000, "write " + status.state);
        assert(MemoryCard.poll(job).result === 50000, "same result on later polls");

        const read = MemoryCard.readFileAsync(ROOT + "/async.bin");
        status = MemoryCard.wait(read);
        assert(status.state === "done" && sameBytes(status.result, data), "read");
        assert(MemoryCard.poll(read).result === status.result, "same buffer on later polls");
    });

    await testAsync("jobs can be awaited", async function() {
        const written = await MemoryCard.writeFileAsync(ROOT + "/await.json", "{\"ok\":true}");
        assert(written === 11, "written " + written);
        const buffer = await MemoryCard.readFileAsync(ROOT + "/await.json");
        assert(buffer.byteLength === 11, "read " + buffer.byteLength);
    });

    await testAsync("readTextAsync / readJSONAsync / writeJSONAsync", async function() {
        const state = { level: 3, name: "Athéna" };
        const written = await MemoryCard.writeJSONAsync(ROOT + "/async.json", state);
        assert(written === JSON.stringify(state).length + 1, "bytes " + written); /* é is two bytes */
        assert(await MemoryCard.readTextAsync(ROOT + "/async.json") === JSON.stringify(state), "text");
        assert(JSON.stringify(await MemoryCard.readJSONAsync(ROOT + "/async.json")) === JSON.stringify(state),
            "json");
    });

    await testAsync("readJSONAsync of invalid JSON fails the job", async function() {
        MemoryCard.writeFile(ROOT + "/bad-async.json", "{ nope");
        const job = MemoryCard.readJSONAsync(ROOT + "/bad-async.json");
        const status = MemoryCard.wait(job);
        assert(status.state === "failed" && status.error instanceof SyntaxError, "state " + status.state);
        assert(status.error.path === ROOT + "/bad-async.json", "path " + status.error.path);
        try {
            await job;
            throw new Error("resolved");
        } catch (error) {
            assert(error instanceof SyntaxError, "awaited " + error);
        }
    });

    await testAsync("awaited failures reject with the error code", async function() {
        try {
            await MemoryCard.readFileAsync(ROOT + "/missing");
            throw new Error("expected a rejection");
        } catch (error) {
            assert(error.code === "NOT_FOUND", "code " + error.code);
        }
    });

    await testAsync("cancel a running write", async function() {
        const job = MemoryCard.writeFileAsync(ROOT + "/cancel.bin", patternBuffer(100000, 1));
        MemoryCard.cancel(job);
        const status = MemoryCard.wait(job);
        if (status.state === "done") return skip("cancel a running write", "finished before the cancel");
        assert(status.state === "cancelled" && status.error.code === "CANCELLED", "state " + status.state);
        assert(!MemoryCard.exists(ROOT + "/cancel.bin"), "partial file removed");
    });

    /* The point of the jobs: the frame loop keeps flipping while the card works. */
    await testAsync("frames keep coming while a job writes", async function() {
        if (typeof Loop === "undefined")
            return skip("frames keep coming while a job writes", "Loop is not in this runtime");
        const size = 256 * 1024;
        if (MemoryCard.getInfo(0).freeBytes < size + 64 * 1024)
            return skip("frames keep coming while a job writes", "needs 320 KiB free");
        const data = patternBuffer(size, 6);
        const outcome = await new Promise(function(resolve) {
            const job = MemoryCard.writeFileAsync(ROOT + "/loop.bin", data);
            let frames = 0, slowest = 0, lastDone = 0, progressed = 0;
            Loop.run(function() {
                const status = MemoryCard.poll(job);
                if (status.state === "running") {
                    frames++;
                    slowest = Math.max(slowest, Loop.getStats().frameMs);
                    if (status.bytesDone > lastDone) {
                        progressed++;
                        lastDone = status.bytesDone;
                    }
                    return;
                }
                Loop.stop();
                resolve({ status: status, frames: frames, slowest: slowest, progressed: progressed });
            });
        });
        console.log("[INFO] " + outcome.frames + " frames during the write, slowest " +
            outcome.slowest.toFixed(1) + " ms, progress updated " + outcome.progressed + " times");
        assert(outcome.status.state === "done" && outcome.status.result === size, "state " + outcome.status.state);
        assert(outcome.frames >= 2, "only " + outcome.frames + " frames while the job ran");
        assert(outcome.slowest < 100, "a frame took " + outcome.slowest + " ms");
        assert(sameBytes(MemoryCard.readFile(ROOT + "/loop.bin"), data), "content");
    });

    await testAsync("removeAsync cleans up", async function() {
        await MemoryCard.removeAsync(ROOT, { recursive: true });
        assert(!MemoryCard.exists(ROOT), "removed");
    });
}

function summary() {
    console.log("Result: " + passed.length + " passed, " + failed.length + " failed, " +
        skipped.length + " skipped");
    if (failed.length !== 0) throw new Error("MemoryCard tests failed: " + failed.join(", "));
}

if (card) slot2Tests();

if (cardReady) {
    cardTests();
    asyncTests().then(summary, function(error) {
        fail("async tests", error);
        summary();
    });
} else {
    summary();
}
